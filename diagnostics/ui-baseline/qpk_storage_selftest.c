/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "qpk_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int qpk_storage_selftest(void)
{
  char root[QPK_STORAGE_PATH_MAX];
  char path[QPK_STORAGE_PATH_MAX];
  char package[QPK_STORAGE_PACKAGE_MAX + 1];
  char key[QPK_STORAGE_KEY_MAX + 1];
  const char *packages[] = {"com.example.hello", "com.example.game2048", package};
  const char *keys[] = {"launches", "best", key};
  char *buffer;
  size_t length;
  int ret = 0;

  memset(package, 'p', sizeof(package) - 1);
  package[sizeof(package) - 1] = '\0';
  memset(key, 'k', sizeof(key) - 1);
  key[sizeof(key) - 1] = '\0';
  snprintf(root, sizeof(root), CONFIG_SYSTEM_DESKTOP_QPK_DIR "/.storage-check-%ld", (long)getpid());
  /* Exclusive creation protects an existing directory, including leftovers
   * from a previous interrupted test. Never test in real application data.
   */
  if (mkdir(root, 0700) < 0)
    {
      ret = -errno;
      printf("desktop: storage selftest setup failed: %d\n", -ret);
      return ret;
    }
  buffer = malloc(QPK_STORAGE_VALUE_MAX + 1);
  if (buffer == NULL) ret = -ENOMEM;
  for (size_t i = 0; ret == 0 && i < 3; i++)
    {
      ret = qpk_storage_write(root, packages[i], keys[i], "first", 5);
      if (ret == 0) ret = qpk_storage_write(root, packages[i], keys[i], "second", 6);
      if (ret == 0) ret = qpk_storage_read(root, packages[i], keys[i], buffer,
                                         QPK_STORAGE_VALUE_MAX + 1, &length);
      if (ret == 0 && (length != 6 || memcmp(buffer, "second", 6) != 0)) ret = -EIO;
      if (ret == 0) ret = qpk_storage_remove(root, packages[i], keys[i]);
      if (ret == 0 && qpk_storage_read(root, packages[i], keys[i], buffer,
                                      QPK_STORAGE_VALUE_MAX + 1, &length) != -ENOENT) ret = -EIO;
    }
  free(buffer);
  for (size_t i = 0; i < 3; i++)
    {
      int cleanup = qpk_storage_remove(root, packages[i], keys[i]);
      if (cleanup < 0 && ret == 0) ret = cleanup;
      if (qpk_storage_path(root, packages[i], keys[i], path, sizeof(path)) == 0)
        {
          char *end;
          while ((end = strrchr(path, '/')) != NULL && (size_t)(end - path) > strlen(root))
            {
              *end = '\0';
              if (rmdir(path) < 0 && errno != ENOENT && errno != ENOTEMPTY && ret == 0)
                ret = -errno;
            }
        }
    }
  if (rmdir(root) < 0 && ret == 0) ret = -errno;
  printf("desktop: storage selftest %s (ret=%d, 3 namespaces, overwrite/read/delete)\n",
         ret == 0 ? "PASS" : "FAIL", ret);
  return ret;
}
