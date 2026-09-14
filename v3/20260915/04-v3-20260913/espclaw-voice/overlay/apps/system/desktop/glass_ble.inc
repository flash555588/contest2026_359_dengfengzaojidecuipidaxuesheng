/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_ble.h"
static lv_obj_t *g_ble_status;
static lv_obj_t *g_ble_list;
static lv_obj_t *g_ble_scan;
static lv_obj_t *g_ble_stop;
static lv_obj_t *g_ble_disconnect;
static lv_timer_t *g_ble_timer;
static struct glass_ble_state g_ble_view;
static bool g_ble_view_valid;

static void glass_ble_refresh(lv_timer_t *timer);

static void glass_ble_action(lv_event_t *event)
{
  uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
  int ret = action == 1 ? glass_ble_scan() : action == 2 ?
            glass_ble_cancel() : glass_ble_disconnect();
  glass_ble_refresh(NULL);
  if (ret)
    {
      char text[64];
      snprintf(text, sizeof(text), "请求失败 (%d)", ret);
      lv_label_set_text(g_ble_status, text);
    }
}

static void glass_ble_pick(lv_event_t *event)
{
  unsigned index = (uintptr_t)lv_event_get_user_data(event);
  int ret = glass_ble_connect(index);
  glass_ble_refresh(NULL);
  if (ret)
    {
      char text[64];
      snprintf(text, sizeof(text), "连接请求失败 (%d)", ret);
      lv_label_set_text(g_ble_status, text);
    }
}

static void glass_ble_refresh(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_ble_status) return;
  struct glass_ble_state state;
  glass_ble_read(&state);
  if (g_ble_view_valid && !memcmp(&state, &g_ble_view, sizeof(state))) return;
  bool devices_changed = !g_ble_view_valid || state.count != g_ble_view.count ||
    memcmp(state.devices, g_ble_view.devices, sizeof(state.devices));
  g_ble_view = state;
  g_ble_view_valid = true;
  char text[80];
  if (state.error) snprintf(text, sizeof(text), "蓝牙错误 (%d)", state.error);
  else snprintf(text, sizeof(text), "%s · %u 个设备",
                state.connected ? "已连接" : state.connecting ? "连接中" :
                state.scanning ? "扫描中" : state.ready ? "就绪" : "正在启动", state.count);
  lv_label_set_text(g_ble_status, text);
  bool idle = state.ready && !state.scanning && !state.connecting && !state.connected;
  if (idle) lv_obj_remove_state(g_ble_scan, LV_STATE_DISABLED);
  else lv_obj_add_state(g_ble_scan, LV_STATE_DISABLED);
  if (state.scanning) lv_obj_remove_state(g_ble_stop, LV_STATE_DISABLED);
  else lv_obj_add_state(g_ble_stop, LV_STATE_DISABLED);
  if (state.connected) lv_obj_remove_state(g_ble_disconnect, LV_STATE_DISABLED);
  else lv_obj_add_state(g_ble_disconnect, LV_STATE_DISABLED);
  if (devices_changed)
    {
      /* Preserve row objects and scroll position while RSSI/names update. */
      if (lv_obj_get_child_count(g_ble_list) > state.count)
        lv_obj_clean(g_ble_list);
      for (unsigned i = 0; i < state.count; i++)
        {
          const struct glass_ble_device *device = &state.devices[i];
          const uint8_t *a = device->address;
          const char *name = device->name[0] ? device->name : "未命名设备";
          lv_obj_t *row = lv_obj_get_child(g_ble_list, i);
          if (!row)
            {
              row = glass_button(g_ble_list, name, 4, i * 72, 816,
                                 theme_surface(), glass_ble_pick,
                                 (void *)(uintptr_t)i);
              lv_obj_set_height(row, 68);
              lv_obj_t *title = lv_obj_get_child(row, 0);
              lv_obj_set_style_text_font(title, zh_font(20), 0);
              lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
              lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 5);
              lv_obj_set_size(title, 788, 28);
              lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
              glass_text(row, "", 12, 38, 788, 16, theme_secondary());
            }
          lv_label_set_text(lv_obj_get_child(row, 0), name);
          snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X  %d dBm",
                   a[5], a[4], a[3], a[2], a[1], a[0], device->rssi);
          lv_label_set_text(lv_obj_get_child(row, 1), text);
        }
    }
  for (unsigned i = 0; i < lv_obj_get_child_count(g_ble_list); i++)
    {
      lv_obj_t *row = lv_obj_get_child(g_ble_list, i);
      if (idle) lv_obj_remove_state(row, LV_STATE_DISABLED);
      else lv_obj_add_state(row, LV_STATE_DISABLED);
    }
}

static void glass_ble_deleted(lv_event_t *event)
{
  LV_UNUSED(event);
  struct glass_ble_state state;
  glass_ble_read(&state);
  if (state.scanning) (void)glass_ble_cancel();
  if (g_ble_timer) lv_timer_delete(g_ble_timer);
  g_ble_timer = NULL;
  g_ble_status = g_ble_list = g_ble_scan = g_ble_stop = g_ble_disconnect = NULL;
  g_ble_view_valid = false;
}

static void glass_ble_page(lv_obj_t *card)
{
  lv_obj_add_event_cb(card, glass_ble_deleted, LV_EVENT_DELETE, NULL);
  g_ble_status = glass_text(card, "正在启动", 24, 76, 450, 20, theme_primary());
  g_ble_scan = glass_button(card, "扫描", 480, 60, 110, theme_surface(), glass_ble_action, (void *)1);
  g_ble_stop = glass_button(card, "停止", 610, 60, 110, theme_surface(), glass_ble_action, (void *)2);
  g_ble_disconnect = glass_button(card, "断开", 740, 60, 110, theme_surface(), glass_ble_action, (void *)3);
  g_ble_list = glass_box(card, 20, 120, 850, 330, theme_surface(), 0);
  lv_obj_add_flag(g_ble_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_ble_list, LV_DIR_VER);
  g_ble_view_valid = false;
  g_ble_timer = lv_timer_create(glass_ble_refresh, 100, NULL);
  if (!g_ble_timer)
    {
      lv_label_set_text(g_ble_status, "状态刷新启动失败");
      lv_obj_add_state(g_ble_scan, LV_STATE_DISABLED);
      lv_obj_add_state(g_ble_stop, LV_STATE_DISABLED);
      lv_obj_add_state(g_ble_disconnect, LV_STATE_DISABLED);
      return;
    }
  (void)glass_ble_start();
  glass_ble_refresh(NULL);
}
