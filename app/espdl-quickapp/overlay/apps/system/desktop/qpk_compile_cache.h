/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <quickjs.h>

/* UI-thread only. This is process-local bytecode produced by this exact
 * engine, never a filesystem or application-provided bytecode input.
 * Eligibility MUST mean both built-in origin AND the exact reviewed source
 * digest. A fresh context and all authorization checks remain mandatory.
 * Retain no JS values, contexts, listeners, credentials or application state.
 */
#define QPK_COMPILE_CACHE_LIMIT (512 * 1024)
static unsigned char *g_qpk_compile_cache;
static size_t g_qpk_compile_cache_size;

static void qpk_compile_cache_clear(void)
{
  free(g_qpk_compile_cache);
  g_qpk_compile_cache = NULL;
  g_qpk_compile_cache_size = 0;
}

static void qpk_compile_cache_exception_clear(JSContext *context)
{
  JSValue exception = JS_GetException(context);
  JS_FreeValue(context, exception);
}

static JSValue qpk_compile_cached(JSContext *context, const char *source,
                                 size_t length, const char *filename,
                                 bool eligible, bool *hit)
{
  *hit = false;
  if (eligible && g_qpk_compile_cache != NULL)
    {
      /* Copy bytecode into the fresh runtime; do not use ROM_DATA, whose
       * functions would retain pointers into our evictable cache buffer. */
      JSValue value = JS_ReadObject(context, g_qpk_compile_cache,
                                    g_qpk_compile_cache_size,
                                    JS_READ_OBJ_BYTECODE);
      if (!JS_IsException(value))
        {
          *hit = true;
          return value;
        }
      qpk_compile_cache_exception_clear(context);
      qpk_compile_cache_clear();
    }

  JSValue value = JS_Eval(context, source, length, filename,
                          JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  if (eligible && !JS_IsException(value))
    {
      size_t size = 0;
      unsigned char *serialized = JS_WriteObject(context, &size, value,
                                                  JS_WRITE_OBJ_BYTECODE);
      if (serialized != NULL)
        {
          if (size > 0 && size <= QPK_COMPILE_CACHE_LIMIT)
            {
              unsigned char *copy = malloc(size);
              if (copy != NULL)
                {
                  memcpy(copy, serialized, size);
                  qpk_compile_cache_clear();
                  g_qpk_compile_cache = copy;
                  g_qpk_compile_cache_size = size;
                }
            }
          js_free(context, serialized);
        }
      else
        {
          /* Optional caching must not turn a successful compile into an
           * application failure when the engine cannot serialize it. */
          qpk_compile_cache_exception_clear(context);
        }
    }
  return value;
}
