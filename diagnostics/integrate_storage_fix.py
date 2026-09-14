"""Apply the storage backend to the separate delivery overlay."""
from pathlib import Path

workspace = Path(__file__).resolve().parent.parent
target = workspace / '04-v3-20260913/app-storage-fix/overlay/apps/system/desktop'
original = workspace / 'diagnostics/storage-comparison'
runtime = (original / 'current-qpk_runtime.c').read_text(encoding='utf-8')
runtime = runtime.replace('#include "qpk_runtime.h"', '#include "qpk_runtime.h"\n#include "qpk_storage.h"')
for line in ['#define QPK_STORAGE_KEY_MAX 64\n', '#define QPK_STORAGE_VALUE_MAX 8192\n',
             '#define QPK_STORAGE_PATH_MAX 256\n']:
    assert line in runtime
    runtime = runtime.replace(line, '')
start = runtime.index('static bool qpk_storage_component_valid(')
end = runtime.index('static JSValue js_ui_button(', start)
glue = r'''/* Keys remain ASCII identifiers. Reject embedded NUL bytes before the
 * filesystem API sees a truncated key. Values keep their explicit length.
 */
static const char *qpk_storage_key(JSContext *context, int argc,
                                  JSValueConst *argv)
{
  size_t length;
  const char *key;
  if (argc < 1)
    {
      JS_ThrowTypeError(context, "storage requires a key");
      return NULL;
    }
  key = JS_ToCStringLen(context, &length, argv[0]);
  if (key != NULL && memchr(key, '\0', length) != NULL)
    {
      JS_FreeCString(context, key);
      JS_ThrowRangeError(context, "invalid storage key");
      return NULL;
    }
  return key;
}

static JSValue js_storage_get(JSContext *context, JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  const char *key = qpk_storage_key(context, argc, argv);
  char *buffer;
  size_t length;
  int ret;
  JSValue result;
  (void)this_value;
  if (key == NULL) return JS_EXCEPTION;
  buffer = malloc(QPK_STORAGE_VALUE_MAX + 1);
  if (buffer == NULL)
    {
      JS_FreeCString(context, key);
      return JS_ThrowOutOfMemory(context);
    }
  ret = qpk_storage_read(QPK_STORAGE_ROOT, g_qpk.package, key,
                         buffer, QPK_STORAGE_VALUE_MAX + 1, &length);
  JS_FreeCString(context, key);
  if (ret == 0) result = JS_NewStringLen(context, buffer, length);
  else if (ret == -ENOENT) result = JS_NULL;
  else if (ret == -EINVAL) result = JS_ThrowRangeError(context, "invalid storage key");
  else result = JS_ThrowInternalError(context, "storage read failed: %d", -ret);
  free(buffer);
  return result;
}

static JSValue js_storage_set(JSContext *context, JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  const char *key;
  const char *value;
  size_t length;
  int ret;
  (void)this_value;
  if (argc < 2) return JS_ThrowTypeError(context, "storage.set requires key and value");
  key = qpk_storage_key(context, argc, argv);
  if (key == NULL) return JS_EXCEPTION;
  value = JS_ToCStringLen(context, &length, argv[1]);
  if (value == NULL)
    {
      JS_FreeCString(context, key);
      return JS_EXCEPTION;
    }
  ret = qpk_storage_write(QPK_STORAGE_ROOT, g_qpk.package, key, value, length);
  JS_FreeCString(context, key);
  JS_FreeCString(context, value);
  if (ret == -EINVAL) return JS_ThrowRangeError(context, "invalid storage key or value");
  return ret < 0 ? JS_ThrowInternalError(context, "storage write failed: %d", -ret) : JS_UNDEFINED;
}

static JSValue js_storage_delete(JSContext *context, JSValueConst this_value,
                                 int argc, JSValueConst *argv)
{
  const char *key = qpk_storage_key(context, argc, argv);
  int ret;
  (void)this_value;
  if (key == NULL) return JS_EXCEPTION;
  ret = qpk_storage_remove(QPK_STORAGE_ROOT, g_qpk.package, key);
  JS_FreeCString(context, key);
  if (ret == -EINVAL) return JS_ThrowRangeError(context, "invalid storage key");
  return ret < 0 ? JS_ThrowInternalError(context, "storage delete failed: %d", -ret) : JS_UNDEFINED;
}

'''
# The raw Python literal deliberately contains C escapes, never shell text.
runtime = runtime[:start] + glue.replace("'\\\\0'", "'\\0'") + runtime[end:]
(target / 'qpk_runtime.c').write_text(runtime, encoding='utf-8')
main = (original / 'current-desktop_main.c').read_text(encoding='utf-8')
main = main.replace('#include "qpk_runtime.h"', '#include "qpk_runtime.h"\n#include "qpk_storage.h"')
marker = '  launch_camera = argc > 1 && strcmp(argv[1], "camera") == 0;'
assert marker in main
main = main.replace(marker, '''  if (argc > 1 && strcmp(argv[1], "storage-test") == 0)
    {
      return qpk_storage_selftest() < 0 ? 1 : 0;
    }

''' + marker)
(target / 'desktop_main.c').write_text(main, encoding='utf-8')
makefile = (original / 'current-Makefile').read_text()
makefile = makefile.replace('CSRCS += glass_file_service.c',
                            'CSRCS += glass_file_service.c qpk_storage.c qpk_storage_selftest.c')
(target / 'Makefile').write_text(makefile)
print('Integrated storage backend and isolated storage-test command')
