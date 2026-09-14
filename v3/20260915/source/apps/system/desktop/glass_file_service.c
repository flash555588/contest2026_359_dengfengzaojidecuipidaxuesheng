/* Filesystem ownership stays with a short-lived, detached worker. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "glass_file_service.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#ifdef __NuttX__
#define GLASS_FILE_STACK_SIZE 8192
#else
#define GLASS_FILE_STACK_SIZE (64 * 1024)
#endif
#define GLASS_FILE_COPY_BUFFER 4096

struct glass_file_job
{
  pthread_mutex_t mutex;
  unsigned references;
  bool cancel_requested;
  enum glass_file_action action;
  unsigned page;
  char root[GLASS_FILE_PATH_CAP];
  char path[GLASS_FILE_PATH_CAP];
  char source[GLASS_FILE_PATH_CAP];
  char name[GLASS_FILE_NAME_CAP];
  struct glass_file_status status;
  struct glass_file_result result;
};

static pthread_mutex_t g_file_service_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_file_service_active;

static int glass_fs_errno(void)
{
  return errno ? errno : EIO;
}

static bool glass_fs_cancelled(struct glass_file_job *job)
{
  pthread_mutex_lock(&job->mutex);
  bool cancelled = job->cancel_requested;
  pthread_mutex_unlock(&job->mutex);
  return cancelled;
}

static void glass_fs_remember_error(int value, int *error, int *cleanup_error)
{
  if (!value) return;
  if (!*error) *error = value;
  else if (!*cleanup_error) *cleanup_error = value;
}

/* close is deliberately not retried: after EINTR descriptor ownership is
 * platform-dependent, and retrying could close an unrelated reused fd.
 */
static void glass_fs_close(int *fd, int *error, int *cleanup_error)
{
  if (*fd < 0) return;
  int owned = *fd;
  *fd = -1;
  if (close(owned) < 0)
    glass_fs_remember_error(glass_fs_errno(), error, cleanup_error);
}

static bool glass_fs_name_valid(const char *name)
{
  size_t length = strlen(name);
  if (!length || length >= GLASS_FILE_NAME_CAP ||
      !strcmp(name, ".") || !strcmp(name, "..")) return false;
  for (size_t i = 0; i < length; i++)
    {
      unsigned char c = (unsigned char)name[i];
      if (c < 32 || c == 127 || c == '/' || c == '\\') return false;
    }
  return true;
}

static int glass_fs_path_syntax(const char *path)
{
  size_t begin = path[0] == '/' ? 1 : 0;
  if (!path[begin]) return EINVAL;
  for (size_t i = begin; ; i++)
    {
      unsigned char c = (unsigned char)path[i];
      if (c && c != '/')
        {
          if (c < 32 || c == 127 || c == '\\') return EINVAL;
          continue;
        }
      size_t length = i - begin;
      if (!length || length >= GLASS_FILE_NAME_CAP ||
          (length == 1 && path[begin] == '.') ||
          (length == 2 && path[begin] == '.' && path[begin + 1] == '.'))
        return EINVAL;
      if (!c) return 0;
      begin = i + 1;
    }
}

static int glass_fs_absolute(char *path, const char *cwd)
{
  if (!path[0] || path[0] == '/') return 0;
  char resolved[GLASS_FILE_PATH_CAP];
  int count = snprintf(resolved, sizeof(resolved), "%s%s%s", cwd,
                       !strcmp(cwd, "/") ? "" : "/", path);
  if (count < 0 || (size_t)count >= sizeof(resolved)) return ENAMETOOLONG;
  memcpy(path, resolved, (size_t)count + 1);
  return 0;
}

/* Relative test roots are resolved only here, never on the UI thread. */
static int glass_fs_resolve_paths(struct glass_file_job *job)
{
  char *paths[] = {job->root, job->path, job->source};
  bool needs_cwd = false;
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
    {
      if (!paths[i][0]) continue;
      int error = glass_fs_path_syntax(paths[i]);
      if (error) return error;
      if (paths[i][0] != '/') needs_cwd = true;
    }
  if (needs_cwd)
    {
      char cwd[GLASS_FILE_PATH_CAP];
      if (glass_fs_cancelled(job)) return ECANCELED;
      if (!getcwd(cwd, sizeof(cwd))) return glass_fs_errno();
      for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
        {
          int error = glass_fs_absolute(paths[i], cwd);
          if (error) return error;
        }
    }
  return 0;
}

static int glass_fs_lstat(struct glass_file_job *job, const char *path,
                      struct stat *info)
{
  for (;;)
    {
      if (glass_fs_cancelled(job)) return ECANCELED;
      if (lstat(path, info) == 0) return 0;
      int error = glass_fs_errno();
      if (error != EINTR) return error;
      sched_yield();
    }
}

static int glass_fs_validate_path(struct glass_file_job *job, const char *path,
                              bool directory, struct stat *info)
{
  size_t root_length = strlen(job->root);
  if (strncmp(path, job->root, root_length) ||
      (path[root_length] && path[root_length] != '/')) return EPERM;

  char checked[GLASS_FILE_PATH_CAP];
  memcpy(checked, path, strlen(path) + 1);
  for (size_t i = 1; ; i++)
    {
      if (checked[i] && checked[i] != '/') continue;
      char saved = checked[i];
      checked[i] = 0;
      int error = glass_fs_lstat(job, checked, info);
      checked[i] = saved;
      if (error) return error;
      if (!S_ISDIR(info->st_mode) && !S_ISREG(info->st_mode)) return EPERM;
      if ((saved || directory) && !S_ISDIR(info->st_mode)) return ENOTDIR;
      if (!saved) return 0;
    }
}

static int glass_fs_target(char *target, const char *directory, const char *name)
{
  if (!glass_fs_name_valid(name)) return EINVAL;
  int count = snprintf(target, GLASS_FILE_PATH_CAP, "%s/%s", directory, name);
  return count < 0 || count >= GLASS_FILE_PATH_CAP ? ENAMETOOLONG : 0;
}

static int glass_fs_open(struct glass_file_job *job, const char *path, int flags,
                     int *fd)
{
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  for (;;)
    {
      if (glass_fs_cancelled(job)) return ECANCELED;
      *fd = open(path, flags, 0666);
      if (*fd >= 0) return 0;
      int error = glass_fs_errno();
      if (error != EINTR) return error;
      sched_yield();
    }
}

static int glass_fs_check_opened(struct glass_file_job *job, int fd,
                             const struct stat *expected, struct stat *actual)
{
  for (;;)
    {
      if (glass_fs_cancelled(job)) return ECANCELED;
      if (fstat(fd, actual) == 0) break;
      int error = glass_fs_errno();
      if (error != EINTR) return error;
    }
  if (!S_ISREG(actual->st_mode)) return EPERM;
  if (expected && (expected->st_dev != actual->st_dev ||
                   expected->st_ino != actual->st_ino)) return EBUSY;
  return 0;
}

static void glass_fs_capacity(struct glass_file_job *job)
{
  struct statvfs space;
  for (;;)
    {
      if (glass_fs_cancelled(job)) return;
      if (statvfs(job->root, &space) == 0) break;
      int error = glass_fs_errno();
      if (error == EINTR) continue;
      job->result.space_error = error;
      return;
    }
  uint64_t unit = space.f_frsize ? space.f_frsize : space.f_bsize;
  if (!unit || (uint64_t)space.f_blocks > UINT64_MAX / unit ||
      (uint64_t)space.f_bavail > UINT64_MAX / unit)
    {
      job->result.space_error = EOVERFLOW;
      return;
    }
  job->result.total_bytes = (uint64_t)space.f_blocks * unit;
  job->result.free_bytes = (uint64_t)space.f_bavail * unit;
}

static int glass_fs_list(struct glass_file_job *job, int *cleanup_error)
{
  struct stat info;
  int error = glass_fs_validate_path(job, job->path, true, &info);
  if (error) return error;
  if (glass_fs_cancelled(job)) return ECANCELED;
  DIR *directory = opendir(job->path);
  if (!directory) return glass_fs_errno();
  uint64_t skip = (uint64_t)job->page * GLASS_FILE_PAGE_SIZE;
  unsigned iterations = 0;
  for (;;)
    {
      if (glass_fs_cancelled(job)) { error = ECANCELED; break; }
      errno = 0;
      struct dirent *entry = readdir(directory);
      if (!entry)
        {
          if (errno == EINTR) continue;
          error = errno;
          break;
        }
      if (++iterations % GLASS_FILE_PAGE_SIZE == 0) sched_yield();
      if (!glass_fs_name_valid(entry->d_name)) continue;
      char full[GLASS_FILE_PATH_CAP];
      if (glass_fs_target(full, job->path, entry->d_name)) continue;
      int entry_error = glass_fs_lstat(job, full, &info);
      if (entry_error == ECANCELED) { error = entry_error; break; }
      if (entry_error || (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)))
        continue;
      if (skip) { skip--; continue; }
      if (job->result.count == GLASS_FILE_PAGE_SIZE)
        {
          job->result.has_next = true;
          break;
        }
      struct glass_file_entry *out = &job->result.entries[job->result.count++];
      memcpy(out->name, entry->d_name, strlen(entry->d_name) + 1);
      out->directory = S_ISDIR(info.st_mode);
    }
  if (closedir(directory) < 0)
    glass_fs_remember_error(glass_fs_errno(), &error, cleanup_error);
  if (!error)
    {
      glass_fs_capacity(job);
      if (glass_fs_cancelled(job)) error = ECANCELED;
    }
  return error;
}

static int glass_fs_details(struct glass_file_job *job, int *cleanup_error)
{
  struct stat expected;
  struct stat actual;
  int error = glass_fs_validate_path(job, job->source, false, &expected);
  if (error) return error;
  struct glass_file_result *result = &job->result;
  result->directory = S_ISDIR(expected.st_mode);
  result->size = expected.st_size > 0 ? (uint64_t)expected.st_size : 0;
  result->mtime = (int64_t)expected.st_mtime;
  if (result->directory) return glass_fs_cancelled(job) ? ECANCELED : 0;
  int fd = -1;
  error = glass_fs_open(job, job->source, O_RDONLY | O_NONBLOCK, &fd);
  if (!error) error = glass_fs_check_opened(job, fd, &expected, &actual);
  if (!error)
    {
      result->size = actual.st_size > 0 ? (uint64_t)actual.st_size : 0;
      result->mtime = (int64_t)actual.st_mtime;
    }
  while (!error && result->preview_size < GLASS_FILE_PREVIEW_LIMIT)
    {
      if (glass_fs_cancelled(job)) { error = ECANCELED; break; }
      ssize_t count = read(fd, result->preview + result->preview_size,
                           GLASS_FILE_PREVIEW_LIMIT - result->preview_size);
      if (count < 0)
        {
          if (errno == EINTR) continue;
          error = glass_fs_errno();
        }
      else if (!count) break;
      else result->preview_size += (size_t)count;
    }
  result->preview[result->preview_size] = 0;
  result->truncated = result->size > result->preview_size;
  for (size_t i = 0; i < result->preview_size; i++)
    {
      unsigned char c = (unsigned char)result->preview[i];
      if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127)
        result->binary = true;
    }
  glass_fs_close(&fd, &error, cleanup_error);
  if (!error && glass_fs_cancelled(job)) error = ECANCELED;
  return error;
}

static void glass_fs_copy_cleanup(const char *target, bool identity_known,
                              const struct stat *created, int *cleanup_error)
{
  struct stat current;
  if (!identity_known)
    {
      if (!*cleanup_error) *cleanup_error = EBUSY;
      return;
    }
  int result;
  do result = lstat(target, &current); while (result < 0 && errno == EINTR);
  if (result < 0)
    {
      if (errno != ENOENT && !*cleanup_error) *cleanup_error = glass_fs_errno();
      return;
    }
  if (!S_ISREG(current.st_mode) || current.st_dev != created->st_dev ||
      current.st_ino != created->st_ino)
    {
      if (!*cleanup_error) *cleanup_error = EBUSY;
      return;
    }
  /* Same-path external writers must not race this ownership check. */
  if (unlink(target) < 0 && errno != ENOENT && !*cleanup_error)
    *cleanup_error = glass_fs_errno();
}

static int glass_fs_copy(struct glass_file_job *job, int *cleanup_error)
{
  struct stat source_info;
  struct stat directory_info;
  struct stat opened_info;
  struct stat created_info;
  char target[GLASS_FILE_PATH_CAP];
  int error = glass_fs_validate_path(job, job->source, false, &source_info);
  if (!error && !S_ISREG(source_info.st_mode)) error = EPERM;
  if (!error) error = glass_fs_validate_path(job, job->path, true, &directory_info);
  const char *name = strrchr(job->source, '/');
  if (!error) error = name ? glass_fs_target(target, job->path, name + 1) : EINVAL;
  if (error) return error;

  /* The transfer buffer never lives on either the UI or worker stack. */
  unsigned char *buffer = malloc(GLASS_FILE_COPY_BUFFER);
  if (!buffer) return ENOMEM;
  int input = -1;
  int output = -1;
  bool created = false;
  bool identity_known = false;
  error = glass_fs_open(job, job->source, O_RDONLY | O_NONBLOCK, &input);
  if (!error) error = glass_fs_check_opened(job, input, &source_info, &opened_info);
  if (!error)
    {
      error = glass_fs_open(job, target, O_WRONLY | O_CREAT | O_EXCL, &output);
      if (!error)
        {
          created = true;
          int result;
          /* Even a just-cancelled create must record its cleanup identity. */
          do result = fstat(output, &created_info);
          while (result < 0 && errno == EINTR);
          if (result < 0) error = glass_fs_errno();
          else
            {
              identity_known = true;
              if (!S_ISREG(created_info.st_mode)) error = EPERM;
            }
        }
    }

  uint64_t bytes = 0;
  while (!error)
    {
      if (glass_fs_cancelled(job)) { error = ECANCELED; break; }
      ssize_t count = read(input, buffer, GLASS_FILE_COPY_BUFFER);
      if (count < 0)
        {
          if (errno == EINTR) { sched_yield(); continue; }
          error = glass_fs_errno();
          break;
        }
      if (!count) break;
      size_t offset = 0;
      while (!error && offset < (size_t)count)
        {
          if (glass_fs_cancelled(job)) { error = ECANCELED; break; }
          ssize_t written = write(output, buffer + offset, (size_t)count - offset);
          if (written < 0 && errno == EINTR) { sched_yield(); continue; }
          if (written <= 0) { error = written < 0 ? glass_fs_errno() : EIO; break; }
          offset += (size_t)written;
          if ((uint64_t)written > UINT64_MAX - bytes) { error = EOVERFLOW; break; }
          bytes += (uint64_t)written;
          pthread_mutex_lock(&job->mutex);
          job->status.bytes = bytes;
          pthread_mutex_unlock(&job->mutex);
        }
      sched_yield();
    }

  while (!error)
    {
      if (glass_fs_cancelled(job)) { error = ECANCELED; break; }
      if (fsync(output) == 0) break;
      if (errno != EINTR) { error = glass_fs_errno(); break; }
      sched_yield();
    }
  if (!error && glass_fs_cancelled(job)) error = ECANCELED;
  glass_fs_close(&output, &error, cleanup_error);
  glass_fs_close(&input, &error, cleanup_error);
  if (!error && glass_fs_cancelled(job)) error = ECANCELED;
  if (error && created)
    glass_fs_copy_cleanup(target, identity_known, &created_info, cleanup_error);
  free(buffer);
  return error;
}

static int glass_fs_missing(struct glass_file_job *job, const char *path)
{
  struct stat info;
  int error = glass_fs_lstat(job, path, &info);
  if (!error) return EEXIST;
  return error == ENOENT ? 0 : error;
}

static int glass_fs_mutate(struct glass_file_job *job)
{
  struct stat source_info;
  struct stat directory_info;
  char target[GLASS_FILE_PATH_CAP];
  int error;
  if (job->action == GLASS_FILE_REMOVE)
    {
      if (!strcmp(job->source, job->root)) return EPERM;
      error = glass_fs_validate_path(job, job->source, false, &source_info);
      if (error) return error;
      if (glass_fs_cancelled(job)) return ECANCELED;
      int result = S_ISDIR(source_info.st_mode) ? rmdir(job->source) :
                                                unlink(job->source);
      return result < 0 ? glass_fs_errno() : 0;
    }

  error = glass_fs_validate_path(job, job->path, true, &directory_info);
  if (error) return error;
  if (job->action == GLASS_FILE_MKDIR)
    {
      error = glass_fs_target(target, job->path, job->name);
      if (error) return error;
      if (glass_fs_cancelled(job)) return ECANCELED;
      return mkdir(target, 0777) < 0 ? glass_fs_errno() : 0;
    }

  if (!strcmp(job->source, job->root)) return EPERM;
  error = glass_fs_validate_path(job, job->source, false, &source_info);
  if (error) return error;
  const char *slash = strrchr(job->source, '/');
  if (!slash) return EINVAL;
  const char *name = job->name;
  if (job->action == GLASS_FILE_MOVE)
    {
      if (!S_ISREG(source_info.st_mode)) return EPERM;
      if (source_info.st_dev != directory_info.st_dev) return EXDEV;
      name = slash + 1;
    }
  else if ((size_t)(slash - job->source) != strlen(job->path) ||
           strncmp(job->source, job->path, strlen(job->path)))
    return EPERM;
  error = glass_fs_target(target, job->path, name);
  if (!error) error = glass_fs_missing(job, target);
  if (error) return error;
  if (glass_fs_cancelled(job)) return ECANCELED;
  return rename(job->source, target) < 0 ? glass_fs_errno() : 0;
}

static int glass_fs_perform(struct glass_file_job *job, int *cleanup_error)
{
  int error = glass_fs_resolve_paths(job);
  struct stat root_info;
  if (!error) error = glass_fs_validate_path(job, job->root, true, &root_info);
  if (error) return error;
  switch (job->action)
    {
      case GLASS_FILE_LIST: return glass_fs_list(job, cleanup_error);
      case GLASS_FILE_DETAILS: return glass_fs_details(job, cleanup_error);
      case GLASS_FILE_COPY: return glass_fs_copy(job, cleanup_error);
      case GLASS_FILE_MOVE:
      case GLASS_FILE_MKDIR:
      case GLASS_FILE_RENAME:
      case GLASS_FILE_REMOVE: return glass_fs_mutate(job);
      default: return EINVAL;
    }
}

static void glass_fs_unref(struct glass_file_job *job)
{
  pthread_mutex_lock(&job->mutex);
  bool destroy = --job->references == 0;
  pthread_mutex_unlock(&job->mutex);
  if (destroy)
    {
      pthread_mutex_destroy(&job->mutex);
      free(job);
    }
}

static void *glass_fs_worker(void *argument)
{
  struct glass_file_job *job = argument;
  int cleanup_error = 0;
  int error = glass_fs_perform(job, &cleanup_error);
  if (cleanup_error)
    fprintf(stderr, "file service cleanup: errno=%d cleanup_errno=%d\n",
            error, cleanup_error);
  pthread_mutex_lock(&job->mutex);
  job->status.error = error;
  job->status.cleanup_error = cleanup_error;
  job->status.cancelled = error == ECANCELED;
  job->status.done = true;
  pthread_mutex_unlock(&job->mutex);

  /* Keep busy asserted through the worker's final ownership release. */
  glass_fs_unref(job);
  pthread_mutex_lock(&g_file_service_mutex);
  g_file_service_active = false;
  pthread_mutex_unlock(&g_file_service_mutex);
  return NULL;
}

static int glass_fs_copy_argument(char *output, size_t capacity, const char *input,
                              bool required)
{
  if (!input)
    {
      output[0] = 0;
      return required ? EINVAL : 0;
    }
  size_t length = strnlen(input, capacity);
  if (length >= capacity) return ENAMETOOLONG;
  if (!length && required) return EINVAL;
  memcpy(output, input, length + 1);
  return 0;
}

bool glass_file_service_busy(void)
{
  pthread_mutex_lock(&g_file_service_mutex);
  bool active = g_file_service_active;
  pthread_mutex_unlock(&g_file_service_mutex);
  return active;
}

int glass_file_submit(const struct glass_file_request *request,
                      struct glass_file_job **output)
{
  if (!output) return EINVAL;
  *output = NULL;
  if (!request || request->action < GLASS_FILE_LIST ||
      request->action > GLASS_FILE_REMOVE) return EINVAL;
  if (glass_file_service_busy()) return EBUSY;
  struct glass_file_job *job = calloc(1, sizeof(*job));
  if (!job) return ENOMEM;
  job->action = request->action;
  job->page = request->page;
  bool path_required = request->action != GLASS_FILE_DETAILS &&
                       request->action != GLASS_FILE_REMOVE;
  bool source_required = request->action != GLASS_FILE_LIST &&
                         request->action != GLASS_FILE_MKDIR;
  bool name_required = request->action == GLASS_FILE_MKDIR ||
                       request->action == GLASS_FILE_RENAME;
  int error = glass_fs_copy_argument(job->root, sizeof(job->root), request->root, true);
  if (!error) error = glass_fs_copy_argument(job->path, sizeof(job->path),
                                        request->path, path_required);
  if (!error) error = glass_fs_copy_argument(job->source, sizeof(job->source),
                                        request->source, source_required);
  if (!error) error = glass_fs_copy_argument(job->name, sizeof(job->name),
                                        request->name, name_required);
  if (!error && name_required && !glass_fs_name_valid(job->name)) error = EINVAL;
  if (error) { free(job); return error; }
  error = pthread_mutex_init(&job->mutex, NULL);
  if (error) { free(job); return error; }
  job->references = 2; /* One caller and one worker; no LVGL pointers. */

  pthread_attr_t attributes;
  error = pthread_attr_init(&attributes);
  if (error)
    {
      pthread_mutex_destroy(&job->mutex);
      free(job);
      return error;
    }
  size_t stack_size = GLASS_FILE_STACK_SIZE;
#ifdef PTHREAD_STACK_MIN
  if (stack_size < (size_t)PTHREAD_STACK_MIN) stack_size = PTHREAD_STACK_MIN;
#endif
  error = pthread_attr_setstacksize(&attributes, stack_size);
  if (!error) error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  if (!error)
    {
      pthread_mutex_lock(&g_file_service_mutex);
      if (g_file_service_active) error = EBUSY;
      else g_file_service_active = true;
      pthread_mutex_unlock(&g_file_service_mutex);
      if (!error)
        {
          pthread_t thread;
          error = pthread_create(&thread, &attributes, glass_fs_worker, job);
          if (error)
            {
              pthread_mutex_lock(&g_file_service_mutex);
              g_file_service_active = false;
              pthread_mutex_unlock(&g_file_service_mutex);
            }
        }
    }
  pthread_attr_destroy(&attributes);
  if (error)
    {
      pthread_mutex_destroy(&job->mutex);
      free(job);
      return error;
    }
  *output = job;
  return 0;
}

void glass_file_cancel(struct glass_file_job *job)
{
  if (!job) return;
  pthread_mutex_lock(&job->mutex);
  if (!job->status.done) job->cancel_requested = true;
  pthread_mutex_unlock(&job->mutex);
}

void glass_file_release(struct glass_file_job *job)
{
  if (!job) return;
  glass_file_cancel(job);
  glass_fs_unref(job);
}

int glass_file_poll(struct glass_file_job *job, struct glass_file_status *status)
{
  if (!job || !status) return EINVAL;
  pthread_mutex_lock(&job->mutex);
  *status = job->status;
  pthread_mutex_unlock(&job->mutex);
  return 0;
}

const struct glass_file_result *glass_file_get_result(struct glass_file_job *job)
{
  if (!job) return NULL;
  pthread_mutex_lock(&job->mutex);
  const struct glass_file_result *result = job->status.done ? &job->result : NULL;
  pthread_mutex_unlock(&job->mutex);
  return result;
}

