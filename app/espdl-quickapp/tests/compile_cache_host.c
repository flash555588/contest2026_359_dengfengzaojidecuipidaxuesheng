/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "qpk_compile_cache.h"

static unsigned long elapsed_us(struct timespec start)
{
  struct timespec end;
  clock_gettime(CLOCK_MONOTONIC, &end);
  return (end.tv_sec - start.tv_sec) * 1000000L +
         (end.tv_nsec - start.tv_nsec) / 1000L;
}

static void run(const char *source, bool eligible, bool expect_hit, int expected)
{
  JSRuntime *runtime = JS_NewRuntime();
  JSContext *context = JS_NewContext(runtime);
  bool hit;
  JSValue compiled = qpk_compile_cached(context, source, strlen(source),
                                        "builtin:/homeassistant/app.js",
                                        eligible, &hit);
  assert(!JS_IsException(compiled) && hit == expect_hit);
  JSValue result = JS_EvalFunction(context, compiled);
  int32_t number;
  assert(!JS_IsException(result));
  assert(JS_ToInt32(context, &number, result) == 0 && number == expected);
  JS_FreeValue(context, result);
  JS_FreeContext(context);
  JS_FreeRuntime(runtime);
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  const char *source = "globalThis.counter = (globalThis.counter || 0) + 1; counter";
  run(source, true, false, 1);
  size_t size = g_qpk_compile_cache_size;
  assert(size > 0 && size <= QPK_COMPILE_CACHE_LIMIT);
  /* A completely fresh context must never retain app globals or listeners. */
  for (unsigned i = 0; i < 30; i++) run(source, true, true, 1);
  /* Denied origin/digest must compile supplied text, never replay trusted code. */
  run("42", false, false, 42);
  run(source, true, true, 1);
  /* Invalid cache version is discarded and source compilation recovers. */
  g_qpk_compile_cache[0] ^= 0xff;
  run(source, true, false, 1);
  qpk_compile_cache_clear();
  assert(!g_qpk_compile_cache && !g_qpk_compile_cache_size);
  FILE *file = fopen(argv[1], "rb");
  assert(file);
  assert(fseek(file, 0, SEEK_END) == 0);
  long length = ftell(file);
  assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
  char *app = malloc(length + 1);
  assert(app && fread(app, 1, length, file) == (size_t)length);
  app[length] = 0;
  fclose(file);
  unsigned long timings[2];
  for (unsigned i = 0; i < 2; i++)
    {
      JSRuntime *runtime = JS_NewRuntime();
      JSContext *context = JS_NewContext(runtime);
      bool hit;
      struct timespec start;
      clock_gettime(CLOCK_MONOTONIC, &start);
      JSValue compiled = qpk_compile_cached(context, app, length,
                        "builtin:/homeassistant/app.js", true, &hit);
      timings[i] = elapsed_us(start);
      assert(!JS_IsException(compiled) && hit == (i == 1));
      JS_FreeValue(context, compiled);
      JS_FreeContext(context);
      JS_FreeRuntime(runtime);
    }
  printf("COMPILE_CACHE_OK bytes=%zu source_bytes=%ld cold_us=%lu warm_us=%lu\n",
         g_qpk_compile_cache_size, length, timings[0], timings[1]);
  qpk_compile_cache_clear();
  free(app);
  JSRuntime *runtime = JS_NewRuntime();
  JSContext *context = JS_NewContext(runtime);
  bool hit;
  JSValue invalid = qpk_compile_cached(context, "function {", 10, "bad.js",
                                       true, &hit);
  assert(JS_IsException(invalid) && !hit && !g_qpk_compile_cache);
  qpk_compile_cache_exception_clear(context);
  JSValue compiled = qpk_compile_cached(context, "17", 2, "valid.js", true, &hit);
  assert(!JS_IsException(compiled));
  JS_FreeValue(context, compiled);
  compiled = qpk_compile_cached(context, "17", 2, "valid.js", true, &hit);
  assert(!JS_IsException(compiled) && hit);
  /* Cache storage can be freed without invalidating deserialized functions. */
  qpk_compile_cache_clear();
  JSValue value = JS_EvalFunction(context, compiled);
  int32_t number;
  assert(!JS_IsException(value) && JS_ToInt32(context, &number, value) == 0 && number == 17);
  JS_FreeValue(context, value);
  JS_FreeContext(context);
  JS_FreeRuntime(runtime);
  runtime = JS_NewRuntime();
  context = JS_NewContext(runtime);
  size_t large_size = QPK_COMPILE_CACHE_LIMIT + 4096;
  char *large = malloc(large_size + 3);
  assert(large);
  large[0] = '\'';
  memset(large + 1, 'a', large_size);
  large[large_size + 1] = '\'';
  large[large_size + 2] = 0;
  compiled = qpk_compile_cached(context, large, large_size + 2, "large.js", true, &hit);
  assert(!JS_IsException(compiled) && !hit && !g_qpk_compile_cache);
  JS_FreeValue(context, compiled);
  free(large);
  JS_FreeContext(context);
  JS_FreeRuntime(runtime);
  return 0;
}
