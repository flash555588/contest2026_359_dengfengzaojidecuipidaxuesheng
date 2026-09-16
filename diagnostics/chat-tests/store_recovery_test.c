/* Reproduce the board's zero-byte main file and complete interrupted staging. */
#include "glass_chat.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAIN CHAT_DATA_ROOT "/chat/history.json"
#define TEMP CHAT_DATA_ROOT "/chat/history.tmp"
static void write_file(const char *path, const char *text)
{
  FILE *file = fopen(path, "wb"); assert(file);
  assert(fwrite(text, 1, strlen(text), file) == strlen(text)); assert(!fclose(file));
}
static long length(const char *path)
{ struct stat s; assert(!stat(path, &s)); return s.st_size; }
int main(void)
{
  assert(!mkdir(CHAT_DATA_ROOT, 0700));
  assert(!mkdir(CHAT_DATA_ROOT "/chat", 0700));
  write_file(MAIN, "");
  write_file(TEMP, "{\"version\":1,\"turns\":[{\"id\":1,\"state\":0,\"error\":0,\"clipped\":false,\"prompt\":\"interrupted question\",\"reply\":\"\"}]}");
  long original = length(TEMP);
  struct chat_snapshot *state = calloc(1, sizeof(*state)), *readback = calloc(1, sizeof(*readback));
  assert(state && readback);
  assert(!glass_chat_store_load(state));
  assert(state->count == 1 && state->turns[0].state == CHAT_FAILED);
  assert(state->turns[0].error == CHAT_ERROR_INTERRUPTED);
  assert(length(MAIN) == 0 && length(TEMP) == original);
  assert(!glass_chat_store_save(state));
  assert(length(MAIN) > 0 && length(TEMP) == original);
  state->turns[0].state = CHAT_DONE; state->turns[0].error = CHAT_ERROR_NONE;
  strcpy(state->turns[0].reply, "Recovered reply");
  assert(!glass_chat_store_save(state));
  assert(!glass_chat_store_load(readback) && readback->turns[0].state == CHAT_DONE);
  /* A reset during the next slot's write must retain the committed reply. */
  write_file(MAIN, "{\"version\":1,\"sequence\":3,");
  assert(!glass_chat_store_load(readback));
  assert(!strcmp(readback->turns[0].reply, "Recovered reply"));
  assert(!glass_chat_store_save(state));
  assert(!glass_chat_store_load(readback) && readback->turns[0].state == CHAT_DONE);
  /* Empty conversations are real committed records, not zero-byte files. */
  state->count = 0; assert(!glass_chat_store_save(state));
  assert(!glass_chat_store_load(readback) && readback->count == 0);
  /* A damaged pair is preserved for recovery rather than silently erased. */
  write_file(MAIN, "broken main"); write_file(TEMP, "broken staging");
  assert(glass_chat_store_load(readback) == -EINVAL);
  assert(glass_chat_store_save(state) == -EINVAL);
  assert(length(MAIN) == 11 && length(TEMP) == 14);
  /* Once storage is repaired, saving works again without a process restart. */
  assert(!unlink(MAIN)); assert(!unlink(TEMP));
  assert(!glass_chat_store_save(state));
  assert(!glass_chat_store_load(readback) && !readback->count);
  free(state); free(readback);
  puts("PASS: actual zero-byte/interrupted-staging recovery, partial writes, committed clear, damaged-file preservation and recovery without restart");
  return 0;
}
