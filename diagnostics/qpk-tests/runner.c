/* Exercise the production builder in separate processes to test durable state. */
#include "glass_qpk_builder.h"
#include "qpk_storage.h"
#include <cJSON.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *file(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0 || (uintmax_t)n >= SIZE_MAX || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
  char *p = malloc((size_t)n + 1);
  if (!p) { fclose(f); return NULL; }
  if (fread(p, 1, n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
  p[n] = 0; fclose(f); return p;
}

int main(int argc, char **argv)
{
  if (argc < 2) return 2;
  char *out = NULL;
  if (!strcmp(argv[1], "state")) out = glass_qpk_read_draft();
  else if (!strcmp(argv[1], "remove") && argc == 4) {
    int ret = glass_qpk_remove_async(argv[2], argv[3]);
    struct qpk_draft_info state;
    if (!ret) {
      do { usleep(1000); glass_qpk_get(&state); } while (state.removing);
      ret = state.remove_error;
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", ret == 0); cJSON_AddNumberToObject(result, "code", ret);
    out = cJSON_PrintUnformatted(result); cJSON_Delete(result);
  }
  else if (!strcmp(argv[1], "storage-write") && argc == 4) {
    int ret = qpk_storage_write(argv[2], argv[3], "score", "42", 2);
    out = strdup(ret ? "{\"ok\":false}" : "{\"ok\":true}");
  }
  else if (!strcmp(argv[1], "discard")) {
    int ret = argc > 2 ? glass_qpk_discard_async() : glass_qpk_discard();
    struct qpk_draft_info info;
    if (argc > 2 && !ret) {
      do { usleep(1000); glass_qpk_get(&info); } while (info.busy);
      ret = info.error;
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", ret == 0);
    cJSON_AddNumberToObject(result, "code", ret);
    out = cJSON_PrintUnformatted(result); cJSON_Delete(result);
  }
  else if (!strcmp(argv[1], "list")) out = glass_qpk_list_examples();
  else if (!strcmp(argv[1], "guide")) out = strdup(glass_qpk_guide());
  else if (!strcmp(argv[1], "read") && argc == 6)
    out = glass_qpk_read_example(argv[2], argv[3], strtoul(argv[4], NULL, 10), strtoul(argv[5], NULL, 10));
  else if (!strcmp(argv[1], "write") && argc >= 3) {
    char *text = file(argv[2]);
    atomic_bool cancelled = argc == 4;
    out = glass_qpk_write_draft(text, &cancelled); free(text);
  } else if (!strcmp(argv[1], "save") && argc >= 3) {
    int ret = glass_qpk_initialize();
    uint32_t revision = strtoul(argv[2], NULL, 10);
    if (!ret && argc >= 4) glass_qpk_preview_result(revision, argc == 5 ? argv[4] : "");
    if (!ret) ret = glass_qpk_save(revision);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", ret == 0); cJSON_AddNumberToObject(result, "code", ret);
    struct qpk_draft_info info; glass_qpk_get(&info);
    cJSON_AddNumberToObject(result, "saved_revision", info.saved_revision);
    out = cJSON_PrintUnformatted(result); cJSON_Delete(result);
  }
  if (!out) return 3;
  puts(out); free(out); return 0;
}
