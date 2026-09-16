/* SPDX-License-Identifier: Apache-2.0 */
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include "glass_qpk_builder.h"
#include "glass_chat.h"
#include "qpk_espdl.h"
#include "qpk_storage.h"
#include <cJSON.h>
#include <quickjs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef QPK_BUILDER_ROOT
#ifdef CONFIG_SYSTEM_DESKTOP_QPK_DIR
#define QPK_BUILDER_ROOT CONFIG_SYSTEM_DESKTOP_QPK_DIR
#else
#define QPK_BUILDER_ROOT "/data/qpk"
#endif
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#define DRAFT_DIR QPK_BUILDER_ROOT "/.draft"
#define EXAMPLE_DIR QPK_BUILDER_ROOT "/.examples"

struct qpk_example_asset { const char *example, *file, *text; };
#include "glass_qpk_assets.inc"

/* Short metadata copies never wait for file I/O or the QuickJS compiler. */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static struct qpk_draft_info info;
static char *draft_source;
static uint32_t draft_sequence;
static uint32_t clock_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000); }
static void save_stage(unsigned stage)
{ pthread_mutex_lock(&state_lock); info.save_stage = stage; pthread_mutex_unlock(&state_lock); }
static const char *const slots[] = {DRAFT_DIR "/draft0.json", DRAFT_DIR "/draft1.json"};

struct draft_record {
  uint32_t revision;
  uint32_t sequence, format;
  bool discarded;
  char name[QPK_DRAFT_NAME_MAX + 1], slug[QPK_DRAFT_SLUG_MAX + 1];
  char *source;
};

static int directory(const char *path, bool create)
{
  struct stat st;
  if (lstat(path, &st)) {
    if (errno != ENOENT || !create) return -errno;
    if (mkdir(path, 0700) && errno != EEXIST) return -errno;
    if (lstat(path, &st)) return -errno;
  }
  return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) ? 0 : -EPERM;
}

static int root_ready(void)
{
  char path[256];
  if (strlen(QPK_BUILDER_ROOT) >= sizeof(path) || QPK_BUILDER_ROOT[0] != '/') return -EINVAL;
  strcpy(path, QPK_BUILDER_ROOT);
  /* Never follow a symlink in an ancestor and never create the data mount. */
  for (char *p = path + 1; *p; p++) if (*p == '/') {
    *p = 0; int ret = directory(path, false); *p = '/';
    if (ret) return ret;
  }
  int ret = directory(path, true);
  if (!ret) ret = directory(DRAFT_DIR, true);
  if (!ret) ret = directory(EXAMPLE_DIR, true);
  return ret;
}

static int read_file(const char *path, size_t maximum, char **out)
{
  *out = NULL;
  struct stat st;
  if (lstat(path, &st)) return -errno;
  if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) return -EPERM;
  if (st.st_size < 0 || (uintmax_t)st.st_size > maximum) return -EFBIG;
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return -errno;
  int ret = 0;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uintmax_t)st.st_size > maximum) {
    close(fd); return -EINVAL;
  }
  size_t size = st.st_size, got = 0;
  char *text = malloc(size + 1);
  if (!text) { close(fd); return -ENOMEM; }
  while (got < size) {
    ssize_t n = read(fd, text + got, size - got);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ret = n < 0 ? -errno : -EIO; break; }
    got += (size_t)n;
  }
  char extra;
  if (!ret && read(fd, &extra, 1) != 0) ret = -EIO;
  if (close(fd) && !ret) ret = -errno;
  text[got] = 0;
  if (!ret && memchr(text, 0, got)) ret = -EINVAL;
  if (ret) free(text); else *out = text;
  return ret;
}

static int write_file(const char *path, const char *text, bool replace)
{
  struct stat st;
  bool exists = lstat(path, &st) == 0;
  if (exists && (!replace || !S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) return -EEXIST;
  if (!exists && errno != ENOENT) return -errno;
  int flags = O_WRONLY | O_CREAT | O_NOFOLLOW | (exists ? O_TRUNC : O_EXCL);
  int fd = open(path, flags, 0600);
  if (fd < 0) return -errno;
  size_t size = strlen(text), sent = 0;
  int ret = 0;
  while (sent < size) {
    ssize_t n = write(fd, text + sent, size - sent);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ret = n < 0 ? -errno : -EIO; break; }
    sent += (size_t)n;
  }
  if (!ret && fsync(fd)) ret = -errno;
  if (close(fd) && !ret) ret = -errno;
  if (!ret) {
    char *check = NULL;
    ret = read_file(path, size, &check);
    if (!ret && strcmp(check, text)) ret = -EIO;
    free(check);
  }
  return ret;
}

static cJSON *parse(const char *text)
{
  if (!text || strstr(text, "\\u0000")) return NULL;
  return cJSON_ParseWithOpts(text, NULL, true);
}

static bool integer(cJSON *value, uint32_t *out)
{
  if (!cJSON_IsNumber(value) || !(value->valuedouble >= 0 && value->valuedouble <= UINT32_MAX) ||
      value->valuedouble != (uint32_t)value->valuedouble) return false;
  *out = (uint32_t)value->valuedouble; return true;
}

static bool slug_valid(const char *slug)
{
  if (!slug || slug[0] < 'a' || slug[0] > 'z') return false;
  size_t len = strlen(slug);
  if (len > QPK_DRAFT_SLUG_MAX) return false;
  for (size_t i = 1; i < len; i++)
    if (!((slug[i] >= 'a' && slug[i] <= 'z') || (slug[i] >= '0' && slug[i] <= '9') || slug[i] == '_')) return false;
  return true;
}

static bool fields(cJSON *root, struct draft_record *out, const char *revision_key)
{
  cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
  cJSON *slug = cJSON_GetObjectItemCaseSensitive(root, "slug");
  cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "source");
  if (!cJSON_IsObject(root) || !cJSON_IsString(name) || !cJSON_IsString(slug) || !cJSON_IsString(source) ||
      !name->valuestring[0] || !source->valuestring[0] ||
      !glass_chat_utf8(name->valuestring, QPK_DRAFT_NAME_MAX, false) ||
      !slug_valid(slug->valuestring) || !glass_chat_utf8(source->valuestring, SIZE_MAX - 1, true) ||
      !integer(cJSON_GetObjectItemCaseSensitive(root, revision_key), &out->revision)) return false;
  strcpy(out->name, name->valuestring); strcpy(out->slug, slug->valuestring);
  out->source = strdup(source->valuestring);
  return out->source != NULL;
}

static cJSON *record_object(const struct draft_record *record)
{
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;
  unsigned format = record->format ? record->format : 3;
  if (!cJSON_AddNumberToObject(root, "format", format) ||
      !cJSON_AddNumberToObject(root, "revision", record->revision) ||
      !cJSON_AddStringToObject(root, "name", record->name) ||
      !cJSON_AddStringToObject(root, "slug", record->slug) ||
      !cJSON_AddStringToObject(root, "source", record->source)) {
    cJSON_Delete(root); return NULL;
  }
  if (format == 3 && (!cJSON_AddNumberToObject(root, "sequence", record->sequence) ||
      !cJSON_AddBoolToObject(root, "discarded", record->discarded))) { cJSON_Delete(root); return NULL; }
  return root;
}

static void checksum(const char *text, char hex[65])
{
  static const char digits[] = "0123456789abcdef";
  uint8_t sum[32]; qpk_dl_sha256(text, strlen(text), sum);
  for (unsigned i = 0; i < 32; i++) { hex[2 * i] = digits[sum[i] >> 4]; hex[2 * i + 1] = digits[sum[i] & 15]; }
  hex[64] = 0;
}

static char *record_pack(const struct draft_record *record)
{
  cJSON *root = record_object(record);
  char *body = root ? cJSON_PrintUnformatted(root) : NULL;
  if (!body) { cJSON_Delete(root); return NULL; }
  char hex[65]; checksum(body, hex); free(body);
  if (!cJSON_AddStringToObject(root, "sha256", hex)) { cJSON_Delete(root); return NULL; }
  body = cJSON_PrintUnformatted(root); cJSON_Delete(root); return body;
}

static int record_read(const char *path, struct draft_record *out)
{
  memset(out, 0, sizeof(*out));
  char *text = NULL; int ret = read_file(path, SIZE_MAX - 1, &text);
  if (ret) return ret;
  cJSON *root = parse(text); free(text);
  uint32_t format;
  cJSON *hash = cJSON_GetObjectItemCaseSensitive(root, "sha256");
  if (!root || !integer(cJSON_GetObjectItemCaseSensitive(root, "format"), &format) ||
      (format != 1 && format != 2 && format != 3) ||
      cJSON_GetArraySize(root) != (format == 3 ? 8 : 6) ||
      !cJSON_IsString(hash) || strlen(hash->valuestring) != 64) ret = -EBADMSG;
  if (!ret) {
    out->format = format;
    out->discarded = format == 2;
    if (format == 3) {
      cJSON *discarded = cJSON_GetObjectItemCaseSensitive(root, "discarded");
      if (!cJSON_IsBool(discarded) || !integer(cJSON_GetObjectItemCaseSensitive(root, "sequence"), &out->sequence) || !out->sequence) ret = -EBADMSG;
      out->discarded = cJSON_IsTrue(discarded);
    }
  }
  if (!ret && !out->discarded && (!fields(root, out, "revision") || !out->revision)) ret = -EBADMSG;
  if (!ret && out->discarded) {
    /* The internal sequence orders durable records independently of the
     * user-visible revision, which returns to zero after discard. */
    const char *keys[] = {"name", "slug", "source"};
    for (unsigned i = 0; i < 3; i++) {
      cJSON *item = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
      if (!cJSON_IsString(item) || item->valuestring[0]) ret = -EBADMSG;
    }
    if (!integer(cJSON_GetObjectItemCaseSensitive(root, "revision"), &out->revision) ||
        (format == 3 ? out->revision != 0 : !out->revision)) ret = -EBADMSG;
    out->discarded = true;
    if (!ret && !(out->source = strdup(""))) ret = -ENOMEM;
  }
  if (!ret && format != 3) out->sequence = out->revision;
  if (!ret) {
    cJSON *canonical = record_object(out);
    char *body = canonical ? cJSON_PrintUnformatted(canonical) : NULL;
    char hex[65];
    if (!body) ret = -ENOMEM;
    else { checksum(body, hex); if (strcmp(hex, hash->valuestring)) ret = -EBADMSG; }
    free(body); cJSON_Delete(canonical);
  }
  cJSON_Delete(root);
  if (ret) { free(out->source); out->source = NULL; }
  return ret;
}

static bool installed_record_matches(const struct draft_record *record)
{
  char dir[256], path[288], package[32], version[24];
  snprintf(dir, sizeof(dir), QPK_BUILDER_ROOT "/ai.%s", record->slug);
  if (directory(dir, false)) return false;
  snprintf(path, sizeof(path), "%s/app.js", dir);
  char *source = NULL, *manifest = NULL;
  int ret = read_file(path, SIZE_MAX - 1, &source);
  bool same = !ret && !strcmp(source, record->source);
  free(source);
  if (!same) return false;
  snprintf(path, sizeof(path), "%s/manifest.json", dir);
  if (read_file(path, 1024, &manifest)) return false;
  cJSON *root = parse(manifest); free(manifest);
  snprintf(package, sizeof(package), "ai.%s", record->slug);
  snprintf(version, sizeof(version), "1.0.%lu", (unsigned long)record->revision);
  const char *keys[] = {"name", "package", "versionName", "entry"};
  const char *values[] = {record->name, package, version, "app.js"};
  for (unsigned i = 0; i < 4; i++) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
    if (!cJSON_IsString(v) || strcmp(v->valuestring, values[i])) same = false;
  }
  cJSON_Delete(root); return same;
}

static int initialize_locked(void)
{
  int ret = root_ready();
  if (ret) return ret;
  pthread_mutex_lock(&state_lock); bool loaded = info.loaded; pthread_mutex_unlock(&state_lock);
  if (loaded) return 0;
  struct draft_record records[2];
  int errors[] = {record_read(slots[0], &records[0]), record_read(slots[1], &records[1])};
  int newest = errors[0] ? (errors[1] ? -1 : 1) :
               errors[1] || records[0].sequence >= records[1].sequence ? 0 : 1;
  if (newest < 0 && (errors[0] != -ENOENT || errors[1] != -ENOENT)) ret = -EBADMSG;
  bool installed = !ret && newest >= 0 && !records[newest].discarded && installed_record_matches(&records[newest]);
  pthread_mutex_lock(&state_lock);
  info.error = ret;
  if (!ret) {
    info.loaded = true;
    if (newest >= 0) {
      struct draft_record *r = &records[newest];
      draft_sequence = r->sequence;
      info.revision = r->discarded ? 0 : r->revision; strcpy(info.name, r->name); strcpy(info.slug, r->slug);
      if (installed) {
        info.saved_revision = r->revision;
        snprintf(info.installed_package, sizeof(info.installed_package), "ai.%s", r->slug);
      }
      if (!r->discarded) { draft_source = r->source; r->source = NULL; }
    }
  }
  pthread_mutex_unlock(&state_lock);
  free(records[0].source); free(records[1].source);
  return ret;
}

int glass_qpk_initialize(void)
{
  pthread_mutex_lock(&io_lock); int ret = initialize_locked(); pthread_mutex_unlock(&io_lock);
  return ret;
}

void glass_qpk_get(struct qpk_draft_info *out)
{ pthread_mutex_lock(&state_lock); *out = info; pthread_mutex_unlock(&state_lock); }

char *glass_qpk_preview_source(struct qpk_draft_info *out)
{
  pthread_mutex_lock(&state_lock);
  *out = info;
  char *source = info.loaded && !info.busy && draft_source ? strdup(draft_source) : NULL;
  pthread_mutex_unlock(&state_lock); return source;
}

void glass_qpk_preview_result(uint32_t revision, const char *error)
{
  pthread_mutex_lock(&state_lock);
  if (revision && info.revision == revision) {
    info.previewed_revision = error && error[0] ? 0 : revision;
    glass_chat_copy_utf8(info.preview_error, sizeof(info.preview_error), error ? error : "");
  }
  pthread_mutex_unlock(&state_lock);
}

static char *error_json(const char *error, int code, const char *detail)
{
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", error);
  cJSON_AddNumberToObject(root, "code", code);
  cJSON_AddStringToObject(root, "detail", detail ? detail : glass_qpk_error_text(code));
  char *text = cJSON_PrintUnformatted(root); cJSON_Delete(root); return text;
}

const char *glass_qpk_guide(void) { return qpk_authoring_guide; }
const char *glass_qpk_tools(void) { return qpk_authoring_tools; }

static int example_file(const struct qpk_example_asset *asset, char **out)
{
  char path[256], dir[240];
  *out = NULL;
  int ret = root_ready();
  if (ret) return ret;
  snprintf(dir, sizeof(dir), EXAMPLE_DIR "/%s", asset->example);
  ret = directory(dir, true);
  if (ret) return ret;
  snprintf(path, sizeof(path), "%s/%s", dir, asset->file);
  ret = read_file(path, strlen(asset->text), out);
  if (ret == -ENOENT) {
    ret = write_file(path, asset->text, false);
    if (!ret) ret = read_file(path, strlen(asset->text), out);
  }
  if (!ret && strcmp(*out, asset->text)) ret = -EBADMSG;
  if (ret) { free(*out); *out = NULL; }
  return ret;
}

char *glass_qpk_list_examples(void)
{
  pthread_mutex_lock(&io_lock);
  int ret = root_ready();
  cJSON *root = ret ? NULL : parse(qpk_example_catalog);
  if (root) {
    int seed_error = 0;
    for (unsigned i = 0; i < sizeof(qpk_example_assets) / sizeof(qpk_example_assets[0]); i++) {
      char *text = NULL; int e = example_file(&qpk_example_assets[i], &text); free(text);
      if (e && !seed_error) seed_error = e;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "file_check_error", seed_error);
    if (seed_error) cJSON_AddStringToObject(root, "note", "Some reference files are unavailable or changed; read tools verify each file. Existing files were preserved.");
  }
  pthread_mutex_unlock(&io_lock);
  if (ret) return error_json("storage", ret, NULL);
  char *text = root ? cJSON_PrintUnformatted(root) : NULL; cJSON_Delete(root); return text;
}

char *glass_qpk_read_example(const char *example, const char *file, unsigned start, unsigned maximum)
{
  if (!example || !file || !start || maximum < 1 || maximum > 160)
    return error_json("invalid_arguments", -EINVAL, "Use a listed example/file, start_line >= 1 and max_lines 1..160.");
  const struct qpk_example_asset *asset = NULL;
  for (unsigned i = 0; i < sizeof(qpk_example_assets) / sizeof(qpk_example_assets[0]); i++)
    if (!strcmp(example, qpk_example_assets[i].example) && !strcmp(file, qpk_example_assets[i].file)) asset = &qpk_example_assets[i];
  if (!asset) return error_json("not_an_example", -EPERM, "Only files returned by qpk_list_examples are readable.");
  char *source = NULL;
  pthread_mutex_lock(&io_lock); int ret = example_file(asset, &source); pthread_mutex_unlock(&io_lock);
  if (ret) return error_json(ret == -EBADMSG || ret == -EFBIG ? "example_changed" : "storage", ret, NULL);
  unsigned line = 1, count = 0;
  const char *p = source;
  while (*p && line < start) { if (*p++ == '\n') line++; }
  const char *begin = p;
  while (*p && count < maximum) {
    const char *end = strchr(p, '\n'); end = end ? end + 1 : p + strlen(p);
    if ((size_t)(end - begin) > QPK_EXAMPLE_PAGE_MAX) break;
    p = end; count++;
  }
  if (*p && !count) { free(source); return error_json("line_too_long", -EFBIG, NULL); }
  bool more = *p != 0;
  ((char *)p)[0] = 0;
  cJSON *root = cJSON_CreateObject();
  if (root) {
    char path[256]; snprintf(path, sizeof(path), EXAMPLE_DIR "/%s/%s", example, file);
    cJSON_AddBoolToObject(root, "ok", true); cJSON_AddStringToObject(root, "path", path);
    cJSON_AddBoolToObject(root, "verified_source", true);
    cJSON_AddNumberToObject(root, "start_line", start); cJSON_AddNumberToObject(root, "line_count", count);
    cJSON_AddBoolToObject(root, "truncated", more);
    if (more) cJSON_AddNumberToObject(root, "next_line", start + count); else cJSON_AddNullToObject(root, "next_line");
    cJSON_AddStringToObject(root, "content", begin);
  }
  free(source); char *text = root ? cJSON_PrintUnformatted(root) : NULL; cJSON_Delete(root); return text;
}

char *glass_qpk_read_draft(void)
{
  int ret = glass_qpk_initialize();
  if (ret) return error_json("storage", ret, NULL);
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;
  pthread_mutex_lock(&state_lock);
  cJSON_AddBoolToObject(root, "ok", true); cJSON_AddNumberToObject(root, "revision", info.revision);
  cJSON_AddBoolToObject(root, "has_draft", draft_source != NULL);
  cJSON_AddStringToObject(root, "name", info.name); cJSON_AddStringToObject(root, "slug", info.slug);
  cJSON_AddStringToObject(root, "source", draft_source ? draft_source : "");
  cJSON_AddNumberToObject(root, "previewed_revision", info.previewed_revision);
  cJSON_AddNumberToObject(root, "saved_revision", info.saved_revision);
  cJSON_AddStringToObject(root, "installed_package", info.installed_package);
  cJSON_AddStringToObject(root, "preview_error", info.preview_error);
  cJSON_AddNumberToObject(root, "last_error", info.error);
  pthread_mutex_unlock(&state_lock);
  char *text = cJSON_PrintUnformatted(root); cJSON_Delete(root); return text;
}

struct compile_watch { uint64_t deadline; atomic_bool *cancel; };
static uint64_t now_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static int compile_interrupt(JSRuntime *runtime, void *opaque)
{
  (void)runtime; struct compile_watch *watch = opaque;
  return now_ms() >= watch->deadline || (watch->cancel && atomic_load(watch->cancel));
}
static int check_source(const char *source, atomic_bool *cancel, char error[256])
{
  if (cancel && atomic_load(cancel)) return -ECANCELED;
  JSRuntime *runtime = JS_NewRuntime();
  if (!runtime) return -ENOMEM;
  JS_SetMemoryLimit(runtime, qpk_js_memory_budget());
#ifdef __NuttX__
  JS_SetMaxStackSize(runtime, 16 * 1024);
#else
  /* Host ASan stack frames are much larger than the RV32 release build. */
  JS_SetMaxStackSize(runtime, 256 * 1024);
#endif
  struct compile_watch watch = {now_ms() + 10000, cancel};
  JS_SetInterruptHandler(runtime, compile_interrupt, &watch);
  JSContext *context = JS_NewContext(runtime);
  if (!context) { JS_FreeRuntime(runtime); return -ENOMEM; }
  JSValue compiled = JS_Eval(context, source, strlen(source), "app.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  int ret = 0;
  if (JS_IsException(compiled)) {
    JSValue exception = JS_GetException(context);
    const char *text = JS_ToCString(context, exception);
    glass_chat_copy_utf8(error, 256, text ? text : "QuickJS compilation failed");
    if (text) JS_FreeCString(context, text);
    JS_FreeValue(context, exception); ret = -ENOEXEC;
  }
  JS_FreeValue(context, compiled); JS_FreeContext(context); JS_FreeRuntime(runtime);
  if (cancel && atomic_load(cancel)) ret = -ECANCELED;
  return ret;
}

static int persist_record(const struct draft_record *next)
{
  struct draft_record previous[2];
  int errors[] = {record_read(slots[0], &previous[0]), record_read(slots[1], &previous[1])};
  int newest = errors[0] ? (errors[1] ? -1 : 1) :
               errors[1] || previous[0].sequence >= previous[1].sequence ? 0 : 1;
  int ret = 0;
  if (newest < 0 && (errors[0] != -ENOENT || errors[1] != -ENOENT)) ret = -EBADMSG;
  if (!ret && (newest < 0 ? next->sequence != 1 : previous[newest].sequence + 1 != next->sequence)) ret = -ESTALE;
  int target = newest == 0 ? 1 : 0;
  free(previous[0].source); free(previous[1].source);
  char *body = ret ? NULL : record_pack(next);
  if (!ret && !body) ret = -ENOMEM;
  if (!ret) ret = write_file(slots[target], body, true);
  free(body); return ret;
}

char *glass_qpk_write_draft(const char *json, atomic_bool *cancel)
{
  if (!json) return error_json("invalid_arguments", -EINVAL, NULL);
  cJSON *root = parse(json);
  struct draft_record next = {0};
  bool valid = root && cJSON_GetArraySize(root) == 4 && fields(root, &next, "base_revision");
  cJSON_Delete(root);
  if (!valid) { free(next.source); return error_json("invalid_arguments", -EINVAL, "Require name, slug, complete UTF-8 source and integer base_revision."); }
  pthread_mutex_lock(&io_lock);
  int ret = initialize_locked();
  pthread_mutex_lock(&state_lock);
  uint32_t current = info.revision;
  if (!ret && info.discarding) ret = -ECANCELED;
  pthread_mutex_unlock(&state_lock);
  if (!ret && next.revision != current) ret = -ESTALE;
  if (!ret && (current == UINT32_MAX || draft_sequence == UINT32_MAX)) ret = -EOVERFLOW;
  char error[256] = {0};
  if (!ret) ret = check_source(next.source, cancel, error);
  if (!ret) { next.revision = current + 1; next.sequence = draft_sequence + 1; ret = persist_record(&next); }
  if (!ret) {
    pthread_mutex_lock(&state_lock);
    free(draft_source); draft_source = next.source; next.source = NULL;
    info.revision = next.revision; strcpy(info.name, next.name); strcpy(info.slug, next.slug);
    draft_sequence = next.sequence;
    info.previewed_revision = info.saved_revision = 0; info.installed_package[0] = 0;
    info.preview_error[0] = 0; info.error = 0;
    pthread_mutex_unlock(&state_lock);
  }
  pthread_mutex_unlock(&io_lock); free(next.source);
  if (ret) return error_json(ret == -ESTALE ? "stale_revision" : ret == -ENOEXEC ? "syntax_error" :
                            ret == -ECANCELED ? "cancelled" : "storage", ret, error[0] ? error : NULL);
  cJSON *result = cJSON_CreateObject();
  if (!result) return NULL;
  cJSON_AddBoolToObject(result, "ok", true); cJSON_AddNumberToObject(result, "revision", next.revision);
  cJSON_AddBoolToObject(result, "syntax_checked", true); cJSON_AddBoolToObject(result, "previewed", false);
  cJSON_AddStringToObject(result, "next_step", "Ask the user to tap 预览草稿, test the app, then return to give feedback or tap 保存应用.");
  char *text = cJSON_PrintUnformatted(result); cJSON_Delete(result); return text;
}

static int install_record(const struct draft_record *record)
{
  save_stage(1);
  char dir[256], script[288], manifest_path[288], version[24];
  snprintf(dir, sizeof(dir), QPK_BUILDER_ROOT "/ai.%s", record->slug);
  snprintf(script, sizeof(script), "%s/app.js", dir);
  snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", dir);
  snprintf(version, sizeof(version), "1.0.%lu", (unsigned long)record->revision);
  char package[32]; snprintf(package, sizeof(package), "ai.%s", record->slug);
  cJSON *root = cJSON_CreateObject();
  if (!root) return -ENOMEM;
  bool ok = cJSON_AddStringToObject(root, "name", record->name) && cJSON_AddStringToObject(root, "package", package) &&
            cJSON_AddStringToObject(root, "versionName", version) && cJSON_AddStringToObject(root, "entry", "app.js");
  char *manifest = ok ? cJSON_PrintUnformatted(root) : NULL; cJSON_Delete(root);
  if (!manifest) return -ENOMEM;
  int ret = 0;
  struct stat st;
  if (!lstat(dir, &st)) {
    char *old_source = NULL, *old_manifest = NULL;
    ret = directory(dir, false);
    if (!ret) ret = read_file(script, SIZE_MAX - 1, &old_source);
    if (!ret) ret = read_file(manifest_path, 1024, &old_manifest);
    if (ret || strcmp(old_source, record->source) || strcmp(old_manifest, manifest)) ret = -EEXIST;
    free(old_source); free(old_manifest); free(manifest); return ret;
  }
  if (errno != ENOENT) { ret = -errno; free(manifest); return ret; }
  /* mkdir claims a new destination; an existing package is never replaced.
   * Manifest is written last, so incomplete scripts are not launchable. */
  if (mkdir(dir, 0700)) { ret = -errno; free(manifest); return ret; }
  save_stage(2);
  ret = write_file(script, record->source, false);
  if (!ret) { save_stage(3); ret = write_file(manifest_path, manifest, false); }
  if (ret) {
    /* Only the two fixed files in the directory created by this invocation. */
    unlink(manifest_path); unlink(script); rmdir(dir);
  }
  free(manifest); return ret;
}

int glass_qpk_save(uint32_t revision)
{
  pthread_mutex_lock(&state_lock); info.save_started_ms = clock_ms(); info.save_elapsed_ms = 0; info.save_stage = 1; pthread_mutex_unlock(&state_lock);
  pthread_mutex_lock(&io_lock);
  int ret = initialize_locked();
  struct draft_record record = {0};
  pthread_mutex_lock(&state_lock);
  if (!ret && (!revision || info.revision != revision)) ret = -ESTALE;
  if (!ret && info.previewed_revision != revision) ret = -EAGAIN;
  if (!ret) {
    record.revision = revision; strcpy(record.name, info.name); strcpy(record.slug, info.slug);
    record.source = draft_source ? strdup(draft_source) : NULL;
    if (!record.source) ret = -ENOMEM;
  }
  pthread_mutex_unlock(&state_lock);
  if (!ret) ret = install_record(&record);
  free(record.source);
  pthread_mutex_lock(&state_lock);
  info.error = ret;
  info.save_stage = ret ? 5 : 4; info.save_elapsed_ms = clock_ms() - info.save_started_ms;
  if (!ret) { info.saved_revision = revision; snprintf(info.installed_package, sizeof(info.installed_package), "ai.%s", info.slug); }
  pthread_mutex_unlock(&state_lock); pthread_mutex_unlock(&io_lock); return ret;
}

int glass_qpk_discard(void)
{
  pthread_mutex_lock(&io_lock);
  int ret = initialize_locked();
  struct draft_record empty = {.discarded = true, .source = ""};
  pthread_mutex_lock(&state_lock);
  if (!ret && draft_sequence == UINT32_MAX) ret = -EOVERFLOW;
  empty.sequence = draft_sequence + 1;
  pthread_mutex_unlock(&state_lock);
  if (!ret) ret = persist_record(&empty);
  if (!ret) {
    pthread_mutex_lock(&state_lock);
    free(draft_source); draft_source = NULL;
    bool busy = info.busy, discarding = info.discarding;
    memset(&info, 0, sizeof(info));
    info.loaded = true; info.busy = busy; info.discarding = discarding;
    draft_sequence = empty.sequence;
    pthread_mutex_unlock(&state_lock);
  }
  pthread_mutex_unlock(&io_lock);
  return ret;
}

#include "glass_qpk_remove.inc"
enum background_operation { QPK_LOAD, QPK_SAVE, QPK_DISCARD, QPK_REMOVE };
struct background_job { uint32_t revision; enum background_operation operation; char dir[48], package[48]; };
static void *background(void *arg)
{
  struct background_job *job = arg;
  bool removing = job->operation == QPK_REMOVE;
  int ret = removing ? remove_installed(job->dir, job->package) : job->operation == QPK_DISCARD ? glass_qpk_discard() :
            job->operation == QPK_SAVE ? glass_qpk_save(job->revision) : glass_qpk_initialize();
  free(job);
  pthread_mutex_lock(&state_lock);
  info.busy = info.discarding = info.removing = false;
  if (removing) info.remove_error = ret; else info.error = ret;
  pthread_mutex_unlock(&state_lock);
  return NULL;
}

static int start_job(enum background_operation operation, uint32_t revision, const char *dir, const char *package)
{
  pthread_mutex_lock(&state_lock);
  if (info.busy) { pthread_mutex_unlock(&state_lock); return -EBUSY; }
  if (operation == QPK_LOAD && info.loaded) { pthread_mutex_unlock(&state_lock); return 0; }
  if (operation == QPK_SAVE && (revision != info.revision || info.previewed_revision != revision)) {
    pthread_mutex_unlock(&state_lock); return -EAGAIN;
  }
  struct background_job *job = malloc(sizeof(*job));
  if (!job) { pthread_mutex_unlock(&state_lock); return -ENOMEM; }
  job->revision = revision; job->operation = operation;
  if (operation == QPK_REMOVE) { strcpy(job->dir, dir); strcpy(job->package, package); info.remove_error = 0; }
  info.busy = true; info.discarding = operation == QPK_DISCARD;
  info.removing = operation == QPK_REMOVE;
  pthread_mutex_unlock(&state_lock);
  pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32768); pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t worker; int ret = pthread_create(&worker, &attr, background, job); pthread_attr_destroy(&attr);
  if (ret) { free(job); pthread_mutex_lock(&state_lock); info.busy = info.discarding = info.removing = false; info.error = -ret; pthread_mutex_unlock(&state_lock); }
  return -ret;
}

int glass_qpk_start(void) { return start_job(QPK_LOAD, 0, NULL, NULL); }
int glass_qpk_save_async(uint32_t revision) { return revision ? start_job(QPK_SAVE, revision, NULL, NULL) : -EINVAL; }
int glass_qpk_discard_async(void) { return start_job(QPK_DISCARD, 0, NULL, NULL); }
int glass_qpk_remove_async(const char *dir, const char *package)
{ return glass_qpk_removable(dir, package) ? start_job(QPK_REMOVE, 0, dir, package) : -EPERM; }

const char *glass_qpk_error_text(int error)
{
  switch (-error) {
    case 0: return "";
    case ENOSPC: return "设备存储空间不足，草稿或应用尚未保存。";
    case EEXIST: return "这个应用标识已存在；请让 AI 使用新标识保存副本。";
    case EAGAIN: return "请先预览当前草稿，返回后再保存。";
    case ESTALE: return "草稿版本已改变，请重新读取或预览。";
    case EBADMSG: return "草稿或示例文件校验失败，原文件已保留。";
    case ENOMEM: return "可用内存不足，请稍后重试。";
    case ECANCELED: return "操作已取消，之前的草稿仍保留。";
    case ENOEXEC: return "JavaScript 语法检查未通过。";
    case EBUSY: return "正在处理草稿，请稍后重试。";
    default: return "草稿操作失败，请检查设备存储或参数后重试。";
  }
}
