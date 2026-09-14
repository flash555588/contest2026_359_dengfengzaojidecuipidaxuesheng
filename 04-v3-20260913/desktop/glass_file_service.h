/* Bounded asynchronous file operations shared by the v1/v3 desktop. */
#ifndef GLASS_FILE_SERVICE_H
#define GLASS_FILE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GLASS_FILE_PATH_CAP 512
#define GLASS_FILE_NAME_CAP 256
#define GLASS_FILE_PAGE_SIZE 32
#define GLASS_FILE_PREVIEW_LIMIT 2048

enum glass_file_action
{
  GLASS_FILE_LIST,
  GLASS_FILE_DETAILS,
  GLASS_FILE_COPY,
  GLASS_FILE_MOVE,
  GLASS_FILE_MKDIR,
  GLASS_FILE_RENAME,
  GLASS_FILE_REMOVE
};

struct glass_file_request
{
  enum glass_file_action action;
  const char *root;    /* Trusted application root, absolute or cwd-relative. */
  const char *path;    /* LIST directory, or mutation destination directory. */
  const char *source;  /* DETAILS/REMOVE/COPY/MOVE/RENAME source. */
  const char *name;    /* MKDIR/RENAME new single-component name. */
  unsigned page;
};

struct glass_file_entry
{
  char name[GLASS_FILE_NAME_CAP];
  bool directory;
};

struct glass_file_status
{
  bool done;
  bool cancelled;
  int error;          /* Positive errno; ECANCELED when cancellation wins. */
  int cleanup_error; /* First secondary close/unlink failure, if any. */
  uint64_t bytes;     /* Bytes successfully written by COPY. */
};

struct glass_file_result
{
  struct glass_file_entry entries[GLASS_FILE_PAGE_SIZE];
  size_t count;
  bool has_next;
  uint64_t total_bytes;
  uint64_t free_bytes;
  int space_error;    /* LIST remains usable if the capacity query fails. */
  bool directory;
  uint64_t size;
  int64_t mtime;
  char preview[GLASS_FILE_PREVIEW_LIMIT + 1];
  size_t preview_size;
  bool binary;
  bool truncated;
};

struct glass_file_job;

/* Returns 0 or a positive errno. The service copies all request strings.
 * Only one worker may perform I/O at once; EBUSY is retryable. All filesystem
 * checks run in the worker. RENAME stays in the source's parent; MOVE accepts
 * regular files only. Existing symlinks/special files and dot components are
 * rejected. External writers must serialize changes to the same paths: these
 * portable component checks do not make rename/check sequences atomic.
 */
int glass_file_submit(const struct glass_file_request *request,
                      struct glass_file_job **job);

/* Neither call performs filesystem I/O or joins a thread. release drops the
 * caller's sole reference and also cancels unfinished work. A worker reference
 * keeps the job alive until cleanup ends. Do not access a job after release.
 * Cancellation cannot undo a mutation that has already completed.
 */
void glass_file_cancel(struct glass_file_job *job);
void glass_file_release(struct glass_file_job *job);
bool glass_file_service_busy(void);
int glass_file_poll(struct glass_file_job *job,
                    struct glass_file_status *status);

/* NULL until done; otherwise immutable and valid until the caller releases. */
const struct glass_file_result *glass_file_get_result(
  struct glass_file_job *job);

#endif
