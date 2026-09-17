/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_audio_store.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char clip_chars[] =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";

static int parents(char *path, bool create)
{
  for (char *p = path + 1; *p; p++) if (*p == '/') {
    *p = 0; struct stat st; int ret = 0;
    if (lstat(path, &st)) {
      if (errno != ENOENT || !create) ret = -errno;
      else if (mkdir(path, 0700)) ret = -errno;
    } else if (!S_ISDIR(st.st_mode)) ret = -EPERM;
    *p = '/'; if (ret) return ret;
  }
  return 0;
}

int qpk_audio_path(const char *root, const char *package, const char *clip,
                   char path[QPK_STORAGE_PATH_MAX], bool create)
{
  if (!clip || !*clip || strlen(clip) > QPK_STORAGE_KEY_MAX - 6 ||
      strspn(clip, clip_chars) != strlen(clip)) return -EINVAL;
  char key[QPK_STORAGE_KEY_MAX + 1]; snprintf(key, sizeof(key), "audio_%s", clip);
  int ret = qpk_storage_path(root, package, key, path, QPK_STORAGE_PATH_MAX);
  if (!ret) ret = parents(path, create);
  if (ret) return ret;
  struct stat st;
  if (!lstat(path, &st) && !S_ISREG(st.st_mode)) return -EPERM;
  return 0;
}

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const unsigned char *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

int qpk_audio_info(const char *path, cJSON *out)
{
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return -errno;
  struct stat st; unsigned char header[44]; size_t used = 0;
  int ret = fstat(fd, &st) ? -errno : !S_ISREG(st.st_mode) ? -EPERM : 0;
  while (!ret && used < sizeof(header)) {
    ssize_t n = read(fd, header + used, sizeof(header) - used);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ret = n < 0 ? -errno : -EBADMSG; break; }
    used += n;
  }
  close(fd); if (ret) return ret;
  unsigned channels = le16(header + 22), rate = le32(header + 24);
  uint32_t bytes = le32(header + 40);
  if (memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVEfmt ", 8) ||
      le32(header + 16) != 16 || le16(header + 20) != 1 ||
      le16(header + 34) != 16 || memcmp(header + 36, "data", 4) ||
      (channels != 1 && channels != 2) || rate < 8000 || rate > 48000 ||
      le16(header + 32) != channels * 2 || le32(header + 28) != rate * channels * 2 ||
      bytes % (channels * 2) || (uint64_t)st.st_size != (uint64_t)bytes + 44 ||
      (uint64_t)le32(header + 4) != (uint64_t)bytes + 36) return -EBADMSG;
  return cJSON_AddNumberToObject(out, "fileBytes", st.st_size) &&
         cJSON_AddNumberToObject(out, "bytes", bytes) &&
         cJSON_AddNumberToObject(out, "sampleRate", rate) &&
         cJSON_AddNumberToObject(out, "channels", channels) &&
         cJSON_AddNumberToObject(out, "bitsPerSample", 16) &&
         cJSON_AddNumberToObject(out, "durationMs", (uint64_t)bytes * 1000 / (rate * channels * 2)) ? 0 : -ENOMEM;
}

static int list_keys(const char *root, const char *package, const char *path,
                      const char *key, unsigned depth, cJSON *clips,
                      const atomic_bool *cancel)
{
  if (atomic_load(cancel)) return -ECANCELED;
  int ret = 0;
  if (strlen(key) > 6 && !strncmp(key, "audio_", 6)) {
    char value[QPK_STORAGE_PATH_MAX], expected[QPK_STORAGE_PATH_MAX];
    if (snprintf(value, sizeof(value), "%s/value", path) >= (int)sizeof(value)) return -ENAMETOOLONG;
    ret = qpk_storage_path(root, package, key, expected, sizeof(expected));
    if (ret) return ret;
    struct stat st;
    if (!strcmp(value, expected) && !lstat(value, &st) && S_ISREG(st.st_mode)) {
      cJSON *entry = cJSON_CreateObject();
      if (!entry || !cJSON_AddStringToObject(entry, "clip", key + 6)) { cJSON_Delete(entry); return -ENOMEM; }
      ret = qpk_audio_info(value, entry);
      if (ret && ret != -EBADMSG) { cJSON_Delete(entry); return ret; }
      if ((ret && !cJSON_AddNumberToObject(entry, "fileBytes", st.st_size)) ||
          !cJSON_AddBoolToObject(entry, "valid", ret == 0)) {
        cJSON_Delete(entry); return -ENOMEM;
      }
      cJSON_AddItemToArray(clips, entry);
      ret = 0;
    }
  }
  if (depth == 3) return 0;
  DIR *dir = opendir(path); if (!dir) return -errno;
  while (!ret) {
    if (atomic_load(cancel)) { ret = -ECANCELED; break; }
    errno = 0; struct dirent *entry = readdir(dir);
    if (!entry) { if (errno) ret = -errno; break; }
    size_t part = strlen(entry->d_name + (entry->d_name[0] ? 1 : 0));
    if (entry->d_name[0] != 'k' || !part || part > 30 ||
        strspn(entry->d_name + 1, clip_chars) != part || strlen(key) + part > QPK_STORAGE_KEY_MAX) continue;
    char next[QPK_STORAGE_PATH_MAX], name[QPK_STORAGE_KEY_MAX + 1];
    snprintf(name, sizeof(name), "%s%s", key, entry->d_name + 1);
    size_t prefix = strlen(name); if (prefix > 6) prefix = 6;
    if (strncmp(name, "audio_", prefix)) continue;
    if (snprintf(next, sizeof(next), "%s/%s", path, entry->d_name) >= (int)sizeof(next)) { ret = -ENAMETOOLONG; break; }
    struct stat st;
    if (!lstat(next, &st) && S_ISDIR(st.st_mode))
      ret = list_keys(root, package, next, name, depth + 1, clips, cancel);
  }
  closedir(dir); return ret;
}

int qpk_audio_list(const char *root, const char *package, cJSON *out,
                   const atomic_bool *cancel)
{
  char path[QPK_STORAGE_PATH_MAX];
  int ret = qpk_storage_path(root, package, "probe", path, sizeof(path));
  if (ret) return ret;
  cJSON *clips = cJSON_AddArrayToObject(out, "clips"); if (!clips) return -ENOMEM;
  char *keys = strstr(path + strlen(root), "/kprobe/value");
  if (!keys) return -EINVAL;
  *keys = '/'; keys[1] = 0;
  ret = parents(path, false);
  if (ret) return ret == -ENOENT ? 0 : ret;
  *keys = 0;
  return list_keys(root, package, path, "", 0, clips, cancel);
}
