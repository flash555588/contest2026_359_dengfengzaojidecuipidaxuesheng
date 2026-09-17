/* SPDX-License-Identifier: Apache-2.0 */
#include "hass_qjs.h"
#include "hass_service.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct binding_s { uint32_t client; unsigned grants; };
static JSClassID g_class;

/* The desktop integration supplies a strong implementation backed by the
 * Portal configuration store. Host tests and other embedders may keep RAM-only
 * configuration by using this default.
 */
__attribute__((weak)) int hass_config_persist(const char *url,
                                              const char *token)
{
  (void)url;
  (void)token;
  return 0;
}

static void finalize(JSRuntime *runtime, JSValue value)
{
  (void)runtime;
  struct binding_s *binding = JS_GetOpaque(value, g_class);
  if (binding) { if (binding->client) hass_close(binding->client); free(binding); }
}

static int attach(struct binding_s *binding)
{
  if (binding->client) return 0;
  int id = hass_open(binding->grants);
  if (id < 0) return id;
  binding->client = (uint32_t)id;
  return 0;
}

static const char *string_arg(JSContext *context, JSValueConst value)
{
  size_t size;
  if (!JS_IsString(value)) return NULL;
  const char *text = JS_ToCStringLen(context, &size, value);
  if (text && strlen(text) != size) { JS_FreeCString(context, text); return NULL; }
  return text;
}

static JSValue invoke(JSContext *context, JSValueConst self, int argc,
                      JSValueConst *argv, int method, JSValue *data)
{
  (void)self;
  struct binding_s *binding = JS_GetOpaque(data[0], g_class);
  if (!binding) return JS_ThrowInternalError(context, "HA session unavailable");
  if (method == 5)
    {
      if (binding->client) hass_close(binding->client);
      binding->client = 0;
      return JS_UNDEFINED;
    }
  int ret = attach(binding);
  if (ret < 0) return JS_ThrowInternalError(context, "HA client capacity unavailable");
  const char *a = NULL, *b = NULL;
  if (method <= 3 && argc > 0) a = string_arg(context, argv[0]);
  if (method == 0 && argc > 1) b = string_arg(context, argv[1]);
  ret = -EINVAL;
  if (method == 0 && a && b && argc == 3 && JS_IsBool(argv[2]))
    {
      ret = hass_configure(binding->client, a, b,
                           JS_ToBool(context, argv[2]));
      if (ret == 0)
        {
          ret = hass_config_persist(a, b);
          if (ret < 0)
            {
              hass_clear_configuration(binding->client);
            }
        }
    }
  else if (method == 1 && a && argc == 1) ret = hass_get(binding->client, a);
  else if (method == 2 && a && argc == 1) ret = hass_get_state(binding->client, a);
  else if (method == 3 && a && argc == 3 && JS_IsBool(argv[1]) && JS_IsNumber(argv[2]))
    {
      double level;
      if (JS_ToFloat64(context, &level, argv[2]) == 0 && level >= -1 && level <= 100 && level == (int)level)
        ret = hass_control(binding->client, a, JS_ToBool(context, argv[1]), (int)level);
    }
  JS_FreeCString(context, a); JS_FreeCString(context, b);
  if (method <= 3) return JS_NewInt32(context, ret);
  JSValue object = JS_NewObject(context);
  if (method == 4)
    {
      struct hass_result_s result;
      ret = hass_poll(binding->client, &result);
      JS_SetPropertyStr(context, object, "requestId", JS_NewUint32(context, result.request_id));
      JS_SetPropertyStr(context, object, "busy", JS_NewBool(context, result.busy));
      JS_SetPropertyStr(context, object, "done", JS_NewBool(context, result.done));
      JS_SetPropertyStr(context, object, "cached", JS_NewBool(context, result.cached));
      JS_SetPropertyStr(context, object, "status", JS_NewInt32(context, result.status));
      JS_SetPropertyStr(context, object, "error", JS_NewInt32(context, ret < 0 ? ret : result.error));
      JS_SetPropertyStr(context, object, "body", JS_NewString(context, result.body ? result.body : ""));
      hass_result_free(&result);
    }
  else
    {
      bool configured, busy, cached = false;
      uint32_t cache_age = 0;
      char url[128];
      hass_status(&configured, &busy, NULL);
      if (hass_get_url(binding->client, url, sizeof(url)) < 0) url[0] = 0;
      hass_cache_info(binding->client, &cached, &cache_age);
      JS_SetPropertyStr(context, object, "configured", JS_NewBool(context, configured));
      JS_SetPropertyStr(context, object, "busy", JS_NewBool(context, busy));
      JS_SetPropertyStr(context, object, "url", JS_NewString(context, url));
      JS_SetPropertyStr(context, object, "canControl", JS_NewBool(context, binding->grants & HASS_CONTROL));
      JS_SetPropertyStr(context, object, "canConfigure", JS_NewBool(context, binding->grants & HASS_CONFIGURE));
      JS_SetPropertyStr(context, object, "statesCached", JS_NewBool(context, cached));
      JS_SetPropertyStr(context, object, "cacheAgeMs", JS_NewUint32(context, cache_age));
    }
  return object;
}

int hass_qjs_install(JSContext *context, JSValue system, unsigned grants)
{
  JSRuntime *runtime = JS_GetRuntime(context);
  if (!g_class) JS_NewClassID(&g_class);
  JSClassDef definition = { .class_name = "HAServiceSession", .finalizer = finalize };
  if (!JS_IsRegisteredClass(runtime, g_class) && JS_NewClass(runtime, g_class, &definition) < 0) return -ENOMEM;
  struct binding_s *binding = calloc(1, sizeof(*binding));
  if (!binding) return -ENOMEM;
  binding->grants = grants;
  int ret = attach(binding);
  if (ret < 0) { free(binding); return ret; }
  JSValue holder = JS_NewObjectClass(context, g_class);
  if (JS_IsException(holder)) { hass_close(binding->client); free(binding); return -ENOMEM; }
  JS_SetOpaque(holder, binding);
  JSValue api = JS_NewObject(context);
  const char *names[] = {"configure", "get", "getState", "control", "poll", "close", "status"};
  const int lengths[] = {3, 1, 1, 3, 0, 0, 0};
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    JS_SetPropertyStr(context, api, names[i], JS_NewCFunctionData(context, invoke, lengths[i], i, 1, &holder));
  JS_SetPropertyStr(context, api, "apiVersion", JS_NewInt32(context, 1));
  JS_FreeValue(context, holder);
  return JS_SetPropertyStr(context, system, "homeAssistantService", api) < 0 ? -ENOMEM : 0;
}
