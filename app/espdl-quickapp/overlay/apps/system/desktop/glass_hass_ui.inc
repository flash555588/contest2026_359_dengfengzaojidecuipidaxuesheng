/* SPDX-License-Identifier: Apache-2.0 */

#define HASS_UI_MAX_DEVICES 48
#define HASS_UI_VISIBLE 6

enum hass_ui_mode_e
{
  HASS_UI_ALL = 0,
  HASS_UI_COMMON,
  HASS_UI_ENVIRONMENT
};

enum hass_ui_request_e
{
  HASS_UI_REQUEST_NONE = 0,
  HASS_UI_REQUEST_STATES,
  HASS_UI_REQUEST_CONTROL
};

struct hass_ui_device_s
{
  char entity[96];
  char name[72];
  char state[40];
  char domain[20];
  char unit[20];
  bool controllable;
  bool environment;
  bool available;
  bool on;
  int brightness;
};

static struct
{
  lv_obj_t *root;
  lv_obj_t *list;
  lv_obj_t *heading;
  lv_obj_t *subtitle;
  lv_obj_t *top_status;
  lv_obj_t *count;
  lv_obj_t *nav[3];
  lv_obj_t *hero_state;
  lv_obj_t *hero_detail;
  lv_obj_t *total_value;
  lv_obj_t *online_value;
  lv_obj_t *running_value;
  lv_obj_t *sync_label;
  lv_obj_t *bottom_name;
  lv_obj_t *bottom_state;
  lv_obj_t *bottom_action;
  lv_obj_t *bottom_action_label;
  lv_obj_t *brightness_group;
  lv_obj_t *brightness_label;
  lv_obj_t *brightness;
  lv_timer_t *timer;
  struct hass_ui_device_s devices[HASS_UI_MAX_DEVICES];
  int visible[HASS_UI_VISIBLE];
  uint32_t client;
  uint32_t next_refresh;
  unsigned count_all;
  unsigned count_online;
  unsigned count_running;
  unsigned stored;
  enum hass_ui_mode_e mode;
  enum hass_ui_request_e request;
  int selected;
  int pending_device;
  int pending_brightness;
  bool pending_on;
  bool configured;
  bool busy;
  bool loaded;
  bool cached;
  char url[128];
  char error[80];
  uint32_t primary;
  uint32_t secondary;
  uint32_t background;
  uint32_t surface;
  uint32_t sidebar;
  uint32_t accent;
  uint32_t warm;
} g_hass_ui;

static void hass_ui_render_devices(void);
static void hass_ui_render_summary(void);
static void hass_ui_refresh_request(bool notify);
static void hass_ui_refresh_clicked(lv_event_t *event);
static void hass_ui_settings(lv_event_t *event);

static void hass_ui_copy(char *out, size_t capacity, const char *text)
{
  if (!capacity) return;
  if (!text) text = "";
  size_t length = strnlen(text, capacity - 1);
  if (text[length] != '\0')
    while (length && (((unsigned char)text[length] & 0xc0) == 0x80)) length--;
  memcpy(out, text, length);
  out[length] = '\0';
}

static lv_obj_t *hass_ui_text(lv_obj_t *parent, const char *text, int x,
                              int y, int width, int size, uint32_t color)
{
  return glass_text(parent, text, x, y, width, size, color);
}

static lv_obj_t *hass_ui_button(lv_obj_t *parent, const char *text, int x,
                                int y, int width, int height, uint32_t color,
                                lv_event_cb_t callback, void *data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(button, height > 40 ? 12 : height / 2, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_80, LV_STATE_PRESSED);
  if (callback) glass_clickable(button, callback, data);
  lv_obj_t *label = hass_ui_text(button, text, 0, 0, width - 16, 16,
                                 g_hass_ui.primary);
  if (!strcmp(text, LV_SYMBOL_REFRESH))
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  return button;
}

static bool hass_ui_domain(const char *entity, char *domain, size_t capacity)
{
  const char *dot = entity ? strchr(entity, '.') : NULL;
  size_t length = dot ? (size_t)(dot - entity) : 0;
  if (!length || length >= capacity) return false;
  memcpy(domain, entity, length);
  domain[length] = '\0';
  return true;
}

static bool hass_ui_control_domain(const char *domain)
{
  return !strcmp(domain, "light") || !strcmp(domain, "switch") ||
         !strcmp(domain, "input_boolean") || !strcmp(domain, "fan");
}

static bool hass_ui_environment_domain(const char *domain)
{
  return !strcmp(domain, "sensor") || !strcmp(domain, "binary_sensor") ||
         !strcmp(domain, "climate");
}

static bool hass_ui_display_domain(const char *domain)
{
  return hass_ui_control_domain(domain) || hass_ui_environment_domain(domain) ||
         !strcmp(domain, "cover") || !strcmp(domain, "lock") ||
         !strcmp(domain, "media_player") || !strcmp(domain, "vacuum");
}

static const char *hass_ui_symbol(const struct hass_ui_device_s *device)
{
  if (!strcmp(device->domain, "light")) return LV_SYMBOL_CHARGE;
  if (!strcmp(device->domain, "fan")) return LV_SYMBOL_LOOP;
  if (!strcmp(device->domain, "switch") ||
      !strcmp(device->domain, "input_boolean")) return LV_SYMBOL_POWER;
  if (!strcmp(device->domain, "media_player")) return LV_SYMBOL_AUDIO;
  if (device->environment) return LV_SYMBOL_EYE_OPEN;
  return LV_SYMBOL_HOME;
}

static const char *hass_ui_state_text(const struct hass_ui_device_s *device,
                                      char *buffer, size_t capacity)
{
  if (!device->available) return "不可用";
  if (device->controllable)
    {
      if (!device->on) return "已关闭";
      if (!strcmp(device->domain, "light") && device->brightness > 0)
        {
          snprintf(buffer, capacity, "已开启 · %d%%", device->brightness);
          return buffer;
        }
      return !strcmp(device->domain, "fan") ? "运行中" : "已开启";
    }
  if (device->unit[0])
    snprintf(buffer, capacity, "%s %s", device->state, device->unit);
  else if (!strcmp(device->domain, "binary_sensor"))
    snprintf(buffer, capacity, "%s", device->on ? "检测到" : "正常");
  else
    snprintf(buffer, capacity, "%s", device->state);
  return buffer;
}

static bool hass_ui_accept(const struct hass_ui_device_s *device)
{
  if (g_hass_ui.mode == HASS_UI_COMMON) return device->controllable;
  if (g_hass_ui.mode == HASS_UI_ENVIRONMENT) return device->environment;
  return true;
}

static int hass_ui_first_visible(void)
{
  for (unsigned i = 0; i < HASS_UI_VISIBLE; i++)
    if (g_hass_ui.visible[i] >= 0) return g_hass_ui.visible[i];
  return -1;
}

static void hass_ui_collect_visible(void)
{
  for (unsigned i = 0; i < HASS_UI_VISIBLE; i++) g_hass_ui.visible[i] = -1;
  unsigned position = 0;
  for (unsigned pass = 0; pass < (g_hass_ui.mode == HASS_UI_ALL ? 2u : 1u); pass++)
    {
      for (unsigned i = 0; i < g_hass_ui.stored && position < HASS_UI_VISIBLE; i++)
        {
          struct hass_ui_device_s *device = &g_hass_ui.devices[i];
          if (!hass_ui_accept(device)) continue;
          if (g_hass_ui.mode == HASS_UI_ALL &&
              device->controllable != (pass == 0)) continue;
          g_hass_ui.visible[position++] = (int)i;
        }
    }
  bool selected_visible = false;
  for (unsigned i = 0; i < HASS_UI_VISIBLE; i++)
    if (g_hass_ui.visible[i] == g_hass_ui.selected) selected_visible = true;
  if (!selected_visible) g_hass_ui.selected = hass_ui_first_visible();
}

static void hass_ui_update_selected(void)
{
  struct hass_ui_device_s *device = NULL;
  if (g_hass_ui.selected >= 0 &&
      (unsigned)g_hass_ui.selected < g_hass_ui.stored)
    device = &g_hass_ui.devices[g_hass_ui.selected];
  char state[80];
  lv_label_set_text(g_hass_ui.bottom_name, device ? device->name : "选择一个设备");
  lv_label_set_text(g_hass_ui.bottom_state,
                    device ? hass_ui_state_text(device, state, sizeof(state)) :
                    "点按设备卡片查看并控制");
  bool controllable = device && device->controllable && device->available;
  lv_label_set_text(g_hass_ui.bottom_action_label,
                    controllable ? (device->on ? "关闭" : "开启") : "不可控制");
  lv_obj_set_style_bg_color(g_hass_ui.bottom_action,
                            lv_color_hex(controllable ? g_hass_ui.accent :
                                         g_hass_ui.sidebar), 0);
  lv_obj_set_style_text_color(g_hass_ui.bottom_action_label,
                              lv_color_hex(controllable ? 0xffffff :
                                           g_hass_ui.secondary), 0);
  if (controllable) lv_obj_remove_state(g_hass_ui.bottom_action, LV_STATE_DISABLED);
  else lv_obj_add_state(g_hass_ui.bottom_action, LV_STATE_DISABLED);
  bool dimmable = device && device->available &&
                  !strcmp(device->domain, "light");
  if (dimmable)
    {
      lv_obj_remove_flag(g_hass_ui.brightness_group, LV_OBJ_FLAG_HIDDEN);
      int value = device->brightness > 0 ? device->brightness : 100;
      lv_slider_set_value(g_hass_ui.brightness, value, LV_ANIM_OFF);
      lv_label_set_text_fmt(g_hass_ui.brightness_label, "亮度 %d%%", value);
    }
  else
    {
      lv_obj_add_flag(g_hass_ui.brightness_group, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hass_ui_device_select(lv_event_t *event)
{
  int index = (int)(uintptr_t)lv_event_get_user_data(event) - 1;
  if (index < 0 || (unsigned)index >= g_hass_ui.stored) return;
  g_hass_ui.selected = index;
  hass_ui_render_devices();
}

static void hass_ui_start_control(int index, bool on, int brightness)
{
  if (!g_hass_ui.client || index < 0 ||
      (unsigned)index >= g_hass_ui.stored) return;
  struct hass_ui_device_s *device = &g_hass_ui.devices[index];
  if (!device->controllable || !device->available) return;
  if (g_hass_ui.request != HASS_UI_REQUEST_NONE || g_hass_ui.busy)
    {
      show_toast("家庭服务正在同步，请稍候");
      return;
    }
  int ret = hass_control(g_hass_ui.client, device->entity, on, brightness);
  if (ret < 0)
    {
      show_toast(ret == -EBUSY ? "家庭服务正忙，请稍候" :
                 ret == -ENOTCONN ? "请先连接 Home Assistant" :
                 "设备控制未发送，请重试");
      return;
    }
  g_hass_ui.request = HASS_UI_REQUEST_CONTROL;
  g_hass_ui.pending_device = index;
  g_hass_ui.pending_on = on;
  g_hass_ui.pending_brightness = brightness;
  g_hass_ui.busy = true;
  lv_label_set_text(g_hass_ui.top_status, "正在执行");
  hass_ui_render_summary();
}

static void hass_ui_device_toggle(lv_event_t *event)
{
  lv_event_stop_bubbling(event);
  int index = (int)(uintptr_t)lv_event_get_user_data(event) - 1;
  if (index < 0 || (unsigned)index >= g_hass_ui.stored) return;
  g_hass_ui.selected = index;
  hass_ui_start_control(index, !g_hass_ui.devices[index].on, -1);
}

static void hass_ui_bottom_toggle(lv_event_t *event)
{
  LV_UNUSED(event);
  int index = g_hass_ui.selected;
  if (index < 0 || (unsigned)index >= g_hass_ui.stored) return;
  hass_ui_start_control(index, !g_hass_ui.devices[index].on, -1);
}

static void hass_ui_brightness_changed(lv_event_t *event)
{
  int value = lv_slider_get_value(lv_event_get_target(event));
  lv_label_set_text_fmt(g_hass_ui.brightness_label, "亮度 %d%%", value);
}

static void hass_ui_brightness_commit(lv_event_t *event)
{
  int value = lv_slider_get_value(lv_event_get_target(event));
  hass_ui_start_control(g_hass_ui.selected, true, value);
}

static void hass_ui_toggle(lv_obj_t *parent, const struct hass_ui_device_s *device,
                           int index)
{
  uint32_t track_color = device->on ? g_hass_ui.accent :
                         (g_desktop.light_theme ? 0xc8cdd3 : 0x59616a);
  lv_obj_t *track = glass_box(parent, 170, 69, 46, 24, track_color, 12);
  lv_obj_set_style_bg_opa(track, device->available ? LV_OPA_COVER : LV_OPA_40, 0);
  lv_obj_t *knob = glass_box(track, device->on ? 24 : 3, 3, 18, 18,
                             0xffffff, 9);
  lv_obj_remove_flag(knob, LV_OBJ_FLAG_CLICKABLE);
  if (device->available)
    {
      glass_clickable(track, hass_ui_device_toggle,
                      (void *)(uintptr_t)(index + 1));
      lv_obj_remove_flag(track, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

static void hass_ui_render_empty(void)
{
  const bool unconfigured = !g_hass_ui.configured;
  const bool waiting = g_hass_ui.configured && !g_hass_ui.loaded &&
                       !g_hass_ui.error[0];
  lv_obj_t *panel = glass_box(g_hass_ui.list, 0, 36, 488, 292,
                              g_hass_ui.surface, 20);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(g_hass_ui.sidebar), 0);
  lv_obj_set_style_border_opa(panel, 110, 0);
  lv_obj_t *mark = glass_box(panel, 24, 24, 64, 64,
                             unconfigured ? g_hass_ui.warm : g_hass_ui.accent,
                             18);
  lv_obj_t *icon = glass_symbol(mark, unconfigured ? LV_SYMBOL_WIFI :
                               LV_SYMBOL_REFRESH, 0, 0, 0xffffff);
  lv_obj_center(icon);
  hass_ui_text(panel, unconfigured ? "连接你的家" : waiting ?
               "正在同步家庭设备" :
               g_hass_ui.error[0] ? "暂时没有同步成功" : "还没有可显示的设备",
               108, 25, 350, 28, g_hass_ui.primary);
  hass_ui_text(panel, unconfigured ?
               "用手机扫码填写局域网地址与长期访问令牌，设备会自动出现在这里。" :
               waiting ? "正在从家庭中枢读取状态，设备卡片稍后会出现在这里。" :
               g_hass_ui.error[0] ? g_hass_ui.error :
               "支持灯、开关、风扇、传感器、空调等 Home Assistant 实体。",
               108, 72, 344, 16, g_hass_ui.secondary);
  if (unconfigured)
    {
      hass_ui_text(panel, "01  同一 Wi-Fi", 28, 134, 132, 16,
                   g_hass_ui.secondary);
      hass_ui_text(panel, "02  扫码配置", 174, 134, 132, 16,
                   g_hass_ui.secondary);
      hass_ui_text(panel, "03  自动同步", 320, 134, 132, 16,
                   g_hass_ui.secondary);
      lv_obj_t *button = hass_ui_button(panel, "打开连接设置", 28, 202, 194, 48,
                                        g_hass_ui.accent, hass_ui_settings, NULL);
      lv_obj_set_style_text_color(lv_obj_get_child(button, 0),
                                  lv_color_hex(0xffffff), 0);
    }
  else if (!waiting)
    {
      lv_obj_t *button = hass_ui_button(panel, "重新同步", 28, 202, 154, 48,
                                        g_hass_ui.accent,
                                        hass_ui_refresh_clicked, NULL);
      lv_obj_set_style_text_color(lv_obj_get_child(button, 0),
                                  lv_color_hex(0xffffff), 0);
    }
}

static void hass_ui_render_devices(void)
{
  if (!g_hass_ui.list) return;
  lv_obj_clean(g_hass_ui.list);
  hass_ui_collect_visible();
  for (unsigned i = 0; i < 3; i++)
    {
      bool selected = (unsigned)g_hass_ui.mode == i;
      lv_obj_set_style_bg_color(g_hass_ui.nav[i],
                                lv_color_hex(selected ? g_hass_ui.surface :
                                             g_hass_ui.sidebar), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(g_hass_ui.nav[i], 0),
                                  lv_color_hex(selected ? g_hass_ui.accent :
                                               g_hass_ui.secondary), 0);
    }
  const char *headings[] = {"全屋设备", "常用控制", "环境感知"};
  const char *subtitles[] = {"设备状态，一眼看清", "灯光、开关与风扇", "温度、湿度与传感器"};
  lv_label_set_text(g_hass_ui.heading, headings[g_hass_ui.mode]);
  lv_label_set_text(g_hass_ui.subtitle, subtitles[g_hass_ui.mode]);
  if (!g_hass_ui.loaded || hass_ui_first_visible() < 0)
    {
      hass_ui_render_empty();
      hass_ui_update_selected();
      return;
    }
  for (unsigned slot = 0; slot < HASS_UI_VISIBLE; slot++)
    {
      int index = g_hass_ui.visible[slot];
      if (index < 0) continue;
      struct hass_ui_device_s *device = &g_hass_ui.devices[index];
      int x = (slot % 2) * 246;
      int y = (slot / 2) * 118;
      uint32_t color = index == g_hass_ui.selected ?
        (g_desktop.light_theme ? 0xe7f5ef : 0x183d35) : g_hass_ui.surface;
      lv_obj_t *card = glass_box(g_hass_ui.list, x, y, 232, 106, color, 16);
      lv_obj_set_style_border_width(card, index == g_hass_ui.selected ? 2 : 1, 0);
      lv_obj_set_style_border_color(card,
        lv_color_hex(index == g_hass_ui.selected ? g_hass_ui.accent :
                     g_hass_ui.sidebar), 0);
      lv_obj_set_style_border_opa(card, index == g_hass_ui.selected ?
                                  LV_OPA_COVER : 100, 0);
      glass_clickable(card, hass_ui_device_select,
                      (void *)(uintptr_t)(index + 1));
      uint32_t icon_color = device->available ?
        (device->on ? g_hass_ui.accent : g_hass_ui.secondary) :
        g_hass_ui.secondary;
      lv_obj_t *icon_box = glass_box(card, 14, 14, 42, 42,
                                     device->on ? g_hass_ui.accent :
                                     g_hass_ui.sidebar, 13);
      lv_obj_set_style_bg_opa(icon_box, device->on ? LV_OPA_COVER : 150, 0);
      lv_obj_t *icon = glass_symbol(icon_box, hass_ui_symbol(device), 0, 0,
                                    device->on ? 0xffffff : icon_color);
      lv_obj_center(icon);
      hass_ui_text(card, device->name, 68, 14, 146, 20, g_hass_ui.primary);
      char state[80];
      hass_ui_text(card, hass_ui_state_text(device, state, sizeof(state)),
                   68, 48, device->controllable ? 94 : 146, 16,
                   device->available ? g_hass_ui.secondary : g_hass_ui.warm);
      if (device->controllable) hass_ui_toggle(card, device, index);
      else
        {
          lv_obj_t *dot = glass_box(card, 18, 78, 7, 7,
                                    device->available ? g_hass_ui.accent :
                                    g_hass_ui.warm, 4);
          lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
          hass_ui_text(card, device->environment ? "环境" : "设备", 32, 70,
                       70, 16, g_hass_ui.secondary);
        }
    }
  hass_ui_update_selected();
}

static void hass_ui_render_summary(void)
{
  if (!g_hass_ui.root) return;
  const char *status = !g_hass_ui.configured ? "等待连接" :
                       g_hass_ui.busy ? "正在同步" :
                       g_hass_ui.error[0] ? "连接异常" :
                       g_hass_ui.loaded ? "家庭在线" : "准备连接";
  lv_label_set_text(g_hass_ui.hero_state, status);
  lv_label_set_text(g_hass_ui.hero_detail,
                    !g_hass_ui.configured ? "尚未配置 Home Assistant" :
                    g_hass_ui.url[0] ? g_hass_ui.url : "局域网安全连接");
  lv_label_set_text_fmt(g_hass_ui.total_value, "%u", g_hass_ui.count_all);
  lv_label_set_text_fmt(g_hass_ui.online_value, "%u", g_hass_ui.count_online);
  lv_label_set_text_fmt(g_hass_ui.running_value, "%u", g_hass_ui.count_running);
  lv_label_set_text(g_hass_ui.sync_label,
                    !g_hass_ui.configured ? "扫码连接后自动同步" :
                    g_hass_ui.busy ? "正在读取最新状态" :
                    g_hass_ui.error[0] ? g_hass_ui.error :
                    g_hass_ui.cached ? "已读取本地状态缓存" :
                    g_hass_ui.loaded ? "状态刚刚更新" : "等待首次同步");
  lv_label_set_text_fmt(g_hass_ui.count, "%u 台设备 · %u 在线",
                        g_hass_ui.count_all, g_hass_ui.count_online);
  if (!g_hass_ui.busy)
    lv_label_set_text(g_hass_ui.top_status,
                      g_hass_ui.error[0] ? "需要检查" :
                      g_hass_ui.loaded ? "已同步" :
                      g_hass_ui.configured ? "待同步" : "未连接");
}

static void hass_ui_set_error(int status, int error)
{
  if (status == 401 || status == 403)
    hass_ui_copy(g_hass_ui.error, sizeof(g_hass_ui.error),
                 "访问令牌已失效，请重新配置");
  else if (status && (status < 200 || status >= 300))
    snprintf(g_hass_ui.error, sizeof(g_hass_ui.error),
             "Home Assistant 返回状态 %d", status);
  else if (error)
    hass_ui_copy(g_hass_ui.error, sizeof(g_hass_ui.error),
                 "无法连接家庭中枢，请检查网络");
  else
    g_hass_ui.error[0] = '\0';
}

static bool hass_ui_parse(const char *body)
{
  cJSON *root = cJSON_ParseWithOpts(body, NULL, true);
  if (!cJSON_IsArray(root))
    {
      cJSON_Delete(root);
      hass_ui_copy(g_hass_ui.error, sizeof(g_hass_ui.error),
                   "设备数据格式异常，请重新同步");
      return false;
    }
  char selected_entity[96] = "";
  if (g_hass_ui.selected >= 0 &&
      (unsigned)g_hass_ui.selected < g_hass_ui.stored)
    hass_ui_copy(selected_entity, sizeof(selected_entity),
                 g_hass_ui.devices[g_hass_ui.selected].entity);
  g_hass_ui.stored = 0;
  g_hass_ui.count_all = 0;
  g_hass_ui.count_online = 0;
  g_hass_ui.count_running = 0;
  g_hass_ui.selected = -1;
  cJSON *item;
  cJSON_ArrayForEach(item, root)
    {
      cJSON *entity = cJSON_GetObjectItemCaseSensitive(item, "entity_id");
      cJSON *state = cJSON_GetObjectItemCaseSensitive(item, "state");
      cJSON *attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
      if (!cJSON_IsString(entity) || !cJSON_IsString(state) ||
          !cJSON_IsObject(attributes)) continue;
      char domain[20];
      if (!hass_ui_domain(entity->valuestring, domain, sizeof(domain)) ||
          !hass_ui_display_domain(domain)) continue;
      g_hass_ui.count_all++;
      bool available = strcmp(state->valuestring, "unavailable") &&
                       strcmp(state->valuestring, "unknown");
      bool controllable = hass_ui_control_domain(domain);
      bool on = !strcmp(state->valuestring, "on") ||
                !strcmp(state->valuestring, "open") ||
                !strcmp(state->valuestring, "playing") ||
                !strcmp(state->valuestring, "heat") ||
                !strcmp(state->valuestring, "cool");
      if (available) g_hass_ui.count_online++;
      if (available && controllable && on) g_hass_ui.count_running++;
      if (g_hass_ui.stored >= HASS_UI_MAX_DEVICES) continue;
      struct hass_ui_device_s *device =
        &g_hass_ui.devices[g_hass_ui.stored];
      memset(device, 0, sizeof(*device));
      hass_ui_copy(device->entity, sizeof(device->entity), entity->valuestring);
      hass_ui_copy(device->domain, sizeof(device->domain), domain);
      hass_ui_copy(device->state, sizeof(device->state), state->valuestring);
      cJSON *friendly = cJSON_GetObjectItemCaseSensitive(attributes,
                                                          "friendly_name");
      if (cJSON_IsString(friendly) && friendly->valuestring[0])
        hass_ui_copy(device->name, sizeof(device->name), friendly->valuestring);
      else
        {
          const char *fallback = strchr(entity->valuestring, '.');
          hass_ui_copy(device->name, sizeof(device->name),
                       fallback ? fallback + 1 : entity->valuestring);
          for (char *p = device->name; *p; p++) if (*p == '_') *p = ' ';
        }
      cJSON *unit = cJSON_GetObjectItemCaseSensitive(attributes,
                                                      "unit_of_measurement");
      if (cJSON_IsString(unit))
        hass_ui_copy(device->unit, sizeof(device->unit), unit->valuestring);
      cJSON *brightness = cJSON_GetObjectItemCaseSensitive(attributes,
                                                            "brightness");
      device->brightness = cJSON_IsNumber(brightness) ?
        (brightness->valueint * 100 + 127) / 255 : -1;
      device->controllable = controllable;
      device->environment = hass_ui_environment_domain(domain);
      device->available = available;
      device->on = on;
      if (selected_entity[0] && !strcmp(selected_entity, device->entity))
        g_hass_ui.selected = (int)g_hass_ui.stored;
      g_hass_ui.stored++;
    }
  cJSON_Delete(root);
  g_glass.available = (int)g_hass_ui.count_online;
  g_glass.total = (int)g_hass_ui.count_all;
  g_hass_ui.loaded = true;
  g_hass_ui.error[0] = '\0';
  return true;
}

static void hass_ui_refresh_request(bool notify)
{
  if (!g_hass_ui.client || !g_hass_ui.configured) return;
  if (g_hass_ui.request != HASS_UI_REQUEST_NONE || g_hass_ui.busy)
    {
      if (notify) show_toast("家庭服务正在同步，请稍候");
      return;
    }
  int ret = hass_get(g_hass_ui.client, "states");
  if (ret < 0)
    {
      if (ret == -EBUSY)
        {
          g_hass_ui.next_refresh = lv_tick_get() + 750;
          if (notify) show_toast("家庭服务正忙，稍后自动重试");
        }
      else
        {
          hass_ui_set_error(0, ret);
          hass_ui_render_summary();
          if (notify) show_toast("同步请求未发送，请检查连接");
        }
      return;
    }
  g_hass_ui.request = HASS_UI_REQUEST_STATES;
  g_hass_ui.busy = true;
  g_hass_ui.cached = false;
  g_hass_ui.error[0] = '\0';
  lv_label_set_text(g_hass_ui.top_status, "正在同步");
  hass_ui_render_summary();
}

static void hass_ui_refresh_clicked(lv_event_t *event)
{
  LV_UNUSED(event);
  hass_ui_refresh_request(true);
}

static void hass_ui_nav(lv_event_t *event)
{
  uintptr_t mode = (uintptr_t)lv_event_get_user_data(event);
  if (mode > HASS_UI_ENVIRONMENT) return;
  g_hass_ui.mode = (enum hass_ui_mode_e)mode;
  hass_ui_render_devices();
}

static void hass_ui_settings(lv_event_t *event)
{
  LV_UNUSED(event);
  glass_portal_show(glass_homeassistant);
}

static void hass_ui_poll(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_hass_ui.client || !g_hass_ui.root) return;
  bool configured = false;
  bool service_busy = false;
  hass_status(&configured, &service_busy, NULL);
  g_hass_ui.configured = configured;
  struct hass_result_s result;
  int ret = hass_poll(g_hass_ui.client, &result);
  if (ret < 0)
    {
      hass_ui_set_error(0, ret);
      g_hass_ui.busy = false;
      hass_ui_render_summary();
      return;
    }
  g_hass_ui.busy = result.busy ||
    (service_busy && g_hass_ui.request != HASS_UI_REQUEST_NONE);
  if (result.done)
    {
      enum hass_ui_request_e completed = g_hass_ui.request;
      g_hass_ui.request = HASS_UI_REQUEST_NONE;
      g_hass_ui.busy = false;
      bool success = result.error == 0 && result.status >= 200 &&
                     result.status < 300;
      if (!success)
        {
          hass_ui_set_error(result.status, result.error);
          if (completed == HASS_UI_REQUEST_CONTROL)
            show_toast(g_hass_ui.error[0] ? g_hass_ui.error : "设备控制失败");
        }
      else if (completed == HASS_UI_REQUEST_STATES)
        {
          g_hass_ui.cached = result.cached;
          if (result.body) hass_ui_parse(result.body);
        }
      else if (completed == HASS_UI_REQUEST_CONTROL)
        {
          int index = g_hass_ui.pending_device;
          if (index >= 0 && (unsigned)index < g_hass_ui.stored)
            {
              struct hass_ui_device_s *device = &g_hass_ui.devices[index];
              device->on = g_hass_ui.pending_on;
              if (g_hass_ui.pending_brightness > 0)
                device->brightness = g_hass_ui.pending_brightness;
            }
          g_hass_ui.error[0] = '\0';
          g_hass_ui.next_refresh = lv_tick_get() + 350;
        }
      hass_result_free(&result);
      hass_ui_render_devices();
      hass_ui_render_summary();
    }
  uint32_t now = lv_tick_get();
  if (g_hass_ui.configured && g_hass_ui.request == HASS_UI_REQUEST_NONE &&
      !service_busy && g_hass_ui.next_refresh &&
      (int32_t)(now - g_hass_ui.next_refresh) >= 0)
    {
      g_hass_ui.next_refresh = 0;
      hass_ui_refresh_request(false);
    }
}

static void hass_ui_deleted(lv_event_t *event)
{
  LV_UNUSED(event);
  if (g_hass_ui.timer) lv_timer_delete(g_hass_ui.timer);
  if (g_hass_ui.client) hass_close(g_hass_ui.client);
  memset(&g_hass_ui, 0, sizeof(g_hass_ui));
}

static void glass_hass_app(lv_obj_t *card)
{
  lv_obj_clean(card);
  memset(&g_hass_ui, 0, sizeof(g_hass_ui));
  g_hass_ui.root = card;
  g_hass_ui.selected = -1;
  g_hass_ui.pending_device = -1;
  bool light = g_desktop.light_theme;
  g_hass_ui.primary = light ? 0x17212b : 0xf3f7f5;
  g_hass_ui.secondary = light ? 0x65727d : 0xaab7b1;
  g_hass_ui.background = light ? 0xf2f5f4 : 0x111817;
  g_hass_ui.surface = light ? 0xffffff : 0x202a28;
  g_hass_ui.sidebar = light ? 0xe8eeeb : 0x18221f;
  g_hass_ui.accent = light ? 0x15956b : 0x43c996;
  g_hass_ui.warm = light ? 0xf06a3b : 0xff8a61;
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_size(card, 1024, 600);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(g_hass_ui.background), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(card, hass_ui_deleted, LV_EVENT_DELETE, NULL);

  lv_obj_t *sidebar = glass_box(card, 0, 0, 178, 500,
                                g_hass_ui.sidebar, 0);
  lv_obj_t *logo = glass_box(sidebar, 22, 24, 48, 48, g_hass_ui.warm, 14);
  lv_obj_t *home = glass_symbol(logo, LV_SYMBOL_HOME, 0, 0, 0xffffff);
  lv_obj_center(home);
  hass_ui_text(sidebar, "米家", 82, 24, 78, 28, g_hass_ui.primary);
  hass_ui_text(sidebar, "HOME", 84, 59, 72, 16, g_hass_ui.secondary);
  const char *nav_names[] = {"全屋", "常用", "环境"};
  for (unsigned i = 0; i < 3; i++)
    g_hass_ui.nav[i] = hass_ui_button(sidebar, nav_names[i], 12,
                                      116 + i * 60, 154, 48,
                                      i ? g_hass_ui.sidebar :
                                          g_hass_ui.surface,
                                      hass_ui_nav, (void *)(uintptr_t)i);
  g_hass_ui.count = hass_ui_text(sidebar, "0 台设备 · 0 在线", 24, 310,
                                 136, 16, g_hass_ui.secondary);
  hass_ui_button(sidebar, "返回桌面", 16, 434, 146, 44,
                 g_hass_ui.surface, panel_hide, NULL);

  g_hass_ui.heading = hass_ui_text(card, "全屋设备", 202, 24, 250, 28,
                                   g_hass_ui.primary);
  g_hass_ui.subtitle = hass_ui_text(card, "设备状态，一眼看清", 202, 64,
                                    300, 16, g_hass_ui.secondary);
  lv_obj_t *status_box = glass_box(card, 576, 28, 108, 34,
                                   g_hass_ui.sidebar, 17);
  g_hass_ui.top_status = hass_ui_text(status_box, "待同步", 8, 7, 92, 16,
                                      g_hass_ui.secondary);
  lv_obj_set_style_text_align(g_hass_ui.top_status, LV_TEXT_ALIGN_CENTER, 0);
  g_hass_ui.list = glass_box(card, 202, 106, 488, 376,
                             g_hass_ui.background, 0);

  lv_obj_t *hero = glass_box(card, 706, 24, 294, 458, 0x146b59, 22);
  glass_gradient(hero, light ? 0x0e3d3d : 0x082e32, LV_GRAD_DIR_VER);
  hass_ui_text(hero, "家庭中枢", 22, 18, 250, 16, 0xcce9df);
  lv_obj_t *hero_icon = glass_box(hero, 83, 62, 128, 128, 0xffffff, 64);
  lv_obj_set_style_bg_opa(hero_icon, 26, 0);
  lv_obj_set_style_border_width(hero_icon, 1, 0);
  lv_obj_set_style_border_color(hero_icon, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_opa(hero_icon, 70, 0);
  lv_obj_t *hero_symbol = glass_symbol(hero_icon, LV_SYMBOL_HOME, 0, 0,
                                       0xffffff);
  lv_obj_set_style_text_font(hero_symbol, &lv_font_montserrat_24, 0);
  lv_obj_center(hero_symbol);
  g_hass_ui.hero_state = hass_ui_text(hero, "准备连接", 22, 210, 250, 28,
                                      0xffffff);
  g_hass_ui.hero_detail = hass_ui_text(hero, "局域网安全连接", 22, 250,
                                       250, 16, 0xcce9df);
  lv_obj_t *rule = glass_box(hero, 22, 286, 250, 1, 0xffffff, 0);
  lv_obj_set_style_bg_opa(rule, 38, 0);
  hass_ui_text(hero, "设备", 24, 306, 60, 16, 0xcce9df);
  hass_ui_text(hero, "在线", 112, 306, 60, 16, 0xcce9df);
  hass_ui_text(hero, "运行", 202, 306, 60, 16, 0xcce9df);
  g_hass_ui.total_value = hass_ui_text(hero, "0", 24, 332, 60, 28,
                                       0xffffff);
  g_hass_ui.online_value = hass_ui_text(hero, "0", 112, 332, 60, 28,
                                        0xffffff);
  g_hass_ui.running_value = hass_ui_text(hero, "0", 202, 332, 60, 28,
                                         0xffffff);
  g_hass_ui.sync_label = hass_ui_text(hero, "等待首次同步", 22, 396, 250,
                                      16, 0xcce9df);

  lv_obj_t *bar = glass_box(card, 0, 500, 1024, 100,
                            light ? 0xffffff : 0x1a2422, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(g_hass_ui.sidebar), 0);
  g_hass_ui.bottom_name = hass_ui_text(bar, "选择一个设备", 24, 20,
                                       274, 20, g_hass_ui.primary);
  g_hass_ui.bottom_state = hass_ui_text(bar, "点按设备卡片查看并控制",
                                        24, 54, 274, 16,
                                        g_hass_ui.secondary);
  hass_ui_button(bar, LV_SYMBOL_REFRESH, 320, 24, 52, 52,
                 g_hass_ui.sidebar, hass_ui_refresh_clicked, NULL);
  hass_ui_button(bar, "连接设置", 388, 26, 126, 48,
                 g_hass_ui.sidebar, hass_ui_settings, NULL);
  g_hass_ui.bottom_action = hass_ui_button(bar, "不可控制", 530, 20,
                                            126, 60, g_hass_ui.sidebar,
                                            hass_ui_bottom_toggle, NULL);
  g_hass_ui.bottom_action_label = lv_obj_get_child(g_hass_ui.bottom_action, 0);
  g_hass_ui.brightness_group = glass_box(bar, 684, 12, 316, 76,
                                         light ? 0xffffff : 0x1a2422, 0);
  g_hass_ui.brightness_label = hass_ui_text(g_hass_ui.brightness_group,
                                             "亮度 100%", 0, 2, 120, 16,
                                             g_hass_ui.secondary);
  g_hass_ui.brightness = lv_slider_create(g_hass_ui.brightness_group);
  lv_obj_set_pos(g_hass_ui.brightness, 0, 42);
  lv_obj_set_size(g_hass_ui.brightness, 294, 8);
  lv_slider_set_range(g_hass_ui.brightness, 1, 100);
  lv_obj_set_style_bg_color(g_hass_ui.brightness,
                            lv_color_hex(g_hass_ui.sidebar), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_hass_ui.brightness,
                            lv_color_hex(g_hass_ui.accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_hass_ui.brightness,
                            lv_color_hex(g_hass_ui.accent), LV_PART_KNOB);
  lv_obj_add_event_cb(g_hass_ui.brightness, hass_ui_brightness_changed,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(g_hass_ui.brightness, hass_ui_brightness_commit,
                      LV_EVENT_RELEASED, NULL);
  lv_obj_add_flag(g_hass_ui.brightness_group, LV_OBJ_FLAG_HIDDEN);

  /* Reapply portal settings on entry so returning from QR configuration does
   * not require a reboot. panel_card() has already stopped the legacy QPK
   * client, so the configure handle can update the shared service safely. */
  int bootstrap = hass_portal_bootstrap();
  int client = hass_open(HASS_READ | HASS_CONTROL);
  if (client < 0)
    hass_ui_copy(g_hass_ui.error, sizeof(g_hass_ui.error),
                 "家庭服务暂时不可用");
  else
    g_hass_ui.client = (uint32_t)client;
  if (bootstrap < 0 && bootstrap != -EBUSY && !g_hass_ui.error[0])
    hass_ui_copy(g_hass_ui.error, sizeof(g_hass_ui.error),
                 "连接设置暂时无法载入");
  hass_status(&g_hass_ui.configured, &g_hass_ui.busy, NULL);
  if (g_hass_ui.client && g_hass_ui.configured)
    hass_get_url(g_hass_ui.client, g_hass_ui.url, sizeof(g_hass_ui.url));
  hass_ui_render_devices();
  hass_ui_render_summary();
  if (g_hass_ui.client && g_hass_ui.configured)
    {
      g_hass_ui.busy = false;
      hass_ui_refresh_request(false);
    }
  g_hass_ui.timer = lv_timer_create(hass_ui_poll, 250, NULL);
}

static void glass_homeassistant(lv_event_t *event)
{
  LV_UNUSED(event);
  lv_obj_t *card = panel_card("智能家居");
  if (!card) return;
  glass_hass_app(card);
  glass_homeassistant_enter(card);
}
