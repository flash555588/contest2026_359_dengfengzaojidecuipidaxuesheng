/* Shared desktop UI. Only the LVGL thread owns these objects. */
#ifdef CONFIG_SYSTEM_C6_DESKTOP
#include "desktop_backend.h"
static struct c6_desktop_worker g_wifi_worker;
static bool g_wifi_started;
static lv_timer_t *g_wifi_timer;
static lv_obj_t *g_wifi_status;
static lv_obj_t *g_wifi_scan;
static lv_obj_t *g_wifi_check;
static lv_obj_t *g_wifi_disconnect;
static lv_obj_t *g_wifi_connect;
static lv_obj_t *g_wifi_ssid;
static lv_obj_t *g_wifi_password;
static lv_obj_t *g_wifi_keyboard;
static lv_obj_t *g_wifi_list;
static struct c6_desktop_result g_wifi_result;
static unsigned long g_wifi_shown;

enum glass_wifi_status_e
{
  GLASS_WIFI_IDLE = 1,
  GLASS_WIFI_SCANNING,
  GLASS_WIFI_CONNECTING,
  GLASS_WIFI_OPERATION_ERROR,
  GLASS_WIFI_CONNECTED,
  GLASS_WIFI_COUNT,
  GLASS_WIFI_REQUEST_ERROR,
  GLASS_WIFI_TIMER_ERROR,
  GLASS_WIFI_LINK,
  GLASS_WIFI_CHECKING,
  GLASS_WIFI_IPV4,
  GLASS_WIFI_DISCONNECTING,
  GLASS_WIFI_DISCONNECTED,
};

/* Eight bytes of display semantics; LONG_DOT's mutable text is not a cache. */
static struct
{
  int value;
  uint8_t status;
  bool busy;
} g_wifi_display;

static void glass_wifi_status_set(enum glass_wifi_status_e status, int value)
{
  if (g_wifi_display.status == status && g_wifi_display.value == value) return;
  const char *text;
  char buffer[48];
  switch (status)
    {
      case GLASS_WIFI_IDLE: text = "尚未扫描"; break;
      case GLASS_WIFI_SCANNING: text = "扫描中"; break;
      case GLASS_WIFI_CONNECTING: text = "正在提交连接请求"; break;
      case GLASS_WIFI_CONNECTED: text = "连接请求已接受，IP 状态未确认"; break;
      case GLASS_WIFI_TIMER_ERROR: text = "状态刷新启动失败"; break;
      case GLASS_WIFI_CHECKING: text = "正在读取链路状态"; break;
      case GLASS_WIFI_DISCONNECTING: text = "正在断开"; break;
      case GLASS_WIFI_DISCONNECTED: text = "断开请求成功，请查询状态"; break;
      case GLASS_WIFI_IPV4:
        snprintf(buffer, sizeof(buffer), "IPv4: %u.%u.%u.%u",
                 (unsigned int)((uint32_t)value >> 24),
                 (unsigned int)(((uint32_t)value >> 16) & 255),
                 (unsigned int)(((uint32_t)value >> 8) & 255),
                 (unsigned int)((uint32_t)value & 255));
        text = buffer;
        break;
      case GLASS_WIFI_LINK:
        text = value == 3 ? "链路就绪，IP 状态未确认" :
               value == 2 ? "已关联，等待链路就绪" :
               value == 1 ? "未连接热点" : "无线设备未初始化";
        break;
      case GLASS_WIFI_OPERATION_ERROR:
        snprintf(buffer, sizeof(buffer), "操作失败 (%d)", value);
        text = buffer;
        break;
      case GLASS_WIFI_REQUEST_ERROR:
        snprintf(buffer, sizeof(buffer), "请求失败 (%d)", value);
        text = buffer;
        break;
      case GLASS_WIFI_COUNT:
        snprintf(buffer, sizeof(buffer), "%u 个热点", (unsigned int)value);
        text = buffer;
        break;
      default:
        return;
    }
  lv_label_set_text(g_wifi_status, text);
  g_wifi_display.status = status;
  g_wifi_display.value = value;
}
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
static lv_obj_t *g_wifi_back;
static lv_obj_t *g_wifi_selected;
#endif

static size_t glass_wifi_name_length(const struct c6_scan_ap *ap)
{
  size_t length = ap->ssid_length;
  if (length > 32) return 0;
  while (length && ap->ssid[length - 1] == 0) length--;
  return length;
}

static bool glass_wifi_utf8(const uint8_t *s, size_t length)
{
  for (size_t i = 0; i < length; )
    {
      uint32_t value = s[i++];
      unsigned extra;
      uint32_t minimum;
      if (value < 0x80)
        {
          if (value < 32 || value == 127) return false;
          continue;
        }
      if (value >= 0xc2 && value <= 0xdf) { extra = 1; minimum = 0x80; value &= 31; }
      else if (value >= 0xe0 && value <= 0xef) { extra = 2; minimum = 0x800; value &= 15; }
      else if (value >= 0xf0 && value <= 0xf4) { extra = 3; minimum = 0x10000; value &= 7; }
      else return false;
      if (extra > length - i) return false;
      while (extra--)
        {
          if ((s[i] & 0xc0) != 0x80) return false;
          value = (value << 6) | (s[i++] & 63);
        }
      if (value < minimum || value > 0x10ffff ||
          (value >= 0xd800 && value <= 0xdfff)) return false;
    }
  return length > 0;
}

#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
static void glass_wifi_list_view(lv_event_t *e)
{
  LV_UNUSED(e);
  lv_textarea_set_text(g_wifi_password, "");
  lv_obj_remove_flag(g_wifi_list, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_wifi_scan, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_wifi_selected, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_wifi_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_wifi_password, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_wifi_connect, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}
#endif

static void glass_wifi_deleted(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_wifi_password) lv_textarea_set_text(g_wifi_password, "");
  if (g_wifi_timer) lv_timer_delete(g_wifi_timer);
  g_wifi_timer = NULL;
  memset(&g_wifi_display, 0, sizeof(g_wifi_display));
  g_wifi_status = g_wifi_scan = g_wifi_connect = NULL;
  g_wifi_check = NULL;
  g_wifi_disconnect = NULL;
  g_wifi_ssid = g_wifi_password = g_wifi_keyboard = g_wifi_list = NULL;
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  g_wifi_back = g_wifi_selected = NULL;
#endif
}

static void glass_wifi_focus(lv_event_t *e)
{
  lv_keyboard_set_textarea(g_wifi_keyboard, lv_event_get_target(e));
}

static void glass_wifi_pick(lv_event_t *e)
{
  size_t index = (uintptr_t)lv_event_get_user_data(e);
  struct c6_desktop_result current;
  if (c6_desktop_worker_read(&g_wifi_worker, &current) < 0 || current.busy) return;
  if (index >= g_wifi_result.count) return;
  struct c6_scan_ap *ap = &g_wifi_result.records[index];
  size_t length = glass_wifi_name_length(ap);
  if (!glass_wifi_utf8(ap->ssid, length)) return;
  char selected[33];
  memcpy(selected, ap->ssid, length);
  selected[length] = 0;
  lv_textarea_set_text(g_wifi_ssid, selected);
  lv_textarea_set_text(g_wifi_password, "");
  lv_keyboard_set_textarea(g_wifi_keyboard, g_wifi_password);
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  lv_label_set_text(g_wifi_selected, selected);
  lv_obj_add_flag(g_wifi_list, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_wifi_scan, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_wifi_selected, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_wifi_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_wifi_password, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_wifi_connect, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
#endif
}

static void glass_wifi_render_list(void)
{
  lv_obj_clean(g_wifi_list);
  if (g_wifi_result.count == 0)
    glass_text(g_wifi_list, "点击扫描查找附近网络", 16, 20, 310, 16, theme_secondary());
  for (size_t i = 0; i < g_wifi_result.count; i++)
    {
      char name[33];
      const struct c6_scan_ap *ap = &g_wifi_result.records[i];
      size_t length = glass_wifi_name_length(ap);
      bool supported = glass_wifi_utf8(ap->ssid, length);
      if (supported) memcpy(name, ap->ssid, length);
      else memset(name, '?', length);
      name[length] = 0;
      char label[96];
      snprintf(label, sizeof(label), "%s  %ld dBm",
               name[0] ? name : "[hidden]", (long)ap->rssi);
      lv_obj_t *row = glass_button(g_wifi_list, label, 4, i * 52, 330,
                                   theme_surface(), glass_wifi_pick,
                                   (void *)(uintptr_t)i);
      if (!supported) lv_obj_add_state(row, LV_STATE_DISABLED);
      lv_obj_t *text = lv_obj_get_child(row, 0);
      lv_obj_set_width(text, 306);
      lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
      lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
      lv_obj_set_width(row, 820);
      lv_obj_set_width(text, 790);
#endif
    }
}

static void glass_wifi_update(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (g_wifi_status == NULL || g_wifi_list == NULL) return;
  struct c6_desktop_result result;
  if (c6_desktop_worker_read(&g_wifi_worker, &result) < 0) return;
  if (result.busy != g_wifi_display.busy)
    {
      lv_obj_t *controls[] = {g_wifi_scan, g_wifi_check, g_wifi_disconnect, g_wifi_connect, g_wifi_ssid,
                             g_wifi_password, g_wifi_keyboard};
      for (unsigned int i = 0; i < sizeof(controls) / sizeof(controls[0]); i++)
        {
          if (result.busy) lv_obj_add_state(controls[i], LV_STATE_DISABLED);
          else lv_obj_remove_state(controls[i], LV_STATE_DISABLED);
        }
      g_wifi_display.busy = result.busy;
    }
  if (result.busy)
    {
      glass_wifi_status_set(result.operation == C6_DESKTOP_SCAN ?
                            GLASS_WIFI_SCANNING :
                            result.operation == C6_DESKTOP_STATUS ?
                            GLASS_WIFI_CHECKING :
                            result.operation == C6_DESKTOP_DISCONNECT ?
                            GLASS_WIFI_DISCONNECTING : GLASS_WIFI_CONNECTING, 0);
      return;
    }
  if (!result.sequence || result.sequence == g_wifi_shown) return;
  g_wifi_shown = result.sequence;
  if (result.error)
    glass_wifi_status_set(GLASS_WIFI_OPERATION_ERROR, result.error);
  else if (result.operation == C6_DESKTOP_CONNECT)
    {
      glass_wifi_status_set(GLASS_WIFI_CONNECTED, 0);
      /* A manual join must also obtain a lease; the boot task only covers the
       * network that was already known when the board started. */
      glass_net_lease(true);
    }
  else if (result.operation == C6_DESKTOP_DISCONNECT)
    glass_wifi_status_set(GLASS_WIFI_DISCONNECTED, 0);
  else if (result.operation == C6_DESKTOP_STATUS)
    {
      if (result.link.ipv4_ready)
        {
          const uint8_t *ip = result.link.ipv4;
          uint32_t value = (uint32_t)ip[0] << 24 | (uint32_t)ip[1] << 16 |
                           (uint32_t)ip[2] << 8 | ip[3];
          glass_wifi_status_set(GLASS_WIFI_IPV4, (int)value);
        }
      else glass_wifi_status_set(GLASS_WIFI_LINK, result.link.carrier_ready ? 3 :
                          result.link.associated ? 2 :
                          result.link.initialized ? 1 : 0);
    }
  else
    {
      g_wifi_result = result;
      glass_wifi_render_list();
      glass_wifi_status_set(GLASS_WIFI_COUNT, (int)result.count);
    }
}

static void glass_wifi_request(lv_event_t *e)
{
  uintptr_t action = (uintptr_t)lv_event_get_user_data(e);
  bool connect = action == 1;
  int ret = c6_desktop_worker_submit(&g_wifi_worker,
      connect ? C6_DESKTOP_CONNECT : action == 2 ? C6_DESKTOP_STATUS :
      action == 3 ? C6_DESKTOP_DISCONNECT : C6_DESKTOP_SCAN,
      connect ? lv_textarea_get_text(g_wifi_ssid) : NULL,
      connect ? lv_textarea_get_text(g_wifi_password) : NULL);
  if (connect && ret == 0) lv_textarea_set_text(g_wifi_password, "");
  if (ret < 0) glass_wifi_status_set(GLASS_WIFI_REQUEST_ERROR, ret);
  else glass_wifi_update(NULL);
}
#endif

static void glass_wifi_page(void *unused)
{
  LV_UNUSED(unused);
  lv_obj_t *card = panel_card("Wi-Fi");
  if (!card) return;
#ifdef CONFIG_SYSTEM_C6_DESKTOP
  if (!g_wifi_started)
    {
      int ret = c6_desktop_worker_start(&g_wifi_worker, &g_c6_desktop_backend);
      if (ret < 0)
        {
          glass_text(card, "网络任务启动失败", 24, 92, 850, 28, theme_primary());
          return;
        }
      g_wifi_started = true;
    }
  lv_obj_add_event_cb(card, glass_wifi_deleted, LV_EVENT_DELETE, NULL);
  g_wifi_shown = 0;
  memset(&g_wifi_display, 0, sizeof(g_wifi_display));
  g_wifi_status = glass_text(card, "", 24, 68, 650, 20, theme_primary());
  glass_wifi_status_set(GLASS_WIFI_IDLE, 0);
  g_wifi_scan = glass_button(card, "扫描", 740, 60, 110, theme_surface(), glass_wifi_request, NULL);
  lv_obj_set_width(g_wifi_status, 440);
  g_wifi_disconnect = glass_button(card, "断开", 480, 60, 110, theme_surface(),
                                   glass_wifi_request, (void *)3);
  g_wifi_check = glass_button(card, "状态", 610, 60, 110, theme_surface(),
                              glass_wifi_request, (void *)2);
  g_wifi_list = glass_box(card, 24, 120, 344, 356, theme_surface(), 12);
  lv_obj_add_flag(g_wifi_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_wifi_list, LV_DIR_VER);
  g_wifi_ssid = lv_textarea_create(card);
  glass_input_style(g_wifi_ssid);
  lv_obj_set_pos(g_wifi_ssid, 390, 120);
  lv_obj_set_size(g_wifi_ssid, 488, 48);
  lv_textarea_set_one_line(g_wifi_ssid, true);
  lv_textarea_set_max_length(g_wifi_ssid, 32);
  lv_textarea_set_placeholder_text(g_wifi_ssid, "网络名称（SSID）");
  g_wifi_password = lv_textarea_create(card);
  glass_input_style(g_wifi_password);
  lv_obj_set_pos(g_wifi_password, 390, 182);
  lv_obj_set_size(g_wifi_password, 344, 48);
  lv_textarea_set_one_line(g_wifi_password, true);
  lv_textarea_set_max_length(g_wifi_password, 64);
  lv_textarea_set_password_mode(g_wifi_password, true);
  lv_textarea_set_placeholder_text(g_wifi_password, "密码");
  g_wifi_connect = glass_button(card, "连接", 750, 182, 128, theme_surface(), glass_wifi_request, (void *)1);
  g_wifi_keyboard = lv_keyboard_create(card);
  glass_keyboard_style(g_wifi_keyboard);
  lv_obj_set_size(g_wifi_keyboard, 488, 228);
  lv_obj_align(g_wifi_keyboard, LV_ALIGN_TOP_LEFT, 390, 248);
  lv_keyboard_set_textarea(g_wifi_keyboard, g_wifi_ssid);
  lv_obj_add_event_cb(g_wifi_ssid, glass_wifi_focus, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(g_wifi_password, glass_wifi_focus, LV_EVENT_FOCUSED, NULL);
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
  lv_obj_set_width(g_wifi_list, 850);
  lv_obj_set_scroll_dir(g_wifi_list, LV_DIR_VER);
  /* SSID remains an internal value; selection is shown with the Chinese font. */
  lv_obj_add_flag(g_wifi_ssid, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(g_wifi_password, 24, 176);
  lv_obj_set_size(g_wifi_password, 570, 46);
  lv_obj_set_pos(g_wifi_connect, 610, 176);
  g_wifi_back = glass_button(card, "返回", 750, 176, 100,
                              theme_surface(), glass_wifi_list_view, NULL);
  g_wifi_selected = glass_text(card, "", 24, 120, 820, 20, theme_primary());
  lv_obj_set_size(g_wifi_keyboard, 826, 210);
  lv_obj_align(g_wifi_keyboard, LV_ALIGN_TOP_LEFT, 24, 240);
  glass_wifi_list_view(NULL);
#endif
  glass_wifi_render_list();
  g_wifi_timer = lv_timer_create(glass_wifi_update, 100, NULL);
  if (!g_wifi_timer)
    {
      lv_obj_add_state(g_wifi_scan, LV_STATE_DISABLED);
      lv_obj_add_state(g_wifi_check, LV_STATE_DISABLED);
      lv_obj_add_state(g_wifi_disconnect, LV_STATE_DISABLED);
      lv_obj_add_state(g_wifi_connect, LV_STATE_DISABLED);
      glass_wifi_status_set(GLASS_WIFI_TIMER_ERROR, 0);
      return;
    }
  glass_wifi_update(NULL);
#else
  glass_text(card, "桌面网络管理未接入", 24, 92, 850, 28, theme_primary());
  glass_text(card, "连接状态：未知", 24, 150, 850, 20, theme_secondary());
#endif
}
