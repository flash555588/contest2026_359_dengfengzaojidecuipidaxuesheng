/* Host regression tests: enforce the existing SmartFS name limit at I/O. */
#define _GNU_SOURCE
#include "qpk_storage.h"
#include <assert.h>
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

FILE *__real_fopen(const char *, const char *);
int __real_mkdir(const char *, mode_t);
int __real_rename(const char *, const char *);
int __real_unlink(const char *);
static int rejected_names;
static int fail_rename;
static char root[] = "/tmp/qpk-test-XXXXXX";
static char output[QPK_STORAGE_VALUE_MAX + 1];

static int names_valid(const char *path)
{
  int components = 0;
  for (const char *p = path; *p;)
    {
      if (*p == '/') { p++; continue; }
      size_t n = strcspn(p, "/");
      /* /tmp/test corresponds to qpk/.data within the target mount. */
      if (n > 31 || ++components > 8)
        { rejected_names++; errno = ENAMETOOLONG; return 0; }
      p += n;
    }
  return 1;
}
FILE *__wrap_fopen(const char *p, const char *m)
{ return names_valid(p) ? __real_fopen(p, m) : NULL; }
int __wrap_mkdir(const char *p, mode_t m)
{ return names_valid(p) ? __real_mkdir(p, m) : -1; }
int __wrap_unlink(const char *p)
{ return names_valid(p) ? __real_unlink(p) : -1; }
int __wrap_rename(const char *a, const char *b)
{
  if (!names_valid(a) || !names_valid(b)) return -1;
  if (fail_rename) { errno = EIO; return -1; }
  return __real_rename(a, b);
}
static void expect_value(const char *package, const char *key,
                         const char *value, size_t expected)
{
  size_t length = 123;
  assert(qpk_storage_read(root, package, key, output, sizeof(output), &length) == 0);
  assert(length == expected && memcmp(value, output, length) == 0);
}
static void missing(const char *package, const char *key)
{
  size_t length;
  assert(qpk_storage_read(root, package, key, output, sizeof(output), &length) == -ENOENT);
}
static void legacy(const char *suffix, const char *value)
{
  char path[256];
  assert(snprintf(path, sizeof(path), "%s/%s", root, suffix) < (int)sizeof(path));
  FILE *file = fopen(path, "wb");
  assert(file);
  assert(fwrite(value, 1, strlen(value), file) == strlen(value));
  assert(fclose(file) == 0);
}
static int cleanup(const char *p, const struct stat *s, int type, struct FTW *f)
{
  (void)s; (void)f;
  assert(strncmp(p, root, strlen(root)) == 0);
  return type == FTW_DP ? rmdir(p) : unlink(p);
}
int main(void)
{
  char path[256], second[256], temporary[260];
  char package[48], key[65];
  char large[8193];
  size_t length;
  assert(mkdtemp(root));
  memset(package, 'p', 47); package[47] = 0;
  memset(key, 'k', 64); key[64] = 0;
  memset(large, 'v', sizeof(large));
  /* The pre-fix hello filename and its temp file cannot fit SmartFS. */
  snprintf(path, sizeof(path), "%s/636f6d2e6578616d706c652e68656c6c6f_launches.txt", root);
  assert(!fopen(path, "wb") && errno == ENAMETOOLONG);
  rejected_names = 0;
  const char *packages[] = {"com.example.hello", "com.example.game2048", package, ".", ".."};
  const char *keys[] = {"launches", "best", key, "x", "x"};
  for (size_t i = 0; i < 5; i++)
    {
      missing(packages[i], keys[i]);
      assert(qpk_storage_path(root, packages[i], keys[i], path, sizeof(path)) == 0);
      assert(names_valid(path));
      assert(qpk_storage_write(root, packages[i], keys[i], "one", 3) == 0);
      expect_value(packages[i], keys[i], "one", 3);
      assert(qpk_storage_write(root, packages[i], keys[i], "two", 3) == 0);
      expect_value(packages[i], keys[i], "two", 3);
      assert(qpk_storage_remove(root, packages[i], keys[i]) == 0);
      assert(qpk_storage_remove(root, packages[i], keys[i]) == 0);
      missing(packages[i], keys[i]);
    }
  assert(qpk_storage_path(root, "a", "bc", path, sizeof(path)) == 0);
  assert(qpk_storage_path(root, "ab", "c", second, sizeof(second)) == 0);
  assert(strcmp(path, second));
  assert(qpk_storage_path(root, "abc", "x", path, 8) == -ENAMETOOLONG);
  assert(qpk_storage_write(root, "empty", "x", "", 0) == 0);
  expect_value("empty", "x", "", 0);
  assert(qpk_storage_write(root, "limits", key, large, 8192) == 0);
  expect_value("limits", key, large, 8192);
  assert(qpk_storage_write(root, "limits", key, large, 8193) == -EINVAL);
  expect_value("limits", key, large, 8192);
  assert(qpk_storage_write(root, "binary", "x", "a\0b", 3) == 0);
  expect_value("binary", "x", "a\0b", 3);
  assert(qpk_storage_read(root, "empty", "x", output, 8192, &length) == -EINVAL);
  const char *bad[] = {"", "../x", "/x", "x.y", "x\\y", "x:y", "@2", "a b"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
      assert(qpk_storage_write(root, "good", bad[i], "v", 1) == -EINVAL);
      assert(qpk_storage_remove(root, "good", bad[i]) == -EINVAL);
    }
  assert(qpk_storage_write(root, "../x", "good", "v", 1) == -EINVAL);
  assert(qpk_storage_write(root, "@2", "good", "v", 1) == -EINVAL);
  assert(qpk_storage_write(root, "good", "good", NULL, 0) == -EINVAL);
  char too_long[66]; memset(too_long, 'x', 65); too_long[65] = 0;
  assert(qpk_storage_write(root, "good", too_long, "v", 1) == -EINVAL);
  assert(qpk_storage_write(root, too_long, "good", "v", 1) == -EINVAL);
  /* Both previous layouts: original directory, then flat-hex precedence. */
  snprintf(path, sizeof(path), "%s/ouo", root); assert(mkdir(path, 0700) == 0);
  legacy("ouo/emotion.txt", "original"); expect_value("ouo", "emotion", "original", 8);
  legacy("6f756f_emotion.txt", "flat"); expect_value("ouo", "emotion", "flat", 4);
  assert(qpk_storage_write(root, "ouo", "emotion", "current", 7) == 0);
  expect_value("ouo", "emotion", "current", 7);
  assert(qpk_storage_remove(root, "ouo", "emotion") == 0);
  missing("ouo", "emotion");
  snprintf(path, sizeof(path), "%s/com.example.hello", root); assert(mkdir(path, 0700) == 0);
  legacy("com.example.hello/launches.txt", "7"); expect_value("com.example.hello", "launches", "7", 1);
  assert(qpk_storage_remove(root, "com.example.hello", "launches") == 0);
  missing("com.example.hello", "launches");
  /* Failure before rename must retain the previous value and remove temp. */
  assert(qpk_storage_write(root, "error", "x", "old", 3) == 0);
  fail_rename = 1;
  assert(qpk_storage_write(root, "error", "x", "new", 3) == -EIO);
  fail_rename = 0; expect_value("error", "x", "old", 3);
  assert(qpk_storage_path(root, "error", "x", path, sizeof(path)) == 0);
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  assert(access(temporary, F_OK) == -1 && errno == ENOENT);
  FILE *f = fopen(path, "wb"); assert(f);
  assert(fwrite(large, 1, sizeof(large), f) == sizeof(large)); assert(fclose(f) == 0);
  assert(qpk_storage_read(root, "error", "x", output, sizeof(output), &length) == -EFBIG);
  assert(rejected_names == 0);
  assert(nftw(root, cleanup, 20, FTW_DEPTH | FTW_PHYS) == 0);
  puts("PASS: SmartFS 31-character names and 7 parent directories, namespace limits, values, legacy compatibility, overwrite/delete, validation, injected I/O failure");
  return 0;
}
