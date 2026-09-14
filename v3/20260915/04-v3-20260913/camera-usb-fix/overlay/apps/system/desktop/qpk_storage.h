/* SPDX-License-Identifier: Apache-2.0 */
#ifndef QPK_STORAGE_H
#define QPK_STORAGE_H

#include <stddef.h>

#define QPK_STORAGE_PACKAGE_MAX 47
#define QPK_STORAGE_KEY_MAX 64
#define QPK_STORAGE_VALUE_MAX 8192
#define QPK_STORAGE_PATH_MAX 256

/* root is a trusted absolute directory. Calls for the same key must be
 * serialized by the caller (the desktop runs JS on its single UI thread).
 * Reads return -ENOENT for a missing key and accept both previous layouts.
 */
int qpk_storage_path(const char *root, const char *package, const char *key,
                     char *path, size_t capacity);
int qpk_storage_read(const char *root, const char *package, const char *key,
                     char *value, size_t capacity, size_t *length);
int qpk_storage_write(const char *root, const char *package, const char *key,
                      const char *value, size_t length);
int qpk_storage_remove(const char *root, const char *package, const char *key);
int qpk_storage_selftest(void);

#endif
