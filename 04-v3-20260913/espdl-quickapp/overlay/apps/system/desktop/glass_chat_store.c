/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_chat.h"
#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#ifndef CHAT_DATA_ROOT
#define CHAT_DATA_ROOT "/data/config"
#define CHAT_ARCHIVE_ROOT "/data/files/espclaw"
#define CHAT_ARCHIVE_PARENT "/data/files"
#else
#define CHAT_ARCHIVE_ROOT CHAT_DATA_ROOT "/outputs"
#define CHAT_ARCHIVE_PARENT CHAT_DATA_ROOT
#endif
#define CHAT_DIRECTORY CHAT_DATA_ROOT "/chat"
#define CHAT_FILE CHAT_DIRECTORY "/history.json"
#define CHAT_TEMP CHAT_DIRECTORY "/history.tmp"
#define CHAT_STORE_LIMIT (CHAT_MAX_TURNS * (CHAT_PROMPT_BYTES + CHAT_REPLY_BYTES) * 6 + 4096)
/* SmartFS/VFS unlinks the destination before rename. Keep two independent
 * snapshots and overwrite only the older one; a complete JSON + sequence
 * commits that slot. history.tmp also preserves interrupted existing saves. */
static const char *const store_slots[] = {CHAT_FILE, CHAT_TEMP};
static int load_file(const char *path, struct chat_snapshot *out, uint32_t *sequence);

int glass_chat_archive(const char *text, uint32_t id, char name[48], uint32_t *bytes)
{
  name[0] = 0; *bytes = 0;
  if (!text || !glass_chat_utf8(text, 2 * 1024 * 1024, true)) return -EINVAL;
  if (mkdir(CHAT_ARCHIVE_PARENT, 0700) && errno != EEXIST) return -errno;
  if (mkdir(CHAT_ARCHIVE_ROOT, 0700) && errno != EEXIST) return -errno;
  char path[256]; int fd = -1;
  for (unsigned n = 0; n < 1000; n++) {
    snprintf(name, 48, "reply-%lu-%u.txt", (unsigned long)id, n);
    snprintf(path, sizeof(path), "%s/%s", CHAT_ARCHIVE_ROOT, name);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd >= 0 || errno != EEXIST) break;
  }
  if (fd < 0) { name[0] = 0; return -errno; }
  size_t length = strlen(text), done = 0; int error = 0;
  while (done < length) {
    ssize_t n = write(fd, text + done, length - done > 4096 ? 4096 : length - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { error = n < 0 ? -errno : -EIO; break; }
    done += n;
  }
  if (!error && fsync(fd)) error = -errno;
  if (close(fd) && !error) error = -errno;
  if (error) { name[0] = 0; return error; } /* Preserve partial data on failure. */
  *bytes = length; return 0;
}

static pthread_mutex_t page_lock = PTHREAD_MUTEX_INITIALIZER;
static struct chat_page page_state;
struct page_job { char name[48]; unsigned page; };
static void *page_worker(void *arg)
{
  struct page_job *job = arg;
  struct chat_page *result = calloc(1, sizeof(*result));
  if (!result) {
    pthread_mutex_lock(&page_lock); page_state.busy = false; page_state.error = -ENOMEM; page_state.revision++;
    pthread_mutex_unlock(&page_lock); free(job); return NULL;
  }
  char path[256]; snprintf(path, sizeof(path), "%s/%s", CHAT_ARCHIVE_ROOT, job->name);
  int fd = open(path, O_RDONLY); struct stat st;
  if (fd < 0) result->error = -errno;
  else if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size > 2 * 1024 * 1024) result->error = -EINVAL;
  else {
    const unsigned stride = CHAT_PAGE_BYTES - 4;
    result->pages = (st.st_size + stride - 1) / stride;
    result->page = job->page < result->pages ? job->page : result->pages - 1;
    if (lseek(fd, result->page * stride, SEEK_SET) < 0) result->error = -errno;
    else {
      size_t got = 0;
      while (got < CHAT_PAGE_BYTES) {
        ssize_t n = read(fd, result->text + got, CHAT_PAGE_BYTES - got);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { result->error = -errno; break; }
        if (!n) break;
        got += n;
      }
      size_t start = 0, end = got < stride ? got : stride;
      while (start < got && ((unsigned char)result->text[start] & 0xc0) == 0x80) start++;
      while (end < got && ((unsigned char)result->text[end] & 0xc0) == 0x80) end++;
      memmove(result->text, result->text + start, end - start); result->text[end - start] = 0;
    }
  }
  if (fd >= 0) close(fd);
  pthread_mutex_lock(&page_lock);
  result->revision = page_state.revision + 1; page_state = *result;
  pthread_mutex_unlock(&page_lock); free(result); free(job); return NULL;
}
int glass_chat_page_request(const char *name, unsigned page)
{
  if (!name || !*name || strlen(name) >= 48 || strstr(name, "..") ||
      strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789-.") != strlen(name)) return -EINVAL;
  pthread_mutex_lock(&page_lock);
  if (page_state.busy) { pthread_mutex_unlock(&page_lock); return -EBUSY; }
  struct page_job *job = calloc(1, sizeof(*job));
  if (!job) { pthread_mutex_unlock(&page_lock); return -ENOMEM; }
  strcpy(job->name, name); job->page = page;
  page_state.busy = true; page_state.error = 0; page_state.revision++;
  pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 16384); pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t thread; int error = pthread_create(&thread, &attr, page_worker, job); pthread_attr_destroy(&attr);
  if (error) { free(job); page_state.busy = false; page_state.error = -error; }
  pthread_mutex_unlock(&page_lock); return -error;
}
bool glass_chat_page_get(struct chat_page *out)
{
  pthread_mutex_lock(&page_lock); bool changed = out->revision != page_state.revision;
  if (changed) *out = page_state;
  pthread_mutex_unlock(&page_lock); return changed;
}

static int latest_slot(const int errors[2], const uint32_t sequence[2])
{
  if (!errors[1] && (errors[0] || sequence[1] >= sequence[0])) return 1;
  return !errors[0] ? 0 : -1;
}

static int load_error(const int errors[2])
{
  if (errors[0] == -ENOENT && errors[1] == -ENOENT) return 0;
  return errors[0] != -ENOENT ? errors[0] : errors[1];
}

bool glass_chat_utf8(const char *text, size_t limit, bool multiline)
{
  if (!text || strnlen(text, limit + 1) > limit) return false;
  const unsigned char *p = (const unsigned char *)text;
  while (*p) {
    unsigned c = *p++, extra = 0, minimum = 0;
    if (c < 128) {
      if ((c < 32 && (!multiline || (c != '\n' && c != '\t'))) || c == 127) return false;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) { c &= 31; extra = 1; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { c &= 15; extra = 2; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { c &= 7; extra = 3; minimum = 0x10000; }
    else return false;
    for (unsigned i = 0; i < extra; i++) {
      if ((*p & 0xc0) != 0x80) return false;
      c = (c << 6) | (*p++ & 63);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
  }
  return true;
}

size_t glass_chat_copy_utf8(char *out, size_t capacity, const char *text)
{
  if (!capacity) return 0;
  size_t size = strlen(text);
  if (size >= capacity) {
    size = capacity - 1;
    while (size && (((unsigned char)text[size] & 0xc0) == 0x80)) size--;
  }
  memcpy(out, text, size); out[size] = 0;
  return size;
}

static bool json_depth_ok(const char *text)
{
  unsigned depth = 0; bool quoted = false, escape = false;
  for (; *text; text++) {
    if (escape) { escape = false; continue; }
    if (quoted && *text == '\\') { escape = true; continue; }
    if (*text == '"') { quoted = !quoted; continue; }
    if (quoted) continue;
    if (*text == '{' || *text == '[') { if (++depth > 4) return false; }
    if (*text == '}' || *text == ']') { if (!depth) return false; depth--; }
  }
  return !quoted && !depth;
}

static cJSON *turns_json(const struct chat_snapshot *state)
{
  cJSON *array = cJSON_CreateArray();
  if (!array) return NULL;
  for (unsigned i = 0; i < state->count; i++) {
    const struct chat_turn *t = &state->turns[i];
    cJSON *item = cJSON_CreateObject();
    if (!item || !cJSON_AddNumberToObject(item, "id", t->id) ||
        !cJSON_AddNumberToObject(item, "state", t->state) ||
        !cJSON_AddNumberToObject(item, "error", t->error) ||
        !cJSON_AddBoolToObject(item, "clipped", t->clipped) ||
        !cJSON_AddStringToObject(item, "archive", t->archive) ||
        !cJSON_AddNumberToObject(item, "archive_bytes", t->archive_bytes) ||
        !cJSON_AddStringToObject(item, "prompt", t->prompt) ||
        !cJSON_AddStringToObject(item, "reply", t->reply)) {
      cJSON_Delete(item); cJSON_Delete(array); return NULL;
    }
    cJSON_AddItemToArray(array, item);
  }
  return array;
}

int glass_chat_store_save(const struct chat_snapshot *state)
{
  if (!state || state->count > CHAT_MAX_TURNS) return -EINVAL;
  /* Never truncate the only valid copy, including a recovered staging file.
   * Re-evaluate on each save so a transient read failure cannot latch forever. */
  struct chat_snapshot *check = calloc(1, sizeof(*check));
  if (!check) return -ENOMEM;
  int errors[2]; uint32_t sequence[2] = {0};
  for (unsigned i = 0; i < 2; i++) errors[i] = load_file(store_slots[i], check, &sequence[i]);
  free(check);
  int latest = latest_slot(errors, sequence);
  if (latest < 0 && load_error(errors)) return load_error(errors);
  uint32_t next = latest < 0 ? 1 : sequence[latest] + 1;
  if (!next) return -EOVERFLOW;
  const char *target = store_slots[latest < 0 ? 0 : 1 - latest];
  cJSON *doc = cJSON_CreateObject(), *turns = turns_json(state);
  if (!doc || !turns || !cJSON_AddNumberToObject(doc, "version", 1) ||
      !cJSON_AddNumberToObject(doc, "sequence", next)) {
    cJSON_Delete(doc); cJSON_Delete(turns); return -ENOMEM;
  }
  cJSON_AddItemToObject(doc, "turns", turns);
  char *text = cJSON_PrintUnformatted(doc); cJSON_Delete(doc);
  if (!text) return -ENOMEM;
  int ret = 0, fd = -1; size_t size = strlen(text), offset = 0;
  if (size > CHAT_STORE_LIMIT) { ret = -EFBIG; goto done; }
  if (mkdir(CHAT_DATA_ROOT, 0700) && errno != EEXIST) { ret = -errno; goto done; }
  if (mkdir(CHAT_DIRECTORY, 0700) && errno != EEXIST) { ret = -errno; goto done; }
  fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) { ret = -errno; goto done; }
  while (offset < size) {
    ssize_t n = write(fd, text + offset, size - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ret = n < 0 ? -errno : -EIO; break; }
    offset += n;
  }
  if (!ret && fsync(fd)) ret = -errno;
  if (close(fd) && !ret) ret = -errno;
  fd = -1;
done:
  if (fd >= 0) close(fd);
  free(text); return ret;
}

static bool integer(const cJSON *o, const char *key, double minimum, double maximum)
{
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  return cJSON_IsNumber(v) && v->valuedouble >= minimum && v->valuedouble <= maximum &&
         v->valuedouble == (uint32_t)v->valuedouble;
}

static int load_file(const char *path, struct chat_snapshot *out, uint32_t *sequence)
{
  out->count = 0; memset(out->turns, 0, sizeof(out->turns)); *sequence = 0;
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -errno;
  struct stat st; int ret = -EINVAL; char *text = NULL; cJSON *doc = NULL;
  if (fstat(fd, &st)) { ret = -errno; goto done; }
  if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > CHAT_STORE_LIMIT) goto done;
  text = malloc((size_t)st.st_size + 1);
  if (!text) { ret = -ENOMEM; goto done; }
  size_t offset = 0;
  while (offset < (size_t)st.st_size) {
    ssize_t n = read(fd, text + offset, (size_t)st.st_size - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ret = n < 0 ? -errno : -EIO; goto done; }
    offset += n;
  }
  text[offset] = 0;
  if (memchr(text, 0, offset) || !json_depth_ok(text)) goto done;
  doc = cJSON_ParseWithOpts(text, NULL, true);
  cJSON *array = cJSON_GetObjectItemCaseSensitive(doc, "turns");
  if (!integer(doc, "version", 1, 1) || !cJSON_IsArray(array) ||
      cJSON_GetArraySize(array) > CHAT_MAX_TURNS) goto done;
  cJSON *stored_sequence = cJSON_GetObjectItemCaseSensitive(doc, "sequence");
  if (stored_sequence) {
    if (!integer(doc, "sequence", 1, UINT32_MAX)) goto done;
    *sequence = (uint32_t)stored_sequence->valuedouble;
  }
  unsigned count = 0; uint32_t previous = 0;
  for (const cJSON *item = array->child; item; item = item->next) {
    cJSON *prompt = cJSON_GetObjectItemCaseSensitive(item, "prompt");
    cJSON *reply = cJSON_GetObjectItemCaseSensitive(item, "reply");
    if (!integer(item, "id", 1, UINT32_MAX) || !integer(item, "state", CHAT_WAITING, CHAT_CANCELLED) ||
        !integer(item, "error", CHAT_ERROR_NONE, CHAT_ERROR_STREAM) ||
        !cJSON_IsString(prompt) || !prompt->valuestring[0] ||
        !glass_chat_utf8(prompt->valuestring, CHAT_PROMPT_BYTES, true) ||
        !cJSON_IsString(reply) || !glass_chat_utf8(reply->valuestring, CHAT_REPLY_BYTES, true)) goto done;
    struct chat_turn *turn = &out->turns[count++];
    turn->id = cJSON_GetObjectItemCaseSensitive(item, "id")->valuedouble;
    if (turn->id <= previous) goto done;
    previous = turn->id;
    turn->state = cJSON_GetObjectItemCaseSensitive(item, "state")->valueint;
    turn->error = cJSON_GetObjectItemCaseSensitive(item, "error")->valueint;
    turn->clipped = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "clipped"));
    cJSON *archive = cJSON_GetObjectItemCaseSensitive(item, "archive");
    if (cJSON_IsString(archive) && strlen(archive->valuestring) < sizeof(turn->archive) &&
        !strspn(archive->valuestring, "/") && !strstr(archive->valuestring, "..") &&
        strspn(archive->valuestring, "abcdefghijklmnopqrstuvwxyz0123456789-.") == strlen(archive->valuestring))
      strcpy(turn->archive, archive->valuestring);
    cJSON *archive_bytes = cJSON_GetObjectItemCaseSensitive(item, "archive_bytes");
    if (cJSON_IsNumber(archive_bytes) && archive_bytes->valuedouble >= 0 && archive_bytes->valuedouble <= 2 * 1024 * 1024)
      turn->archive_bytes = archive_bytes->valuedouble;
    strcpy(turn->prompt, prompt->valuestring); strcpy(turn->reply, reply->valuestring);
    if (turn->state == CHAT_DONE && !turn->reply[0]) goto done;
    if (turn->state == CHAT_WAITING) {
      turn->state = CHAT_FAILED; turn->error = CHAT_ERROR_INTERRUPTED; turn->reply[0] = 0;
    }
  }
  out->count = count; ret = 0;
done:
  if (ret) { out->count = 0; memset(out->turns, 0, sizeof(out->turns)); }
  cJSON_Delete(doc); free(text); close(fd); return ret;
}

int glass_chat_store_load(struct chat_snapshot *out)
{
  if (!out) return -EINVAL;
  struct chat_snapshot *other = calloc(1, sizeof(*other));
  if (!other) return -ENOMEM;
  uint32_t sequence[2] = {0};
  int errors[2] = {load_file(store_slots[0], out, &sequence[0]),
                   load_file(store_slots[1], other, &sequence[1])};
  int latest = latest_slot(errors, sequence);
  if (latest == 1) {
    out->count = other->count;
    memcpy(out->turns, other->turns, sizeof(out->turns));
  }
  free(other);
  return latest < 0 ? load_error(errors) : 0;
}

char *glass_chat_context(const struct chat_snapshot *state)
{
  unsigned start = state->count; size_t bytes = 0;
  while (start) {
    const struct chat_turn *t = &state->turns[start - 1];
    if (t->state == CHAT_DONE) {
      size_t size = strlen(t->prompt) + strlen(t->reply);
      if (bytes + size > CHAT_CONTEXT_BYTES) break;
      bytes += size;
    }
    start--;
  }
  cJSON *array = cJSON_CreateArray(); if (!array) return NULL;
  for (unsigned i = start; i < state->count; i++) {
    const struct chat_turn *t = &state->turns[i];
    if (t->state != CHAT_DONE) continue;
    const char *roles[] = {"user", "assistant"}, *texts[] = {t->prompt, t->reply};
    for (unsigned j = 0; j < 2; j++) {
      cJSON *m = cJSON_CreateObject();
      if (!m || !cJSON_AddStringToObject(m, "role", roles[j]) || !cJSON_AddStringToObject(m, "content", texts[j])) {
        cJSON_Delete(m); cJSON_Delete(array); return NULL;
      }
      cJSON_AddItemToArray(array, m);
    }
  }
  char *result = cJSON_PrintUnformatted(array); cJSON_Delete(array); return result;
}
