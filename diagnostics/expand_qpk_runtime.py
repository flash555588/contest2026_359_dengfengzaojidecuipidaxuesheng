"""One-time migration of the fixed runtime pools to dynamic resources."""
from pathlib import Path
ws = Path(__file__).resolve().parent.parent
p = ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop/qpk_runtime.c'
s = p.read_text(encoding='utf-8')
assert '#define QPK_MAX_WIDGETS   64' in s
s = s.replace('#define QPK_MEMORY_LIMIT  (2 * 1024 * 1024)\n#define QPK_HA_MEMORY_LIMIT (8 * 1024 * 1024)\n', '')
s = s.replace('#define QPK_MAX_WIDGETS   64\n/* The bundled Home Assistant screen retains 24 buttons, including hidden\n * settings controls. Keep a bounded pool with room for its complete UI. */\n#define QPK_MAX_EVENTS    32\n#define QPK_MAX_TIMERS    8\n', '')
s = s.replace('#include "qpk_storage.h"', '#include "qpk_storage.h"\n#include "qpk_limits.h"')
s = s.replace('struct qpk_event_s\n{', 'struct qpk_event_s\n{\n  struct qpk_event_s *next;')
s = s.replace('struct qpk_timer_s\n{', 'struct qpk_timer_s\n{\n  struct qpk_timer_s *next;')
s = s.replace('lv_obj_t *widgets[QPK_MAX_WIDGETS];\n  uint8_t widget_types[QPK_MAX_WIDGETS];\n  struct qpk_event_s events[QPK_MAX_EVENTS];',
'''lv_obj_t **widgets;
  uint8_t *widget_types;
  int widget_capacity;
  int widget_hint;
  struct qpk_event_s *events;''')
s = s.replace('struct qpk_timer_s timers[QPK_MAX_TIMERS];', 'struct qpk_timer_s *timers;')
s = s.replace('QPK_MAX_WIDGETS', 'g_qpk.widget_capacity')
s = s.replace('g_qpk.widget_types[handle - 1] = 0;', 'g_qpk.widget_types[handle - 1] = 0;\n      if ((int)handle - 1 < g_qpk.widget_hint) g_qpk.widget_hint = handle - 1;')
start = s.index('static int qpk_add_widget(')
end = s.index('\nstatic int qpk_arg_int', start)
s = s[:start]+'''static int qpk_add_widget(lv_obj_t *object, enum qpk_widget_type_e type)
{
  int i = g_qpk.widget_hint;
  while (i < g_qpk.widget_capacity && g_qpk.widgets[i]) i++;
  if (i == g_qpk.widget_capacity) {
    if (i > INT_MAX / 2 || (size_t)i > SIZE_MAX / (2 * sizeof(*g_qpk.widgets))) return 0;
    int capacity = i ? i * 2 : 32;
    lv_obj_t **widgets = calloc(capacity, sizeof(*widgets));
    uint8_t *types = calloc(capacity, sizeof(*types));
    if (!widgets || !types) { free(widgets); free(types); return 0; }
    if (i) {
      memcpy(widgets, g_qpk.widgets, i * sizeof(*widgets));
      memcpy(types, g_qpk.widget_types, i * sizeof(*types));
    }
    free(g_qpk.widgets); free(g_qpk.widget_types);
    g_qpk.widgets = widgets; g_qpk.widget_types = types; g_qpk.widget_capacity = capacity;
  }
  if (!lv_obj_add_event_cb(object, qpk_widget_deleted, LV_EVENT_DELETE, (void *)(uintptr_t)(i + 1))) return 0;
  g_qpk.widgets[i] = object; g_qpk.widget_types[i] = type; g_qpk.widget_hint = i + 1;
  return i + 1;
}
''' + s[end:]
start=s.index('  for (i = 0; i < QPK_MAX_EVENTS; i++)',s.index('static JSValue js_ui_button('))
end=s.index('\n  button = lv_button_create',start)
s=s[:start]+'''  /* Each callback owns a stable allocation: adding more buttons must not
   * invalidate LVGL's user_data pointers. Released entries are reused. */
  for (binding = g_qpk.events; binding && binding->used; binding = binding->next) {}
  if (!binding) {
    binding = calloc(1, sizeof(*binding));
    if (!binding) { JS_FreeCString(context, text); return JS_ThrowOutOfMemory(context); }
    binding->next = g_qpk.events; g_qpk.events = binding;
  }
''' +s[end:]
start=s.index('  for (i = 0; i < QPK_MAX_TIMERS; i++)',s.index('static JSValue js_set_interval('))
end=s.index('\n  if (g_qpk.next_timer_id',start)
s=s[:start]+'''  for (binding = g_qpk.timers; binding && binding->used; binding = binding->next) {}
  if (!binding) {
    binding = calloc(1, sizeof(*binding));
    if (!binding) return JS_ThrowOutOfMemory(context);
    binding->next = g_qpk.timers; g_qpk.timers = binding;
  }
''' +s[end:]
s=s.replace('for (i = 0; i < QPK_MAX_TIMERS; i++)\n    {\n      struct qpk_timer_s *binding = &g_qpk.timers[i];',
            'for (struct qpk_timer_s *binding = g_qpk.timers; binding; binding = binding->next)\n    {')
start=s.index('      for (i = 0; i < QPK_MAX_EVENTS; i++)',s.index('void qpk_runtime_stop('))
end=s.index('\n      if (g_qpk.input_shade',start)
s=s[:start]+'''      for (struct qpk_event_s *binding = g_qpk.events; binding; binding = binding->next) {
        if (binding->used && binding->owner) {
          lv_obj_remove_event_cb(binding->owner, qpk_event_clicked);
          lv_obj_remove_event_cb(binding->owner, qpk_event_deleted);
        }
      }
''' +s[end:]
start=s.index('      for (i = 0; i < QPK_MAX_TIMERS; i++)',s.index('void qpk_runtime_stop('))
end=s.index('\n      if (g_qpk.swipe_event.used)',start)
s=s[:start]+'''      for (struct qpk_timer_s *binding = g_qpk.timers; binding; binding = binding->next)
        if (binding->used) qpk_timer_release(g_qpk.context, binding);
      for (struct qpk_event_s *binding = g_qpk.events; binding; binding = binding->next)
        if (binding->used) qpk_event_release(g_qpk.context, binding);
''' +s[end:]
s=s.replace('  memset(&g_qpk, 0, sizeof(g_qpk));\n}', '''  while (g_qpk.events) { struct qpk_event_s *next = g_qpk.events->next; free(g_qpk.events); g_qpk.events = next; }
  while (g_qpk.timers) { struct qpk_timer_s *next = g_qpk.timers->next; free(g_qpk.timers); g_qpk.timers = next; }
  free(g_qpk.widgets); free(g_qpk.widget_types);
  memset(&g_qpk, 0, sizeof(g_qpk));
}''')
s=s.replace('JS_SetMemoryLimit(g_qpk.runtime,\n                    package != NULL &&\n                    strcmp(package, "com.openvela.homeassistant") == 0 ?\n                    QPK_HA_MEMORY_LIMIT : QPK_MEMORY_LIMIT);', 'JS_SetMemoryLimit(g_qpk.runtime, qpk_js_memory_budget());')
s=s.replace('JS_ThrowInternalError(context, "too many widgets")', 'JS_ThrowOutOfMemory(context)')
for name in ('js_ui_button', 'js_set_interval', 'js_clear_interval'):
    start=s.index('static JSValue '+name+'('); end=s.index('\n}\n',start)
    s=s[:start]+s[start:end].replace('  int i;\n','')+s[end:]
assert 'QPK_MAX_' not in s and 'QPK_MEMORY_LIMIT' not in s
p.write_text(s,encoding='utf-8')
print('Runtime pools now grow on demand with stable callback addresses')
