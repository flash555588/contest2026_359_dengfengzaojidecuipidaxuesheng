/* SPDX-License-Identifier: Apache-2.0 */
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  if (argc != 3) return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
  if (size < 0) return 2;
  char *source = malloc((size_t)size + 1);
  if (!source || fread(source, 1, size, f) != (size_t)size) return 2;
  fclose(f); source[size] = 0;
  JSRuntime *rt = JS_NewRuntime();
  if (!rt) return 2;
  JS_SetMemoryLimit(rt, strtoul(argv[2], NULL, 10) * 1024u * 1024u);
  JS_SetMaxStackSize(rt, 128u * 1024u);
  JSContext *ctx = JS_NewContext(rt);
  if (!ctx) return 2;
  JSValue v = JS_Eval(ctx, source, size, "kernel-benchmark", JS_EVAL_TYPE_GLOBAL);
  free(source);
  int rc = 0;
  if (JS_IsException(v)) {
    JSValue e = JS_GetException(ctx); const char *s = JS_ToCString(ctx, e);
    fprintf(stderr, "KERNEL_ERROR %s\n", s ? s : "allocation failure");
    JS_FreeCString(ctx, s); JS_FreeValue(ctx, e); rc = 1;
  } else {
    const char *s = JS_ToCString(ctx, v);
    printf("%s\n", s ? s : "null"); JS_FreeCString(ctx, s);
  }
  JS_FreeValue(ctx, v); JS_FreeContext(ctx); JS_FreeRuntime(rt);
  return rc;
}
