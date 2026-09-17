/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <quickjs.h>
#include <string.h>

/* JS_IsError checks the native class, excluding Proxy objects. Descriptor
 * access on this class does not invoke accessors; accept string data only.
 * The caller owns a non-NULL result via JS_FreeCString().
 */
static inline const char *qpk_exception_text(JSContext *context,
                                            JSValueConst exception,
                                            const char *property)
{
  if (JS_IsString(exception))
    return strcmp(property, "message") == 0 ? JS_ToCString(context, exception) : NULL;
  if (!JS_IsError(context, exception)) return NULL;
  JSAtom atom = JS_NewAtom(context, property);
  if (atom == JS_ATOM_NULL) return NULL;
  JSPropertyDescriptor desc = { .flags = 0, .value = JS_UNDEFINED,
                               .getter = JS_UNDEFINED, .setter = JS_UNDEFINED };
  int present = JS_GetOwnProperty(context, &desc, exception, atom);
  JS_FreeAtom(context, atom);
  const char *text = NULL;
  if (present > 0)
    {
      if (!(desc.flags & JS_PROP_GETSET) && JS_IsString(desc.value))
        text = JS_ToCString(context, desc.value);
      JS_FreeValue(context, desc.value);
      JS_FreeValue(context, desc.getter);
      JS_FreeValue(context, desc.setter);
    }
  return text;
}
