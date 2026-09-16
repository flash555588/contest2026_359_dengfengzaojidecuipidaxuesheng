/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_audio_store.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void put16(unsigned char *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void put32(unsigned char *p, unsigned n) { put16(p, n); put16(p + 2, n >> 16); }
static void wav(const char *path) {
  unsigned char data[364] = {0};
  memcpy(data, "RIFF", 4); put32(data + 4, sizeof(data) - 8);
  memcpy(data + 8, "WAVEfmt ", 8); put32(data + 16, 16); put16(data + 20, 1);
  put16(data + 22, 1); put32(data + 24, 16000); put32(data + 28, 32000);
  put16(data + 32, 2); put16(data + 34, 16); memcpy(data + 36, "data", 4); put32(data + 40, 320);
  FILE *f = fopen(path, "wb"); assert(f); assert(fwrite(data, 1, sizeof(data), f) == sizeof(data)); assert(!fclose(f));
}
static int number(cJSON *o, const char *k) { cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k); assert(cJSON_IsNumber(v)); return v->valueint; }

int main(int argc, char **argv) {
  assert(argc == 2); const char *root = argv[1];
  const char *package = "abcdefghijklmnopqrstuvwx1234";
  const char *neighbor = "abcdefghijklmnopqrstuvwx1234_neighbor";
  char path[QPK_STORAGE_PATH_MAX], other[QPK_STORAGE_PATH_MAX], longclip[59];
  atomic_bool cancel = false;
  cJSON *list = cJSON_CreateObject(); assert(!qpk_audio_list(root, package, list, &cancel));
  assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(list, "clips")) == 0); cJSON_Delete(list);
  assert(qpk_audio_path(root, package, "../escape", path, true) == -EINVAL);
  assert(!qpk_audio_path(root, package, "rec_000001", path, true)); wav(path);
  cJSON *info = cJSON_CreateObject(); assert(!qpk_audio_info(path, info));
  assert(number(info, "fileBytes") == 364 && number(info, "bytes") == 320 && number(info, "durationMs") == 10);
  assert(number(info, "sampleRate") == 16000 && number(info, "channels") == 1 && number(info, "bitsPerSample") == 16);
  cJSON_Delete(info);
  assert(!qpk_audio_path(root, neighbor, "private", other, true)); wav(other);
  memset(longclip, 'x', 58); longclip[58] = 0;
  assert(!qpk_audio_path(root, package, longclip, path, true)); wav(path);
  assert(!qpk_audio_path(root, package, "broken", path, true)); wav(path);
  assert(!truncate(path, 100)); info = cJSON_CreateObject(); assert(qpk_audio_info(path, info) == -EBADMSG); cJSON_Delete(info);
  assert(!qpk_storage_write(root, package, "settings", "{}", 2));
  assert(!qpk_audio_path(root, package, "pending", path, true));
  char temp[QPK_STORAGE_PATH_MAX + 4]; snprintf(temp, sizeof(temp), "%s.tmp", path); wav(temp);
  assert(!qpk_audio_path(root, package, "linked", path, true)); assert(!symlink(other, path));
  assert(qpk_audio_path(root, package, "linked", path, false) == -EPERM);
  assert(!qpk_audio_path(root, package, "escape", path, true));
  *strrchr(path, '/') = 0; assert(!rmdir(path));
  char outside[QPK_STORAGE_PATH_MAX]; snprintf(outside, sizeof(outside), "%s", other); *strrchr(outside, '/') = 0;
  assert(!symlink(outside, path));
  assert(qpk_audio_path(root, package, "escape", temp, false) == -EPERM);
  list = cJSON_CreateObject(); assert(!qpk_audio_list(root, package, list, &cancel));
  cJSON *clips = cJSON_GetObjectItemCaseSensitive(list, "clips"); assert(cJSON_GetArraySize(clips) == 3);
  unsigned valid = 0, broken = 0; cJSON *entry;
  cJSON_ArrayForEach(entry, clips) {
    const char *name = cJSON_GetObjectItemCaseSensitive(entry, "clip")->valuestring;
    assert(!strcmp(name, "rec_000001") || !strcmp(name, "broken") || !strcmp(name, longclip));
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(entry, "valid"))) valid++; else broken++;
  }
  assert(valid == 2 && broken == 1); cJSON_Delete(list);
  atomic_store(&cancel, true); list = cJSON_CreateObject(); assert(qpk_audio_list(root, package, list, &cancel) == -ECANCELED); cJSON_Delete(list);
  assert(!qpk_storage_remove_package(root, package));
  info = cJSON_CreateObject(); assert(!qpk_audio_info(other, info)); cJSON_Delete(info);
  puts("PASS: WAV header/length metadata, long clip keys, persistent list, corrupt/temp files, symlink and shared-prefix isolation, cancellation, scoped cleanup");
  return 0;
}
