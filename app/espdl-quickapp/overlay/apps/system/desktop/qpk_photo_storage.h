/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "qpk_volume.h"
#include <stdint.h>
#ifdef __NuttX__
#include <sys/statfs.h>
#endif

#ifndef QPK_PHOTO_SD_ROOT
#define QPK_PHOTO_SD_ROOT "/sdcard/photos"
#endif
#ifndef QPK_PHOTO_INTERNAL_ROOT
#define QPK_PHOTO_INTERNAL_ROOT "/data/photos"
#endif

static inline int qpk_photo_parent(const char *root, char *parent, size_t size)
{
  size_t length = strlen(root);
  if (length >= size) return -ENAMETOOLONG;
  memcpy(parent, root, length + 1);
  char *slash = strrchr(parent, '/');
  if (!slash || slash == parent || !slash[1]) return -EINVAL;
  *slash = 0;
  struct stat st;
  if (lstat(parent, &st)) return -errno;
  if (S_ISLNK(st.st_mode)) return -EPERM;
  return S_ISDIR(st.st_mode) ? 0 : -ENODEV;
}

/* Prefer the mounted SD card. A full/broken SD card must report its error,
 * not silently spill a large image onto the small firmware data partition.
 * override is reserved for isolated host fixtures. */
static inline int qpk_photo_directory(char *out, size_t capacity,
                                      size_t required, const char *override)
{
  if (!out || !capacity) return -EINVAL;
  if (required > SIZE_MAX - 65536) return -EFBIG;
  const char *root = override;
  char parent[128];
  if (!root)
    {
      int ret = qpk_photo_parent(QPK_PHOTO_SD_ROOT, parent, sizeof(parent));
      if (!ret) ret = qpk_volume_mounted(QPK_VOLUME_SD);
      if (!ret) root = QPK_PHOTO_SD_ROOT;
      else
        {
          if (ret != -ENODEV && ret != -ENOENT && ret != -ENXIO) return ret;
          ret = qpk_volume_mounted(QPK_VOLUME_INTERNAL);
          if (ret) return ret;
          root = QPK_PHOTO_INTERNAL_ROOT;
        }
    }
  size_t length = strlen(root);
  /* Reserve room for the longest generated photo basename as well. */
  if (length + 32 >= capacity) return -ENAMETOOLONG;
  int ret = qpk_photo_parent(root, parent, sizeof(parent));
  if (ret) return ret;
  struct stat st;
#ifdef __NuttX__
  struct statfs fs;
  if (statfs(parent, &fs)) return -errno;
  uint64_t available = (uint64_t)fs.f_bavail * fs.f_bsize;
  /* Leave space for preferences and other small persistent state. */
  if ((uint64_t)required + 65536 > available) return -ENOSPC;
#else
  (void)required;
#endif
  if (mkdir(root, 0755) && errno != EEXIST) return -errno;
  if (lstat(root, &st)) return -errno;
  if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) return -EPERM;
  memcpy(out, root, length + 1);
  return 0;
}
