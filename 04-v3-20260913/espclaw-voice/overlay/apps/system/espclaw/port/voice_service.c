/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_voice.h"
#include "claw_core.h"
#include "claw_tls.h"
#include "claw_webclient.h"
#include "cJSON.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CONFIG_FILE
#define CONFIG_FILE "/data/espclaw/voice.json"
#endif
#ifndef RECORD_FILE
#define RECORD_FILE "/tmp/espclaw-record.wav"
#endif
int claw_voice_chat(const char *, const claw_core_config_t *, atomic_bool *, char *, size_t);

static pthread_mutex_t voice_lock = PTHREAD_MUTEX_INITIALIZER;
static struct claw_voice_snapshot snapshot;
static atomic_bool cancelled;
static atomic_bool finish_recording;
static unsigned int record_seconds;
static bool request_answer;
static unsigned char *ca_storage;
static size_t ca_storage_size;

struct voice_config {
  char url[1025], model[129], key[2001];
  char chat_url[1025], chat_model[129], chat_key[2001], ca_file[256];
};

static void wipe(void *ptr, size_t size)
{ volatile unsigned char *p = ptr; while (size--) *p++ = 0; }

static void set_state(enum claw_voice_state state, const char *detail, int error)
{
  pthread_mutex_lock(&voice_lock);
  snapshot.state = atomic_load(&cancelled) ? CLAW_VOICE_STOPPING : state;
  snapshot.error = error;
  snprintf(snapshot.detail, sizeof(snapshot.detail), "%s", detail ? detail : "");
  pthread_mutex_unlock(&voice_lock);
}

void claw_voice_read(struct claw_voice_snapshot *out)
{
  if (!out) return;
  pthread_mutex_lock(&voice_lock);
  *out = snapshot;
  pthread_mutex_unlock(&voice_lock);
}

void claw_voice_progress(unsigned int milliseconds, unsigned int peak)
{
  pthread_mutex_lock(&voice_lock);
  snapshot.milliseconds = milliseconds;
  snapshot.peak = peak;
  pthread_mutex_unlock(&voice_lock);
}

void claw_voice_cancel(void)
{
  pthread_mutex_lock(&voice_lock);
  if (snapshot.busy) {
    atomic_store(&cancelled, true);
    snapshot.state = CLAW_VOICE_STOPPING;
  }
  pthread_mutex_unlock(&voice_lock);
}

void claw_voice_finish_recording(void)
{
  pthread_mutex_lock(&voice_lock);
  if (snapshot.busy && snapshot.state == CLAW_VOICE_RECORDING)
    atomic_store(&finish_recording, true);
  pthread_mutex_unlock(&voice_lock);
}

static int read_file(const char *path, size_t max, char **out, size_t *length)
{
  *out = NULL; *length = 0;
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -errno;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size > max) {
    close(fd); return -EINVAL;
  }
  char *data = calloc(1, st.st_size + 1);
  if (!data) { close(fd); return -ENOMEM; }
  size_t used = 0;
  while (used < st.st_size) {
    ssize_t n = read(fd, data + used, st.st_size - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { wipe(data, st.st_size); free(data); close(fd); return -EIO; }
    used += n;
  }
  close(fd);
  if (memchr(data, 0, used)) { wipe(data, used); free(data); return -EINVAL; }
  *out = data; *length = used;
  return 0;
}

static int setting(cJSON *root, const char *name, char *out, size_t capacity)
{
  cJSON *v = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!cJSON_IsString(v) || !v->valuestring[0] ||
      strnlen(v->valuestring, capacity) >= capacity) return -EINVAL;
  for (const unsigned char *p = (const unsigned char *)v->valuestring; *p; p++)
    if (*p < 32 || *p == 127) return -EINVAL;
  strcpy(out, v->valuestring);
  return 0;
}

static int load_config(struct voice_config *cfg)
{
  char *json; size_t length;
  int ret = read_file(CONFIG_FILE, 16384, &json, &length);
  if (ret) return ret;
  cJSON *root = cJSON_ParseWithOpts(json, NULL, 1);
  wipe(json, length); free(json);
  if (!root) return -EINVAL;
  ret = setting(root, "transcription_url", cfg->url, sizeof(cfg->url));
  ret |= setting(root, "transcription_model", cfg->model, sizeof(cfg->model));
  ret |= setting(root, "transcription_api_key", cfg->key, sizeof(cfg->key));
  ret |= setting(root, "chat_base_url", cfg->chat_url, sizeof(cfg->chat_url));
  ret |= setting(root, "chat_model", cfg->chat_model, sizeof(cfg->chat_model));
  ret |= setting(root, "chat_api_key", cfg->chat_key, sizeof(cfg->chat_key));
  ret |= setting(root, "ca_file", cfg->ca_file, sizeof(cfg->ca_file));
  cJSON *key = cJSON_GetObjectItemCaseSensitive(root, "transcription_api_key");
  if (cJSON_IsString(key)) wipe(key->valuestring, strlen(key->valuestring));
  key = cJSON_GetObjectItemCaseSensitive(root, "chat_api_key");
  if (cJSON_IsString(key)) wipe(key->valuestring, strlen(key->valuestring));
  cJSON_Delete(root);
  if (ret || !claw_voice_valid_url(cfg->url) || !claw_voice_valid_url(cfg->chat_url) ||
      cfg->ca_file[0] != '/') return -EINVAL;
  return 0;
}

static int setup_tls(const char *path)
{
  /* Certificate validity checks require a real wall clock. */
  if (time(NULL) < 1704067200) return -ETIME;
  char *ca; size_t size;
  int ret = read_file(path, 65536, &ca, &size);
  if (ret) return ret;
  if (ca_storage) {
    ret = ca_storage_size == size + 1 && !memcmp(ca_storage, ca, size + 1) ? 0 : -EBUSY;
    free(ca); return ret;
  }
  ret = claw_tls_initialize((unsigned char *)ca, size + 1);
  if (ret) { free(ca); return ret; }
  ca_storage = (unsigned char *)ca; ca_storage_size = size + 1;
  return 0;
}

static int save_recording(const unsigned char *wav, size_t size)
{
  /* Only the explicitly requested diagnostic recording is saved, in RAM. */
  const char *temp = RECORD_FILE ".part";
  int fd = open(temp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd < 0) return -errno;
  size_t done = 0; int ret = 0;
  while (done < size) {
    ssize_t n = write(fd, wav + done, size - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ret = -EIO; break; }
    done += n;
  }
  if (close(fd) && !ret) ret = -errno;
  if (!ret && rename(temp, RECORD_FILE)) ret = -errno;
  if (ret) unlink(temp);
  return ret;
}

static void *voice_worker(void *unused)
{
  (void)unused;
  struct voice_config *cfg = NULL;
  unsigned char *wav = NULL, *body = NULL;
  char *response = NULL, *transcript = NULL, *answer = NULL;
  size_t wav_size = 0, body_size = 0;
  const char *failure = "录音失败";
  int ret = 0;
  if (request_answer) {
    cfg = calloc(1, sizeof(*cfg));
    if (!cfg) { ret = -ENOMEM; goto done; }
    ret = load_config(cfg);
    failure = "请先配置语音识别和聊天服务";
    if (ret) goto done;
    failure = "请检查系统时间和服务证书";
    ret = setup_tls(cfg->ca_file);
    if (ret) goto done;
  }
  if (atomic_load(&cancelled)) { ret = -ECANCELED; goto done; }
  set_state(CLAW_VOICE_RECORDING, "正在录音", 0);
  ret = claw_voice_capture(record_seconds, &cancelled, &finish_recording, &wav, &wav_size);
  failure = "录音失败，请检查麦克风";
  if (ret) goto done;
  if (!request_answer) {
    ret = save_recording(wav, wav_size);
    failure = "保存录音失败";
    goto done;
  }
  if (atomic_load(&cancelled)) { ret = -ECANCELED; goto done; }
  set_state(CLAW_VOICE_TRANSCRIBING, "正在识别语音", 0);
  failure = "语音识别失败，请检查服务配置和网络";
  ret = claw_voice_multipart(cfg->model, wav, wav_size, &body, &body_size);
  free(wav); wav = NULL;
  if (ret) goto done;
  ret = claw_webclient_post_binary(cfg->url, cfg->key, claw_voice_content_type(),
                                   body, body_size, &cancelled, &response);
  free(body); body = NULL;
  if (ret) goto done;
  transcript = calloc(1, CLAW_VOICE_TEXT + 1);
  answer = calloc(1, CLAW_VOICE_TEXT + 1);
  if (!transcript || !answer) { ret = -ENOMEM; goto done; }
  ret = claw_voice_parse_transcript(response, transcript, CLAW_VOICE_TEXT + 1);
  free(response); response = NULL;
  if (ret) { failure = "未识别到有效文字，请重新录音"; goto done; }
  if (atomic_load(&cancelled)) { ret = -ECANCELED; goto done; }
  pthread_mutex_lock(&voice_lock);
  strcpy(snapshot.transcript, transcript);
  pthread_mutex_unlock(&voice_lock);
  set_state(CLAW_VOICE_ASKING, "正在等待回答", 0);
  claw_core_config_t chat = {
    .base_url = cfg->chat_url, .model = cfg->chat_model,
    .api_key = cfg->chat_key, .backend_type = "openai_compatible"
  };
  failure = "聊天请求失败，识别文字已保留";
  ret = claw_voice_chat(transcript, &chat, &cancelled, answer, CLAW_VOICE_TEXT + 1);
  if (!ret) {
    pthread_mutex_lock(&voice_lock);
    strcpy(snapshot.answer, answer);
    pthread_mutex_unlock(&voice_lock);
  }
done:
  free(wav); free(body); free(response); free(transcript); free(answer);
  if (cfg) { wipe(cfg, sizeof(*cfg)); free(cfg); }
  pthread_mutex_lock(&voice_lock);
  if (atomic_load(&cancelled)) { ret = -ECANCELED; snapshot.answer[0] = 0; }
  snapshot.error = ret;
  snapshot.state = ret == -ECANCELED ? CLAW_VOICE_CANCELLED : ret ? CLAW_VOICE_ERROR : CLAW_VOICE_DONE;
  snprintf(snapshot.detail, sizeof(snapshot.detail), "%s", ret == -ECANCELED ? "已取消" :
    ret ? failure : request_answer ? "回答完成" : "录音已保存");
  snapshot.busy = false;
  pthread_mutex_unlock(&voice_lock);
  return NULL;
}

int claw_voice_start(unsigned int seconds, bool ask)
{
  if (!seconds || seconds > CLAW_VOICE_MAX_SECONDS) return -EINVAL;
  pthread_mutex_lock(&voice_lock);
  if (snapshot.busy) { pthread_mutex_unlock(&voice_lock); return -EBUSY; }
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.busy = true; snapshot.state = CLAW_VOICE_PREPARING;
  record_seconds = seconds; request_answer = ask;
  atomic_store(&cancelled, false); atomic_store(&finish_recording, false);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32768);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t worker;
  int ret = pthread_create(&worker, &attr, voice_worker, NULL);
  pthread_attr_destroy(&attr);
  if (ret) { snapshot.busy = false; snapshot.state = CLAW_VOICE_ERROR; snapshot.error = -ret; }
  pthread_mutex_unlock(&voice_lock);
  return -ret;
}

int claw_voice_command(int argc, char **argv)
{
  if (argc == 1 && !strcmp(argv[0], "cancel")) { claw_voice_cancel(); puts("voice: cancel requested"); return 0; }
  if (argc == 1 && !strcmp(argv[0], "stop")) { claw_voice_finish_recording(); puts("voice: finish requested"); return 0; }
  if (argc == 1 && (!strcmp(argv[0], "status") || !strcmp(argv[0], "result"))) {
    struct claw_voice_snapshot *s = malloc(sizeof(*s));
    if (!s) return 1;
    claw_voice_read(s);
    printf("voice: state=%d busy=%d ms=%u peak=%u error=%d %s\n", s->state, s->busy,
           s->milliseconds, s->peak, s->error, s->detail);
    if (!strcmp(argv[0], "result")) {
      if (*s->transcript) printf("transcript: %s\n", s->transcript);
      if (*s->answer) printf("answer: %s\n", s->answer);
    }
    free(s); return 0;
  }
  if ((argc == 1 || argc == 2) && (!strcmp(argv[0], "start") || !strcmp(argv[0], "record"))) {
    char *end = NULL;
    unsigned long seconds = argc == 2 ? strtoul(argv[1], &end, 10) : 8;
    if (!seconds || seconds > CLAW_VOICE_MAX_SECONDS || (end && *end)) return 1;
    int ret = claw_voice_start(seconds, !strcmp(argv[0], "start"));
    printf("voice: start ret=%d%s\n", ret, !strcmp(argv[0], "record") ? " record=/tmp/espclaw-record.wav" : "");
    return ret ? 1 : 0;
  }
  puts("espclaw voice record|start [1..15 seconds] OR status|result|stop|cancel");
  puts("record: local WAV only; start: record, transcribe, ask (voice.json required)");
  return argc ? 1 : 0;
}
