/* SPDX-License-Identifier: Apache-2.0
 * Standalone ESPHome page for openvela, using the desktop tiny TTF font.
 * Only esphome_client performs networking; LVGL consumes bounded snapshots.
 */

#include "esphome_lvgl.h"
#include "esphome_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAPER_BG     0xf3efe8
#define PAPER_CARD   0xfaf8f4
#define PAPER_INK    0x2a2724
#define PAPER_MUTED  0x8a847c
#define PAPER_EDGE   0xe4dac8
#define PAPER_ACCENT 0x497c68
#define PAPER_ERROR  0xa54535
#define POLL_MS      250

struct entity_row
{
  uint32_t key;
  uint32_t device_id;
  enum esphome_entity_kind kind;
  lv_obj_t *card;
  lv_obj_t *name;
  lv_obj_t *value;
  lv_obj_t *button;
  lv_obj_t *button_text;
  lv_obj_t *slider;
  lv_obj_t *brightness;
};

static struct
{
  lv_obj_t *root;
  lv_obj_t *form;
  lv_obj_t *tabs;
  lv_obj_t *connection_view;
  lv_obj_t *entity_view;
  lv_obj_t *address;
  lv_obj_t *port;
  lv_obj_t *expected_name;
  lv_obj_t *psk;
  lv_obj_t *plaintext;
  lv_obj_t *connect;
  lv_obj_t *disconnect;
  lv_obj_t *retry;
  lv_obj_t *keyboard;
  lv_obj_t *stage;
  lv_obj_t *device;
  lv_obj_t *error;
  lv_obj_t *notice;
  lv_obj_t *summary;
  lv_obj_t *list;
  lv_obj_t *empty;
  lv_timer_t *timer;
  const lv_font_t *font;
  esphome_lvgl_close_cb_t close_cb;
  struct entity_row rows[ESPHOME_MAX_ENTITIES];
  size_t row_count;
  size_t source_count;
  bool rows_failed;
  bool attempted;
  bool stopping;
  bool retry_pending;
} g_ui;

/* About 20 KiB: never put this object on the NuttX UI thread's stack. */
static struct esphome_snapshot g_snapshot;

/* No icon font is needed, including on the keyboard's item part. */
static const char * const g_keys_lower[] =
{
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
  "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
  "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
  "z", "x", "c", "v", "b", "n", "m", ".", "\n",
  "大写", "+", "/", "=", "-", "_", "删除", "清空", "完成", ""
};

static const char * const g_keys_upper[] =
{
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
  "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
  "Z", "X", "C", "V", "B", "N", "M", ".", "\n",
  "小写", "+", "/", "=", "-", "_", "删除", "清空", "完成", ""
};

static lv_buttonmatrix_ctrl_t g_key_ctrl[46];

static void refresh_page(void);
static void entity_event(lv_event_t *event);

static void view_changed(lv_event_t *event)
{
  LV_UNUSED(event);
  bool entities = lv_buttonmatrix_get_selected_button(g_ui.tabs) == 1;
  lv_obj_t *show = entities ? g_ui.entity_view : g_ui.connection_view;
  lv_obj_t *hide = entities ? g_ui.connection_view : g_ui.entity_view;
  lv_obj_remove_flag(show, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
  if (g_ui.keyboard != NULL)
    {
      lv_keyboard_set_textarea(g_ui.keyboard, NULL);
      lv_obj_add_flag(g_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void wipe(void *memory, size_t size)
{
  volatile unsigned char *p = memory;

  while (size-- > 0)
    {
      *p++ = 0;
    }
}

static void clear_psk(void)
{
  if (g_ui.psk != NULL)
    {
      /* Inspected LVGL 9.2: password get_text returns its owned pwd_tmp
       * allocation. Erase it BEFORE set_text frees/replaces that buffer.
       * The display label contains only '*', with show_time set to zero.
       */
      const char *text = lv_textarea_get_text(g_ui.psk);
      if (text != NULL)
        {
          wipe((void *)text, strlen(text));
        }

      lv_textarea_set_text(g_ui.psk, "");
    }
}

static void set_disabled(lv_obj_t *obj, bool disabled)
{
  if (disabled)
    {
      lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
  else
    {
      lv_obj_remove_state(obj, LV_STATE_DISABLED);
    }
}

static void set_text(lv_obj_t *label, const char *text)
{
  if (strcmp(lv_label_get_text(label), text) != 0)
    {
      lv_label_set_text(label, text);
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             uint32_t color)
{
  lv_obj_t *obj = lv_label_create(parent);

  if (obj != NULL)
    {
      lv_obj_set_width(obj, lv_pct(100));
      lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(obj, g_ui.font, 0);
      lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
      lv_label_set_text(obj, text);
    }

  return obj;
}

static void style_box(lv_obj_t *obj, uint32_t color, int padding)
{
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 8, 0);
  lv_obj_set_style_pad_all(obj, padding, 0);
  lv_obj_set_style_pad_row(obj, 10, 0);
  lv_obj_set_style_pad_column(obj, 12, 0);
  lv_obj_set_style_text_font(obj, g_ui.font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(PAPER_INK), 0);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(obj, LV_DIR_VER);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                              lv_event_cb_t cb, void *user)
{
  lv_obj_t *obj = lv_button_create(parent);
  lv_obj_t *label;

  if (obj == NULL)
    {
      return NULL;
    }

  style_box(obj, PAPER_EDGE, 8);
  lv_obj_set_size(obj, 94, 44);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_opa(obj, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  label = make_label(obj, text, PAPER_INK);
  if (label == NULL)
    {
      lv_obj_delete(obj);
      return NULL;
    }

  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user);
  return obj;
}

static void keyboard_hide(void)
{
  lv_obj_t *target = lv_keyboard_get_textarea(g_ui.keyboard);

  lv_keyboard_set_textarea(g_ui.keyboard, NULL);
  lv_obj_add_flag(g_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
  if (target != NULL)
    {
      lv_obj_remove_state(target, LV_STATE_FOCUSED);
    }
}

static void keyboard_event(lv_event_t *event)
{
  lv_obj_t *target = lv_keyboard_get_textarea(g_ui.keyboard);
  uint32_t index = lv_keyboard_get_selected_button(g_ui.keyboard);
  const char *key = lv_keyboard_get_button_text(g_ui.keyboard, index);

  LV_UNUSED(event);
  if (key == NULL || target == NULL)
    {
      return;
    }

  if (strcmp(key, "完成") == 0)
    {
      keyboard_hide();
    }
  else if (strcmp(key, "大写") == 0 || strcmp(key, "小写") == 0)
    {
      lv_keyboard_set_mode(g_ui.keyboard,
        strcmp(key, "大写") == 0 ? LV_KEYBOARD_MODE_USER_2 :
                                    LV_KEYBOARD_MODE_USER_1);
    }
  else if (strcmp(key, "删除") == 0)
    {
      lv_textarea_delete_char(target);
    }
  else if (strcmp(key, "清空") == 0)
    {
      if (target == g_ui.psk)
        {
          clear_psk();
        }
      else
        {
          lv_textarea_set_text(target, "");
        }
    }
  else
    {
      lv_textarea_add_text(target, key);
    }
}

static void field_clicked(lv_event_t *event)
{
  lv_obj_t *obj = lv_event_get_target(event);

  if (g_ui.keyboard == NULL || lv_obj_has_state(obj, LV_STATE_DISABLED))
    {
      return;
    }

  lv_keyboard_set_textarea(g_ui.keyboard, obj);
  lv_obj_remove_flag(g_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(g_ui.root);
  lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
}

static void root_deleted(lv_event_t *event)
{
  if (lv_event_get_target(event) != g_ui.root)
    {
      return;
    }

  /* LV_EVENT_DELETE runs before child deletion. Detach the keyboard and
   * erase the textarea while they are still alive, also on parent clean.
   * Never retain LVGL pointers in the background client.
   */
  if (g_ui.timer != NULL)
    {
      lv_timer_delete(g_ui.timer);
    }

  if (g_ui.keyboard != NULL)
    {
      lv_keyboard_set_textarea(g_ui.keyboard, NULL);
    }

  esphome_client_stop();
  clear_psk();
  memset(&g_ui, 0, sizeof(g_ui));
  wipe(&g_snapshot, sizeof(g_snapshot));
}

void esphome_lvgl_close(void)
{
  if (g_ui.root != NULL)
    {
      lv_obj_delete(g_ui.root);
    }
}

bool esphome_lvgl_is_open(void)
{
  return g_ui.root != NULL;
}

static void back_clicked(lv_event_t *event)
{
  esphome_lvgl_close_cb_t close_cb = g_ui.close_cb;

  LV_UNUSED(event);
  esphome_lvgl_close();
  if (close_cb != NULL)
    {
      close_cb();
    }
}

static bool online(void)
{
  return g_snapshot.running && g_snapshot.status == ESPHOME_READY &&
         !g_ui.stopping && !g_ui.retry_pending;
}

static size_t entity_count(void)
{
  return g_snapshot.count < ESPHOME_MAX_ENTITIES ?
         g_snapshot.count : ESPHOME_MAX_ENTITIES;
}

static const struct esphome_entity *find_entity(const struct entity_row *row)
{
  size_t i;

  for (i = 0; i < entity_count(); i++)
    {
      const struct esphome_entity *entity = &g_snapshot.entities[i];
      if (entity->key == row->key && entity->device_id == row->device_id &&
          entity->kind == row->kind)
        {
          return entity;
        }
    }

  return NULL;
}

static int brightness_percent(const struct esphome_entity *entity)
{
  if (!entity->has_state || !isfinite(entity->brightness))
    {
      return 0;
    }

  if (entity->brightness <= 0.0f)
    {
      return 0;
    }

  if (entity->brightness >= 1.0f)
    {
      return 100;
    }

  return (int)(entity->brightness * 100.0f + 0.5f);
}

static void update_row(struct entity_row *row,
                       const struct esphome_entity *entity, bool restore)
{
  char text[256];
  const char *kind;
  bool enabled = online() && entity != NULL && entity->has_state;

  if (entity == NULL)
    {
      set_text(row->value, "实体已移除");
      if (row->button != NULL)
        {
          set_disabled(row->button, true);
        }

      if (row->slider != NULL)
        {
          set_disabled(row->slider, true);
          lv_slider_set_value(row->slider, 0, LV_ANIM_OFF);
        }

      return;
    }

  switch (entity->kind)
    {
      case ESPHOME_SENSOR:        kind = "传感器"; break;
      case ESPHOME_BINARY_SENSOR: kind = "二值传感器"; break;
      case ESPHOME_TEXT_SENSOR:   kind = "文字传感器"; break;
      case ESPHOME_SWITCH:        kind = "开关"; break;
      case ESPHOME_LIGHT:         kind = "灯光"; break;
      default:                   kind = "未知实体"; break;
    }

  snprintf(text, sizeof(text), "%s · %.*s", kind,
           (int)sizeof(entity->name), entity->name[0] ? entity->name :
                                                       "未命名");
  set_text(row->name, text);
  if (!entity->has_state)
    {
      snprintf(text, sizeof(text), "状态未知 · 等待设备回报");
    }
  else
    {
      const char *prefix = online() ? "" : "上次回报（当前离线）：";
      switch (entity->kind)
        {
          case ESPHOME_SENSOR:
            if (isfinite(entity->value))
              {
                snprintf(text, sizeof(text), "%s%.6g %.*s", prefix,
                         (double)entity->value, (int)sizeof(entity->unit),
                         entity->unit);
              }
            else
              {
                snprintf(text, sizeof(text), "%s数值无效", prefix);
              }
            break;
          case ESPHOME_TEXT_SENSOR:
            snprintf(text, sizeof(text), "%s%.*s", prefix,
                     (int)sizeof(entity->text),
                     entity->text[0] ? entity->text : "（空文本）");
            break;
          default:
            snprintf(text, sizeof(text), "%s%s", prefix,
                     entity->state ? "开启" : "关闭");
            break;
        }
    }

  set_text(row->value, text);
  if (row->button != NULL)
    {
      set_text(row->button_text, enabled ?
               (entity->state ? "关闭" : "打开") : "不可控制");
      set_disabled(row->button, !enabled);
    }

  if (row->slider != NULL)
    {
      bool brightness_valid = entity->has_state &&
                              isfinite(entity->brightness) &&
                              entity->brightness >= 0.0f &&
                              entity->brightness <= 1.0f;
      bool adjustable = enabled && entity->supports_brightness &&
                        brightness_valid;

      set_disabled(row->slider, !adjustable);
      if (!entity->supports_brightness)
        {
          set_text(row->brightness, "此灯不支持亮度调节");
        }
      else if (!brightness_valid)
        {
          set_text(row->brightness, "亮度未知");
        }
      else
        {
          snprintf(text, sizeof(text), "设备回报亮度：%d%%",
                   brightness_percent(entity));
          set_text(row->brightness, text);
        }

      if (restore || !adjustable || !lv_slider_is_dragged(row->slider))
        {
          lv_slider_set_value(row->slider, brightness_valid ?
                              brightness_percent(entity) : 0, LV_ANIM_OFF);
        }
    }
}

static void entity_event(lv_event_t *event)
{
  struct entity_row *row = lv_event_get_user_data(event);
  lv_obj_t *target = lv_event_get_target(event);
  const struct esphome_entity *entity;
  bool slider = target == row->slider;
  bool state;
  int percent = slider ? lv_slider_get_value(target) : 0;
  int ret;

  /* The rendered row may predate removal/reordering or a disconnect. Never
   * use its list index as an entity identifier, or its cached state to send.
   */
  esphome_client_snapshot(&g_snapshot);
  entity = find_entity(row);
  update_row(row, entity, true);
  if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST)
    {
      return;
    }

  if (!online() || entity == NULL || !entity->has_state ||
      (entity->kind != ESPHOME_SWITCH && entity->kind != ESPHOME_LIGHT) ||
      (slider && (!entity->supports_brightness ||
                  !isfinite(entity->brightness) ||
                  entity->brightness < 0.0f || entity->brightness > 1.0f)))
    {
      set_text(g_ui.notice, "设备未在线或状态未知，未发送命令");
      return;
    }

  /* Brightness changes preserve the reported on/off state. The slider is
   * restored above; only a subsequent device snapshot changes the display.
   */
  state = slider ? entity->state : !entity->state;
  ret = esphome_client_command(row->key, row->device_id, state, slider,
                               slider ? percent / 100.0f : 0.0f);
  set_text(g_ui.notice, ret < 0 ? esphome_client_error(ret) :
                                 "命令已提交，等待设备回报");
}

static bool create_row(struct entity_row *row,
                        const struct esphome_entity *entity)
{
  row->key = entity->key;
  row->device_id = entity->device_id;
  row->kind = entity->kind;
  row->card = lv_obj_create(g_ui.list);
  if (row->card == NULL)
    {
      return false;
    }

  style_box(row->card, PAPER_CARD, 14);
  lv_obj_set_size(row->card, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(row->card, LV_OBJ_FLAG_SCROLLABLE);
  row->name = make_label(row->card, "", PAPER_INK);
  row->value = make_label(row->card, "", PAPER_MUTED);
  if (row->name == NULL || row->value == NULL)
    {
      goto fail;
    }

  if (entity->kind == ESPHOME_SWITCH || entity->kind == ESPHOME_LIGHT)
    {
      row->button = make_button(row->card, "不可控制", entity_event, row);
      if (row->button == NULL)
        {
          goto fail;
        }

      lv_obj_set_width(row->button, 120);
      row->button_text = lv_obj_get_child(row->button, 0);
    }

  if (entity->kind == ESPHOME_LIGHT)
    {
      row->brightness = make_label(row->card, "亮度未知", PAPER_MUTED);
      row->slider = lv_slider_create(row->card);
      if (row->brightness == NULL || row->slider == NULL)
        {
          goto fail;
        }

      lv_obj_set_size(row->slider, lv_pct(95), 18);
      lv_obj_set_style_margin_top(row->slider, 12, 0);
      lv_obj_set_style_margin_bottom(row->slider, 12, 0);
      lv_obj_set_style_bg_color(row->slider, lv_color_hex(PAPER_ACCENT),
                                LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(row->slider, lv_color_hex(PAPER_ACCENT),
                                LV_PART_KNOB);
      lv_obj_set_style_opa(row->slider, LV_OPA_40, LV_STATE_DISABLED);
      lv_slider_set_range(row->slider, 0, 100);
      lv_obj_add_event_cb(row->slider, entity_event, LV_EVENT_RELEASED, row);
      lv_obj_add_event_cb(row->slider, entity_event, LV_EVENT_PRESS_LOST, row);
    }

  update_row(row, entity, true);
  return true;

fail:
  lv_obj_delete(row->card);
  memset(row, 0, sizeof(*row));
  return false;
}

static void refresh_entities(void)
{
  size_t count = entity_count();
  size_t i;
  bool rebuild = count != g_ui.source_count;
  char text[192];

  for (i = 0; !rebuild && i < g_ui.row_count; i++)
    {
      const struct esphome_entity *entity = &g_snapshot.entities[i];
      const struct entity_row *row = &g_ui.rows[i];
      rebuild = row->key != entity->key || row->device_id != entity->device_id ||
                row->kind != entity->kind;
    }

  if (rebuild)
    {
      lv_obj_clean(g_ui.list);
      g_ui.empty = NULL;
      memset(g_ui.rows, 0, sizeof(g_ui.rows));
      g_ui.row_count = 0;
      g_ui.source_count = count;
      g_ui.rows_failed = false;
      for (i = 0; i < count; i++)
        {
          if (!create_row(&g_ui.rows[i], &g_snapshot.entities[i]))
            {
              g_ui.rows_failed = true;
              break;
            }

          g_ui.row_count++;
        }
    }

  if (count == 0)
    {
      if (g_ui.empty == NULL)
        {
          g_ui.empty = make_label(g_ui.list, "", PAPER_MUTED);
        }

      if (g_ui.empty != NULL)
        {
          set_text(g_ui.empty, online() ? "设备未提供可显示的实体" :
                                         "连接设备后显示实时实体");
        }
    }

  for (i = 0; i < g_ui.row_count; i++)
    {
      update_row(&g_ui.rows[i], &g_snapshot.entities[i], false);
    }

  snprintf(text, sizeof(text), "已显示 %u 个实体%s%s",
           (unsigned)g_ui.row_count,
           g_snapshot.truncated || g_snapshot.count > ESPHOME_MAX_ENTITIES ?
             " · 已达客户端实体上限，部分实体未显示" : "",
           g_ui.rows_failed ? " · 页面内存不足，部分实体未显示" : "");
  set_text(g_ui.summary, text);
}

static void start_from_form(void)
{
  struct esphome_config config;
  struct in_addr address;
  const char *port_text = lv_textarea_get_text(g_ui.port);
  char *end;
  unsigned long port;
  int ret;

  memset(&config, 0, sizeof(config));
  snprintf(config.address, sizeof(config.address), "%s",
           lv_textarea_get_text(g_ui.address));
  if (inet_pton(AF_INET, config.address, &address) != 1)
    {
      set_text(g_ui.notice, "请输入有效的设备 IPv4 地址");
      return;
    }

  port = strtoul(port_text, &end, 10);
  if (*port_text == '\0' || *end != '\0' || port == 0 || port > 65535)
    {
      set_text(g_ui.notice, "端口必须在 1 到 65535 之间");
      return;
    }

  config.port = (uint16_t)port;
  config.allow_plaintext = lv_obj_has_state(g_ui.plaintext, LV_STATE_CHECKED);
  snprintf(config.expected_name, sizeof(config.expected_name), "%s",
           lv_textarea_get_text(g_ui.expected_name));
  snprintf(config.noise_psk, sizeof(config.noise_psk), "%s",
           lv_textarea_get_text(g_ui.psk));
  if (!config.allow_plaintext && strlen(config.noise_psk) != 44)
    {
      set_text(g_ui.notice, "请输入 44 字符的 Noise PSK；明文需显式勾选");
      wipe(&config, sizeof(config));
      return;
    }

  if (config.allow_plaintext && config.noise_psk[0] != '\0')
    {
      set_text(g_ui.notice, "明文模式不能同时填写 Noise PSK");
      wipe(&config, sizeof(config));
      return;
    }

  keyboard_hide();
  g_ui.attempted = true;
  ret = esphome_client_start(&config);
  wipe(&config, sizeof(config));
  set_text(g_ui.notice, ret < 0 ? esphome_client_error(ret) :
                                 "连接请求已提交");
}

static void connection_event(lv_event_t *event)
{
  intptr_t action = (intptr_t)lv_event_get_user_data(event);

  esphome_client_snapshot(&g_snapshot);
  if (action == 1)
    {
      g_ui.retry_pending = false;
      g_ui.stopping = g_snapshot.running;
      esphome_client_stop();
      keyboard_hide();
      clear_psk();
      set_text(g_ui.notice, "已请求断开，密码已清除");
    }
  else if (action == 2 && g_snapshot.running)
    {
      g_ui.retry_pending = true;
      g_ui.stopping = true;
      esphome_client_stop();
      keyboard_hide();
      set_text(g_ui.notice, "等待后台停止后重试");
    }
  else if (!g_snapshot.running && !g_ui.stopping && !g_ui.retry_pending)
    {
      start_from_form();
    }

  /* Timer owns list rebuilding, so callbacks never delete their targets. */
  refresh_page();
}

static void refresh_page(void)
{
  const char *stage;
  bool busy;
  char text[224];

  esphome_client_snapshot(&g_snapshot);
  if (!g_snapshot.running)
    {
      g_ui.stopping = false;
    }

  busy = g_snapshot.running || g_ui.stopping || g_ui.retry_pending;
  set_disabled(g_ui.connect, busy);
  set_disabled(g_ui.disconnect, !busy);
  set_disabled(g_ui.retry, g_ui.stopping || g_ui.retry_pending ||
               !g_ui.attempted ||
               (g_snapshot.running && g_snapshot.status != ESPHOME_ERROR));
  set_disabled(g_ui.address, busy);
  set_disabled(g_ui.port, busy);
  set_disabled(g_ui.expected_name, busy);
  set_disabled(g_ui.psk, busy);
  set_disabled(g_ui.plaintext, busy);

  if (g_ui.stopping || g_ui.retry_pending)
    {
      stage = "正在停止 · 等待后台释放连接";
    }
  else
    {
      switch (g_snapshot.status)
        {
          case ESPHOME_IDLE:        stage = "未连接"; break;
          case ESPHOME_CONNECTING:  stage = "正在连接 · 协议握手"; break;
          case ESPHOME_DISCOVERING: stage = "正在发现实体 · 订阅状态"; break;
          case ESPHOME_READY:
            stage = g_snapshot.running ?
              (g_snapshot.encrypted ? "在线 · Noise 加密" : "在线 · 明文") :
              "已离线";
            break;
          case ESPHOME_STOPPING:    stage = "正在停止 · 释放连接"; break;
          case ESPHOME_ERROR:       stage = "连接失败"; break;
          default:                  stage = "未知连接阶段"; break;
        }
    }

  set_text(g_ui.stage, stage);
  if (g_snapshot.device_name[0] != '\0')
    {
      snprintf(text, sizeof(text), "设备：%.*s\n版本：%.*s",
               (int)sizeof(g_snapshot.device_name), g_snapshot.device_name,
               (int)sizeof(g_snapshot.version), g_snapshot.version);
    }
  else
    {
      snprintf(text, sizeof(text), "等待设备信息");
    }

  set_text(g_ui.device, text);
  set_text(g_ui.error, g_snapshot.error != 0 ?
           esphome_client_error(g_snapshot.error) : "");
  if (g_snapshot.error == 0)
    {
      lv_obj_add_flag(g_ui.error, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_remove_flag(g_ui.error, LV_OBJ_FLAG_HIDDEN);
    }
}

static void poll_timer(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  refresh_page();
  if (g_ui.retry_pending && !g_snapshot.running)
    {
      g_ui.retry_pending = false;
      start_from_form();
      refresh_page();
    }

  refresh_entities();
}

static lv_obj_t *make_field(const char *title, size_t limit, bool secret)
{
  lv_obj_t *field;
  if (make_label(g_ui.form, title, PAPER_INK) == NULL) return NULL;
  field = lv_textarea_create(g_ui.form);
  if (field == NULL) return NULL;
  lv_obj_set_width(field, lv_pct(100));
  lv_textarea_set_one_line(field, true);
  lv_textarea_set_max_length(field, limit);
  lv_obj_set_style_text_font(field, g_ui.font, 0);
  if (secret)
    {
      lv_textarea_set_password_mode(field, true);
      lv_textarea_set_password_show_time(field, 0);
    }
  lv_obj_add_event_cb(field, field_clicked, LV_EVENT_CLICKED, NULL);
  return field;
}

int esphome_lvgl_create(lv_obj_t *parent, const lv_font_t *font_zh,
                        esphome_lvgl_close_cb_t close_cb)
{
  lv_obj_t *actions;
  static const char * const view_names[] = {"连接", "实体", ""};
  if (parent == NULL || font_zh == NULL) return -EINVAL;
  esphome_lvgl_close();
  g_ui.font = font_zh;
  g_ui.close_cb = close_cb;
  g_ui.root = lv_obj_create(parent);
  if (g_ui.root == NULL) return -ENOMEM;
  style_box(g_ui.root, PAPER_BG, 16);
  lv_obj_set_size(g_ui.root, lv_pct(100), lv_pct(100));
  lv_obj_remove_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_ui.root, root_deleted, LV_EVENT_DELETE, NULL);
  if (make_button(g_ui.root, "返回", back_clicked, NULL) == NULL ||
      make_label(g_ui.root, "ESPHome", PAPER_INK) == NULL) goto fail;
  g_ui.tabs = lv_buttonmatrix_create(g_ui.root);
  g_ui.connection_view = lv_obj_create(g_ui.root);
  g_ui.entity_view = lv_obj_create(g_ui.root);
  if (!g_ui.tabs || !g_ui.connection_view || !g_ui.entity_view) goto fail;
  lv_buttonmatrix_set_map(g_ui.tabs, view_names);
  lv_buttonmatrix_set_button_ctrl_all(g_ui.tabs, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  lv_buttonmatrix_set_one_checked(g_ui.tabs, true);
  lv_buttonmatrix_set_button_ctrl(g_ui.tabs, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_size(g_ui.tabs, lv_pct(100), 48);
  lv_obj_set_style_text_font(g_ui.tabs, g_ui.font, LV_PART_ITEMS);
  lv_obj_add_event_cb(g_ui.tabs, view_changed, LV_EVENT_VALUE_CHANGED, NULL);
  style_box(g_ui.connection_view, PAPER_BG, 0);
  style_box(g_ui.entity_view, PAPER_BG, 0);
  lv_obj_set_size(g_ui.connection_view, lv_pct(100), 200);
  lv_obj_set_size(g_ui.entity_view, lv_pct(100), 200);
  lv_obj_set_flex_grow(g_ui.connection_view, 1);
  lv_obj_set_flex_grow(g_ui.entity_view, 1);
  lv_obj_add_flag(g_ui.entity_view, LV_OBJ_FLAG_HIDDEN);
  g_ui.form = lv_obj_create(g_ui.connection_view);
  if (g_ui.form == NULL) goto fail;
  style_box(g_ui.form, PAPER_BG, 0);
  lv_obj_set_size(g_ui.form, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(g_ui.form, LV_OBJ_FLAG_SCROLLABLE);
  g_ui.address = make_field("IPv4 地址", 15, false);
  g_ui.port = make_field("API 端口", 5, false);
  g_ui.expected_name = make_field("设备名称（可选）", 63, false);
  g_ui.psk = make_field("Noise PSK", 44, true);
  if (!g_ui.address || !g_ui.port || !g_ui.expected_name || !g_ui.psk)
    goto fail;
  lv_textarea_set_text(g_ui.port, "6053");
  lv_textarea_set_accepted_chars(g_ui.address, "0123456789.");
  lv_textarea_set_accepted_chars(g_ui.port, "0123456789");
  g_ui.plaintext = lv_checkbox_create(g_ui.form);
  if (g_ui.plaintext == NULL) goto fail;
  lv_checkbox_set_text(g_ui.plaintext, "明文连接（不加密）");
  actions = lv_obj_create(g_ui.connection_view);
  if (actions == NULL) goto fail;
  style_box(actions, PAPER_BG, 0);
  lv_obj_set_size(actions, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
  g_ui.connect = make_button(actions, "连接", connection_event, NULL);
  g_ui.disconnect = make_button(actions, "断开", connection_event, (void *)1);
  g_ui.retry = make_button(actions, "重试", connection_event, (void *)2);
  g_ui.stage = make_label(g_ui.root, "", PAPER_ACCENT);
  g_ui.device = make_label(g_ui.entity_view, "", PAPER_INK);
  g_ui.error = make_label(g_ui.root, "", PAPER_ERROR);
  g_ui.notice = make_label(g_ui.root, "", PAPER_MUTED);
  g_ui.summary = make_label(g_ui.entity_view, "", PAPER_MUTED);
  g_ui.list = lv_obj_create(g_ui.entity_view);
  if (!g_ui.connect || !g_ui.disconnect || !g_ui.retry || !g_ui.stage ||
      !g_ui.device || !g_ui.error || !g_ui.notice || !g_ui.summary ||
      !g_ui.list) goto fail;
  style_box(g_ui.list, PAPER_BG, 0);
  lv_obj_set_size(g_ui.list, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_remove_flag(g_ui.list, LV_OBJ_FLAG_SCROLLABLE);
  g_ui.keyboard = lv_keyboard_create(g_ui.root);
  if (g_ui.keyboard == NULL) goto fail;
  lv_obj_set_size(g_ui.keyboard, lv_pct(100), 240);
  lv_obj_set_style_text_font(g_ui.keyboard, g_ui.font, LV_PART_ITEMS);
  for (size_t i = 0; i < sizeof(g_key_ctrl) / sizeof(g_key_ctrl[0]); ++i)
    g_key_ctrl[i] = 1;
  lv_keyboard_set_map(g_ui.keyboard, LV_KEYBOARD_MODE_USER_1,
                      g_keys_lower, g_key_ctrl);
  lv_keyboard_set_map(g_ui.keyboard, LV_KEYBOARD_MODE_USER_2,
                      g_keys_upper, g_key_ctrl);
  lv_keyboard_set_mode(g_ui.keyboard, LV_KEYBOARD_MODE_USER_1);
  lv_obj_remove_event_cb(g_ui.keyboard, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(g_ui.keyboard, keyboard_event, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_add_flag(g_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
  g_ui.timer = lv_timer_create(poll_timer, POLL_MS, NULL);
  if (g_ui.timer == NULL) goto fail;
  refresh_page();
  refresh_entities();
  return 0;
fail:
  esphome_lvgl_close();
  return -ENOMEM;
}
