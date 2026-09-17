/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_chat.h"
#include "glass_chat_backend.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t chat_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t chat_wake = PTHREAD_COND_INITIALIZER;
/* Large histories belong in the runtime heap, after PSRAM initialization. */
static struct chat_snapshot *chat_state;
#define chat (*chat_state)
static bool started, send_pending, clear_pending, refresh_pending;
static atomic_bool cancelled;
static uint32_t next_id = 1;
static char *stream_text;
static size_t stream_size, stream_capacity;

const char *glass_chat_error_text(enum chat_error error)
{
  switch (error) {
    case CHAT_ERROR_CONFIG: return "请先扫码配置 AI 服务、模型和密钥。";
    case CHAT_ERROR_TIMEOUT: return "连接长时间没有收到数据，已保留收到的内容，可以重试。";
    case CHAT_ERROR_OUTPUT_LIMIT: return "模型的本次输出额度已用完，可以提高最大输出 Token 后重试。";
    case CHAT_ERROR_STREAM: return "连接在回复完成前断开，已保留收到的内容，可以重试。";
    case CHAT_ERROR_AUTH: return "密钥无效或没有访问权限，请检查 AI 配置。";
    case CHAT_ERROR_MODEL: return "接口或模型不可用，请检查服务地址和模型名称。";
    case CHAT_ERROR_LIMIT: return "服务请求过于频繁或额度不足，请稍后重试。";
    case CHAT_ERROR_TLS: return "安全连接失败，请检查设备时间和服务证书。";
    case CHAT_ERROR_RESPONSE: return "服务返回了无法读取的回复，请检查模型后重试。";
    case CHAT_ERROR_MEMORY: return "可用内存不足，关闭其他应用后再试。";
    case CHAT_ERROR_INTERRUPTED: return "上次回复因设备重启而中断，可以重新发送。";
    case CHAT_ERROR_CLEANUP: return "上一请求仍在结束中，请稍后重试。";
    default: return "暂时无法连接 AI 服务，请检查 Wi-Fi 后重试。";
  }
}

static void changed(void)
{ if (!++chat.revision) chat.revision = 1; }

static uint32_t now_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000); }

void glass_chat_stream(uint32_t id, const struct claw_stream_event *e)
{
  pthread_mutex_lock(&chat_lock);
  if (!chat_state || !chat.count || clear_pending || atomic_load(&cancelled)) goto done;
  struct chat_turn *t = &chat.turns[chat.count - 1];
  if (t->id != id || t->state != CHAT_WAITING) goto done;
  uint32_t now = now_ms();
  bool notify = e->kind != CLAW_STREAM_ACTIVITY || now - t->activity_ms >= 1000;
  t->activity_ms = now;
  if (e->kind == CLAW_STREAM_CONNECT) {
    t->stream_round++; strcpy(t->progress, "正在连接服务");
  } else if (e->kind == CLAW_STREAM_TEXT && e->text) {
    size_t length = strlen(t->reply), size = strlen(e->text);
    t->text_bytes += size;
    if (size <= 2 * 1024 * 1024 - stream_size) {
      size_t need = stream_size + size + 1;
      if (need > stream_capacity) {
        size_t capacity = stream_capacity ? stream_capacity : 8192;
        while (capacity < need) capacity *= 2;
        char *grown = realloc(stream_text, capacity);
        if (grown) { stream_text = grown; stream_capacity = capacity; }
      }
      if (need <= stream_capacity) { memcpy(stream_text + stream_size, e->text, size + 1); stream_size += size; }
    }
    /* Keep the current output visible even when the retained excerpt is full. */
    if (length + size > CHAT_REPLY_BYTES) {
      size_t drop = length > CHAT_REPLY_BYTES / 2 ? length - CHAT_REPLY_BYTES / 2 : length;
      while (drop < length && ((unsigned char)t->reply[drop] & 0xc0) == 0x80) drop++;
      memmove(t->reply, t->reply + drop, length - drop + 1); length -= drop;
      t->clipped = true;
    }
    size_t copied = glass_chat_copy_utf8(t->reply + length, sizeof(t->reply) - length, e->text);
    if (copied < size) t->clipped = true;
    strcpy(t->progress, "正在接收回复");
  } else if (e->kind == CLAW_STREAM_REASONING && e->text) {
    t->reasoning_bytes += strlen(e->text);
    size_t n = strlen(t->reasoning), size = strlen(e->text);
    if (n + size >= sizeof(t->reasoning)) {
      size_t keep = n > 512 ? 512 : n;
      const char *start = t->reasoning + n - keep;
      while ((*start & 0xc0) == 0x80) start++;
      memmove(t->reasoning, start, strlen(start) + 1);
    }
    n = strlen(t->reasoning);
    glass_chat_copy_utf8(t->reasoning + n, sizeof(t->reasoning) - n, e->text);
    strcpy(t->progress, "正在思考");
  } else if (e->kind == CLAW_STREAM_TOOL) {
    const char *name = e->text ? e->text : "";
    snprintf(t->progress, sizeof(t->progress), "%s", !strcmp(name, "qpk_write_draft") ? "正在编写应用草稿" :
      !strcmp(name, "qpk_read_example") ? "正在读取验证过的示例" : !strcmp(name, "qpk_read_draft") ? "正在检查当前草稿" : "正在查看示例目录");
  } else if (e->kind == CLAW_STREAM_USAGE) {
    if (e->has_output_tokens) { t->has_output_tokens = true; t->output_tokens = e->output_tokens; }
    if (e->has_reasoning_tokens) { t->has_reasoning_tokens = true; t->reasoning_tokens = e->reasoning_tokens; }
  } else if (e->kind == CLAW_STREAM_LIMIT) t->output_limited = true;
  if (notify) changed();
done:
  pthread_mutex_unlock(&chat_lock);
}

static void save_snapshot(struct chat_snapshot *snapshot)
{
  int ret = glass_chat_store_save(snapshot);
  pthread_mutex_lock(&chat_lock);
  if (chat.storage_error != ret) { chat.storage_error = ret; changed(); }
  pthread_mutex_unlock(&chat_lock);
}

static void refresh_settings(struct portal_ai_config *settings)
{
  memset(settings, 0, sizeof(*settings));
  int ret = glass_chat_load_settings(settings);
  pthread_mutex_lock(&chat_lock);
  bool configured = !ret;
  if (chat.configured != configured || strcmp(chat.model, settings->model)) {
    chat.configured = configured;
    snprintf(chat.model, sizeof(chat.model), "%s", settings->model);
    changed();
  }
  pthread_mutex_unlock(&chat_lock);
}

static void *chat_worker(void *unused)
{
  (void)unused;
  struct chat_snapshot *copy = calloc(1, sizeof(*copy));
  struct portal_ai_config *settings = calloc(1, sizeof(*settings));
  if (!copy || !settings) {
    free(copy); free(settings);
    pthread_mutex_lock(&chat_lock);
    started = false; chat.storage_error = -ENOMEM; changed();
    pthread_mutex_unlock(&chat_lock); return NULL;
  }
  int loaded = glass_chat_store_load(copy);
  pthread_mutex_lock(&chat_lock);
  memcpy(chat.turns, copy->turns, sizeof(chat.turns)); chat.count = copy->count;
  next_id = chat.count ? chat.turns[chat.count - 1].id + 1 : 1;
  if (!next_id) next_id = 1;
  chat.storage_error = loaded;
  pthread_mutex_unlock(&chat_lock);
  refresh_settings(settings);
  memset(settings, 0, sizeof(*settings));
  pthread_mutex_lock(&chat_lock);
  chat.loaded = true; chat.phase = CHAT_IDLE; changed();
  pthread_mutex_unlock(&chat_lock);

  for (;;) {
    pthread_mutex_lock(&chat_lock);
    while (!send_pending && !clear_pending && !refresh_pending) pthread_cond_wait(&chat_wake, &chat_lock);
    bool clear = clear_pending, send = send_pending;
    refresh_pending = false;
    if (clear) {
      clear_pending = send_pending = false;
      chat.phase = CHAT_CLEARING; changed();
      memset(copy, 0, sizeof(*copy));
      pthread_mutex_unlock(&chat_lock);
      int ret = glass_chat_store_save(copy);
      pthread_mutex_lock(&chat_lock);
      if (!ret) {
        memset(chat.turns, 0, sizeof(chat.turns)); chat.count = 0;
        next_id = 1;
      } else if (chat.count && chat.turns[chat.count - 1].state == CHAT_WAITING) {
        chat.turns[chat.count - 1].state = CHAT_CANCELLED;
      }
      chat.storage_error = ret; chat.phase = CHAT_IDLE; changed();
      pthread_mutex_unlock(&chat_lock);
      refresh_settings(settings); memset(settings, 0, sizeof(*settings)); continue;
    }
    if (!send) {
      pthread_mutex_unlock(&chat_lock); refresh_settings(settings); memset(settings, 0, sizeof(*settings)); continue;
    }
    send_pending = false;
    memcpy(copy, &chat, sizeof(*copy));
    chat.phase = atomic_load(&cancelled) ? CHAT_STOPPING : CHAT_REQUESTING; changed();
    pthread_mutex_unlock(&chat_lock);

    refresh_settings(settings);
    save_snapshot(copy);
    char *context = glass_chat_context(copy), *reply = NULL;
    struct chat_turn *turn = &copy->turns[copy->count - 1];
    enum chat_error error = CHAT_ERROR_NONE;
    if (!atomic_load(&cancelled)) {
      pthread_mutex_lock(&chat_lock); bool configured = chat.configured; pthread_mutex_unlock(&chat_lock);
      if (!configured) error = CHAT_ERROR_CONFIG;
      else if (!context) error = CHAT_ERROR_MEMORY;
      else error = glass_chat_request(settings, context, turn->prompt, turn->id, &cancelled, &reply);
    }
    free(context);
    char archive[48] = ""; uint32_t archive_bytes = 0;
    const char *complete = error == CHAT_ERROR_NONE && reply && !atomic_load(&cancelled) ? reply : stream_text;
    int archive_error = 0;
    if (complete && strlen(complete) > CHAT_REPLY_BYTES)
      archive_error = glass_chat_archive(complete, turn->id, archive, &archive_bytes);
    /* Do not retain the API key in the idle worker's configuration buffer. */
    memset(settings, 0, sizeof(*settings));
    pthread_mutex_lock(&chat_lock);
    bool reset = clear_pending;
    if (!reset) {
      struct chat_turn *current = &chat.turns[chat.count - 1];
      snprintf(current->archive, sizeof(current->archive), "%s", archive); current->archive_bytes = archive_bytes;
      if (archive_error) chat.storage_error = archive_error;
      if (atomic_load(&cancelled)) {
        current->state = CHAT_CANCELLED; current->error = CHAT_ERROR_NONE;
      } else if (error == CHAT_ERROR_NONE && reply && reply[0] && glass_chat_utf8(reply, 2 * 1024 * 1024, true)) {
        current->state = CHAT_DONE; current->error = CHAT_ERROR_NONE;
        size_t bytes = glass_chat_copy_utf8(current->reply, sizeof(current->reply), reply);
        current->clipped = strlen(reply) > bytes;
      } else {
        current->state = CHAT_FAILED;
        current->error = error == CHAT_ERROR_NONE ? CHAT_ERROR_RESPONSE : error;
      }
      memcpy(copy, &chat, sizeof(*copy)); changed();
    }
    pthread_mutex_unlock(&chat_lock);
    free(reply);
    free(stream_text); stream_text = NULL; stream_size = stream_capacity = 0;
    if (!reset) save_snapshot(copy);
    pthread_mutex_lock(&chat_lock);
    if (!clear_pending) chat.phase = CHAT_IDLE;
    changed();
    pthread_mutex_unlock(&chat_lock);
  }
  return NULL;
}

int glass_chat_start(void)
{
  pthread_mutex_lock(&chat_lock);
  if (started) { pthread_mutex_unlock(&chat_lock); return 0; }
  if (!chat_state) {
    chat_state = calloc(1, sizeof(*chat_state));
    if (!chat_state) { pthread_mutex_unlock(&chat_lock); return -ENOMEM; }
    chat.revision = 1; chat.phase = CHAT_LOADING;
  }
  pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32768);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t thread; atomic_store(&cancelled, false);
  int ret = pthread_create(&thread, &attr, chat_worker, NULL);
  pthread_attr_destroy(&attr);
  if (!ret) started = true;
  pthread_mutex_unlock(&chat_lock); return -ret;
}

void glass_chat_refresh_config(void)
{
  if (glass_chat_start()) return;
  pthread_mutex_lock(&chat_lock); refresh_pending = true;
  pthread_cond_signal(&chat_wake); pthread_mutex_unlock(&chat_lock);
}

static int queue_turn(const char *prompt, bool retry)
{
  if (!retry && (!prompt || !glass_chat_utf8(prompt, CHAT_PROMPT_BYTES, true))) return -EINVAL;
  if (glass_chat_start()) return -ENOMEM;
  pthread_mutex_lock(&chat_lock); int ret = -EBUSY;
  if (!chat.loaded || chat.phase != CHAT_IDLE) goto done;
  if (!chat.configured) { ret = -ENOKEY; goto done; }
  if (retry) {
    if (!chat.count || (chat.turns[chat.count - 1].state != CHAT_FAILED &&
                       chat.turns[chat.count - 1].state != CHAT_CANCELLED)) { ret = -EINVAL; goto done; }
  } else {
    while (*prompt == ' ' || *prompt == '\n' || *prompt == '\t') prompt++;
    size_t size = strlen(prompt);
    while (size && (prompt[size - 1] == ' ' || prompt[size - 1] == '\n' || prompt[size - 1] == '\t')) size--;
    if (!size) { ret = -EINVAL; goto done; }
    if (chat.count == CHAT_MAX_TURNS) {
      memmove(chat.turns, chat.turns + 1, (CHAT_MAX_TURNS - 1) * sizeof(chat.turns[0])); chat.count--;
    }
    struct chat_turn *t = &chat.turns[chat.count++]; memset(t, 0, sizeof(*t));
    memcpy(t->prompt, prompt, size); t->prompt[size] = 0;
  }
  struct chat_turn *t = &chat.turns[chat.count - 1];
  t->id = next_id++;
  t->reply[0] = 0; t->clipped = false; t->state = CHAT_WAITING; t->error = CHAT_ERROR_NONE;
  t->text_bytes = t->reasoning_bytes = t->output_tokens = t->reasoning_tokens = t->stream_round = 0;
  t->has_output_tokens = t->has_reasoning_tokens = t->output_limited = false;
  t->archive[0] = 0; t->archive_bytes = 0;
  t->reasoning[0] = 0; strcpy(t->progress, "等待服务响应"); t->began_ms = t->activity_ms = now_ms();
  atomic_store(&cancelled, false); send_pending = true; chat.phase = CHAT_QUEUED;
  changed(); pthread_cond_signal(&chat_wake); ret = 0;
done:
  pthread_mutex_unlock(&chat_lock); return ret;
}

int glass_chat_send(const char *prompt) { return queue_turn(prompt, false); }
int glass_chat_retry(void) { return queue_turn(NULL, true); }

void glass_chat_cancel(void)
{
  pthread_mutex_lock(&chat_lock);
  if (!chat_state) { pthread_mutex_unlock(&chat_lock); return; }
  if (chat.phase == CHAT_REQUESTING || chat.phase == CHAT_QUEUED) {
    atomic_store(&cancelled, true); chat.phase = CHAT_STOPPING; changed();
  }
  pthread_mutex_unlock(&chat_lock);
}

int glass_chat_clear(void)
{
  if (glass_chat_start()) return -ENOMEM;
  pthread_mutex_lock(&chat_lock);
  if (!chat.loaded || chat.phase == CHAT_CLEARING) { pthread_mutex_unlock(&chat_lock); return -EBUSY; }
  clear_pending = true; atomic_store(&cancelled, true); chat.phase = CHAT_CLEARING;
  changed(); pthread_cond_signal(&chat_wake); pthread_mutex_unlock(&chat_lock); return 0;
}

bool glass_chat_get(struct chat_snapshot *out)
{
  if (!out) return false;
  pthread_mutex_lock(&chat_lock);
  if (!chat_state) {
    bool update = out->revision != 1;
    if (update) { memset(out, 0, sizeof(*out)); out->revision = 1; out->phase = CHAT_LOADING; }
    pthread_mutex_unlock(&chat_lock); return update;
  }
  bool update = out->revision != chat.revision;
  if (update) memcpy(out, &chat, sizeof(*out));
  pthread_mutex_unlock(&chat_lock); return update;
}
