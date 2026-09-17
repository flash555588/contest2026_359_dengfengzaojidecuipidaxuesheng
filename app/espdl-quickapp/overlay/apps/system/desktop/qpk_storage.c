/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_storage.h"

#include <errno.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The volume reserves 32 bytes per name, including the terminating NUL.
 * Keep names within 31 bytes: SmartFS createentry uses strlcpy(size=32).
 * Put @2 in the first package chunk to avoid an extra directory level;
 * qpk/.data plus 2 package and 3 key chunks fits DIRDEPTH=8 (root + 7).
 * @2 cannot be an application package name accepted by this API.
 */
#define STORAGE_NAME_MAX 31

static bool component_valid(const char *text, size_t limit, bool dots)
{
  size_t i;
  if (text == NULL || text[0] == '\0') return false;
  for (i = 0; text[i] != '\0'; i++)
    {
      unsigned char c = text[i];
      if (i >= limit || !((c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '-' || (dots && c == '.'))) return false;
    }
  return true;
}

static int append_chunks(char *path, size_t capacity, char prefix,
                         const char *text, bool first_package_chunk)
{
  size_t offset = strlen(path);
  while (*text != '\0')
    {
      size_t length = strnlen(text, STORAGE_NAME_MAX -
                             (first_package_chunk ? 3 : 1));
      if (offset + 2 + length >= capacity) return -ENAMETOOLONG;
      if (!first_package_chunk) path[offset++] = '/';
      path[offset++] = prefix;
      memcpy(path + offset, text, length);
      offset += length;
      path[offset] = '\0';
      text += length;
      first_package_chunk = false;
    }
  return 0;
}

int qpk_storage_path(const char *root, const char *package, const char *key,
                     char *path, size_t capacity)
{
  int length;
  int ret;
  if (root == NULL || root[0] != '/' || path == NULL || capacity == 0 ||
      !component_valid(package, QPK_STORAGE_PACKAGE_MAX, true) ||
      !component_valid(key, QPK_STORAGE_KEY_MAX, false)) return -EINVAL;
  length = snprintf(path, capacity, "%s/@2", root);
  if (length < 0 || (size_t)length >= capacity) return -ENAMETOOLONG;
  ret = append_chunks(path, capacity, 'p', package, true);
  if (ret < 0) return ret;
  ret = append_chunks(path, capacity, 'k', key, false);
  if (ret < 0) return ret;
  length = strlen(path);
  if ((size_t)length + sizeof("/value") > capacity) return -ENAMETOOLONG;
  memcpy(path + length, "/value", sizeof("/value"));
  return 0;
}

/* Skip impossible old filenames instead of passing them to SmartFS.
 * The flat hex layout takes precedence over the original per-app layout.
 */
static bool legacy_path(const char *root, const char *package, const char *key,
                         unsigned int format, char *path, size_t capacity)
{
  size_t plen = strlen(package);
  size_t klen = strlen(key);
  int length;
  if (format == 0)
    {
      char encoded[QPK_STORAGE_PACKAGE_MAX * 2 + 1];
      static const char hex[] = "0123456789abcdef";
      if (plen * 2 + 1 + klen + 4 > STORAGE_NAME_MAX) return false;
      for (size_t i = 0; i < plen; i++)
        {
          encoded[i * 2] = hex[(unsigned char)package[i] >> 4];
          encoded[i * 2 + 1] = hex[(unsigned char)package[i] & 15];
        }
      encoded[plen * 2] = '\0';
      length = snprintf(path, capacity, "%s/%s_%s.txt", root, encoded, key);
    }
  else
    {
      if (plen > STORAGE_NAME_MAX || klen + 4 > STORAGE_NAME_MAX ||
          strcmp(package, ".") == 0 || strcmp(package, "..") == 0) return false;
      length = snprintf(path, capacity, "%s/%s/%s.txt", root, package, key);
    }
  return length >= 0 && (size_t)length < capacity;
}

static int read_file(const char *path, char *value, size_t capacity,
                     size_t *length)
{
  FILE *file = fopen(path, "rb");
  int ret = 0;
  if (file == NULL) return -errno;
  *length = fread(value, 1, capacity, file);
  if (ferror(file)) ret = -(errno ? errno : EIO);
  if (fclose(file) != 0 && ret == 0) ret = -(errno ? errno : EIO);
  if (ret == 0 && *length > QPK_STORAGE_VALUE_MAX) ret = -EFBIG;
  return ret;
}

int qpk_storage_read(const char *root, const char *package, const char *key,
                     char *value, size_t capacity, size_t *length)
{
  char path[QPK_STORAGE_PATH_MAX];
  int ret;
  if (value == NULL || length == NULL || capacity <= QPK_STORAGE_VALUE_MAX)
    return -EINVAL;
  *length = 0;
  ret = qpk_storage_path(root, package, key, path, sizeof(path));
  if (ret < 0) return ret;
  ret = read_file(path, value, capacity, length);
  for (unsigned int format = 0; ret == -ENOENT && format < 2; format++)
    if (legacy_path(root, package, key, format, path, sizeof(path)))
      ret = read_file(path, value, capacity, length);
  return ret;
}

static int prepare_parent(char *path)
{
  for (char *p = path + 1; *p != '\0'; p++)
    {
      if (*p == '/')
        {
          struct stat info;
          int ret = 0;
          *p = '\0';
          if (mkdir(path, 0777) < 0)
            {
              if (errno != EEXIST) ret = -errno;
              else if (stat(path, &info) < 0) ret = -errno;
              else if (!S_ISDIR(info.st_mode)) ret = -ENOTDIR;
            }
          *p = '/';
          if (ret < 0) return ret;
        }
    }
  return 0;
}

int qpk_storage_write(const char *root, const char *package, const char *key,
                      const char *value, size_t length)
{
  char path[QPK_STORAGE_PATH_MAX];
  char temporary[QPK_STORAGE_PATH_MAX + 4];
  FILE *file;
  int ret;
  if (value == NULL || length > QPK_STORAGE_VALUE_MAX) return -EINVAL;
  ret = qpk_storage_path(root, package, key, path, sizeof(path));
  if (ret < 0) return ret;
  ret = prepare_parent(path);
  if (ret < 0) return ret;
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  file = fopen(temporary, "wb");
  if (file == NULL) return -errno;
  ret = fwrite(value, 1, length, file) == length ? 0 : -(errno ? errno : EIO);
  if (fclose(file) != 0 && ret == 0) ret = -(errno ? errno : EIO);
  if (ret == 0 && rename(temporary, path) < 0) ret = -errno;
  if (ret < 0) unlink(temporary);
  return ret;
}

int qpk_storage_remove(const char *root, const char *package, const char *key)
{
  char path[QPK_STORAGE_PATH_MAX];
  char legacy[QPK_STORAGE_PATH_MAX];
  int ret = qpk_storage_path(root, package, key, path, sizeof(path));
  if (ret < 0) return ret;
  /* Delete legacy copies first so a failed delete cannot expose an old value
   * after the current value disappears. No legacy data is deleted on read.
   */
  for (unsigned int format = 0; format < 2; format++)
    if (legacy_path(root, package, key, format, legacy, sizeof(legacy)) &&
        unlink(legacy) < 0 && errno != ENOENT) return -errno;
  return unlink(path) < 0 && errno != ENOENT ? -errno : 0;
}

int qpk_remove_tree(const char *path)
{
  struct stat st;
  if (lstat(path, &st)) return errno == ENOENT ? 0 : -errno;
  if (!S_ISDIR(st.st_mode)) return unlink(path) ? -errno : 0;
  DIR *dir = opendir(path);
  if (!dir) return -errno;
  int ret = 0;
  struct dirent *entry;
  while (!ret) {
    errno = 0; entry = readdir(dir);
    if (!entry) { if (errno) ret = -errno; break; }
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    char child[512];
    if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child)) ret = -ENAMETOOLONG;
    else ret = qpk_remove_tree(child);
  }
  closedir(dir);
  return ret ? ret : rmdir(path) ? -errno : 0;
}

/* Reject symlink ancestors before walking a package's shared chunk tree. */
static int storage_directory(const char *path)
{
  char copy[QPK_STORAGE_PATH_MAX];
  if (strlen(path) >= sizeof(copy)) return -ENAMETOOLONG;
  strcpy(copy, path);
  for (char *p = copy + 1;; p++) if (*p == '/' || !*p) {
    char saved = *p; *p = 0;
    struct stat st;
    int ret = lstat(copy, &st) ? -errno : !S_ISDIR(st.st_mode) ? -EPERM : 0;
    *p = saved;
    if (ret || !saved) return ret;
  }
}

int qpk_storage_remove_package(const char *root, const char *package)
{
  char path[QPK_STORAGE_PATH_MAX];
  int ret = qpk_storage_path(root, package, "probe", path, sizeof(path));
  if (ret) return ret;
  ret = storage_directory(root);
  if (ret) return ret == -ENOENT ? 0 : ret;
  char *keys = strstr(path + strlen(root), "/kprobe/value");
  if (!keys) return -EINVAL;
  *keys = 0;
  ret = storage_directory(path);
  if (!ret) {
    DIR *dir = opendir(path); if (!dir) return -errno;
    struct dirent *entry;
    while (!ret) {
      errno = 0; entry = readdir(dir);
      if (!entry) { if (errno) ret = -errno; break; }
      /* p children belong to other packages sharing this prefix. */
      if (entry->d_name[0] != 'k') continue;
      char child[512];
      snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
      ret = qpk_remove_tree(child);
    }
    closedir(dir);
    if (ret) return ret;
    while (strlen(path) > strlen(root)) {
      if (rmdir(path)) { if (errno != ENOTEMPTY && errno != EEXIST && errno != ENOENT) return -errno; break; }
      *strrchr(path, '/') = 0;
    }
  } else if (ret != -ENOENT) return ret;
  /* Remove both legacy layouts, including interrupted temporary writes. */
  char prefix[QPK_STORAGE_PACKAGE_MAX * 2 + 2];
  static const char hex[] = "0123456789abcdef";
  size_t n = strlen(package);
  for (size_t i = 0; i < n; i++) { prefix[2*i] = hex[(unsigned char)package[i] >> 4]; prefix[2*i+1] = hex[(unsigned char)package[i] & 15]; }
  prefix[2*n] = '_'; prefix[2*n+1] = 0;
  DIR *dir = opendir(root); if (!dir) return -errno;
  struct dirent *entry; ret = 0;
  while (!ret) {
    errno = 0; entry = readdir(dir);
    if (!entry) { if (errno) ret = -errno; break; }
    if (!strcmp(package, ".") || !strcmp(package, "..")) { ret = -EINVAL; break; }
    if (strcmp(entry->d_name, package) && strncmp(entry->d_name, prefix, strlen(prefix))) continue;
    if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= (int)sizeof(path)) ret = -ENAMETOOLONG;
    else ret = qpk_remove_tree(path);
  }
  closedir(dir); return ret;
}
