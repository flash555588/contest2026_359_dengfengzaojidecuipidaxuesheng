/****************************************************************************
 * apps/ha_panel/ha_ui.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "ha_ui.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HA_UI_MAX_CARDS CONFIG_HA_PANEL_MAX_ENTITIES

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef enum
{
  DISP_SMALL,
  DISP_MEDIUM,
  DISP_LARGE
} disp_size_t;

struct ha_card_s
{
  char entity_id[64];
  enum ha_domain_e domain;
  enum ha_area_e area;
  lv_obj_t *obj;
  lv_obj_t *name;
  lv_obj_t *state;
  lv_obj_t *sw;
  lv_obj_t *slider;
  lv_obj_t *arc;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static disp_size_t g_disp_size;
static const lv_font_t *g_font_large;
static const lv_font_t *g_font_normal;
static lv_style_t g_style_title;
static lv_style_t g_style_muted;
static lv_style_t g_style_icon;
static lv_style_t g_style_wrap;

static FAR struct ha_store_s *g_store;
static FAR struct ha_config_s *g_cfg;
static bool g_sync;
static bool g_cjk;
static int32_t g_card_w;
static int32_t g_card_h;

static lv_obj_t *g_status;
static lv_obj_t *g_clock;
static lv_obj_t *g_stats;
static lv_obj_t *g_dev_cont;
static lv_obj_t *g_cli_cont;
static lv_obj_t *g_scene_cont;
static lv_obj_t *g_kb;
static lv_obj_t *g_ta_server;
static lv_obj_t *g_ta_port;
static lv_obj_t *g_ta_token;
static lv_obj_t *g_set_info;
static int g_room_all = 1;
static enum ha_area_e g_room_sel = HA_AREA_LIVING;

static const char *g_tab_home = "Home";
static const char *g_tab_scene = "Scenes";
static const char *g_tab_dev = "Devices";
static const char *g_tab_cli = "Climate";
static const char *g_tab_set = "Settings";

/* Copied from examples/widgets/buttonmatrix/lv_example_buttonmatrix_1.c */

static const char *g_quick_map_en[] =
{
  "Movie", "Night", "Leave", "\n",
  "Home", "Read", "All off", ""
};

static const char *g_quick_map_zh[] =
{
  "影院", "晚安", "离家", "\n",
  "回家", "阅读", "全关", ""
};

static const char *g_quick_ids[] =
{
  "scene.movie", "scene.goodnight", "scene.leave_home",
  "scene.arrive_home", "scene.reading", "light.all"
};

static const char *g_room_map_en[] =
{
  "All", "Living", "Kitchen", "Bedroom", "Outdoor", ""
};

static const char *g_room_map_zh[] =
{
  "全部", "客厅", "厨房", "卧室", "户外", ""
};

static struct ha_card_s g_cards[HA_UI_MAX_CARDS];
static int g_ncards;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void cpy(FAR char *dst, size_t n, FAR const char *src)
{
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

static FAR struct ha_card_s *card_find(FAR const char *id)
{
  int i;

  for (i = 0; i < g_ncards; i++)
    {
      if (strcmp(g_cards[i].entity_id, id) == 0)
        {
          return &g_cards[i];
        }
    }

  return NULL;
}

static void set_checked(FAR lv_obj_t *obj, bool on)
{
  if (obj == NULL)
    {
      return;
    }

  if (on)
    {
      lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
  else
    {
      lv_obj_remove_state(obj, LV_STATE_CHECKED);
    }
}

/* Copied from examples/widgets/keyboard/lv_example_keyboard_1.c */

static void ta_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *ta = lv_event_get_target(e);
  lv_obj_t *kb = lv_event_get_user_data(e);

  if (code == LV_EVENT_FOCUSED)
    {
      lv_keyboard_set_textarea(kb, ta);
      lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }

  if (code == LV_EVENT_DEFOCUSED)
    {
      lv_keyboard_set_textarea(kb, NULL);
      lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void switch_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  const char *svc;

  if (g_sync || code != LV_EVENT_VALUE_CHANGED || card == NULL)
    {
      return;
    }

  if (card->domain == HA_DOMAIN_LOCK)
    {
      svc = lv_obj_has_state(obj, LV_STATE_CHECKED) ? "lock" : "unlock";
    }
  else
    {
      svc = lv_obj_has_state(obj, LV_STATE_CHECKED) ? "turn_on" : "turn_off";
    }

  ha_client_call(ha_domain_name(card->domain), svc, card->entity_id, NULL);
}

static void quick_event_cb(lv_event_t *e)
{
  lv_obj_t *obj = lv_event_get_target(e);
  uint32_t id;
  FAR const char *eid;

  if (g_sync || lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
      return;
    }

  id = lv_buttonmatrix_get_selected_button(obj);
  if (id >= (uint32_t)(sizeof(g_quick_ids) / sizeof(g_quick_ids[0])))
    {
      return;
    }

  eid = g_quick_ids[id];
  if (strcmp(eid, "light.all") == 0)
    {
      ha_client_call("light", "turn_off", "all", NULL);
      return;
    }

  ha_client_call("scene", "turn_on", eid, NULL);
}

static void room_event_cb(lv_event_t *e)
{
  lv_obj_t *obj = lv_event_get_target(e);
  uint32_t id;
  int i;

  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
      return;
    }

  id = lv_buttonmatrix_get_selected_button(obj);
  g_room_all = (id == 0);
  if (id == 1)
    {
      g_room_sel = HA_AREA_LIVING;
    }
  else if (id == 2)
    {
      g_room_sel = HA_AREA_KITCHEN;
    }
  else if (id == 3)
    {
      g_room_sel = HA_AREA_BEDROOM;
    }
  else if (id == 4)
    {
      g_room_sel = HA_AREA_OUTDOOR;
    }

  for (i = 0; i < g_ncards; i++)
    {
      bool show;

      if (g_cards[i].obj == NULL ||
          lv_obj_get_parent(g_cards[i].obj) != g_dev_cont)
        {
          continue;
        }

      show = g_room_all || g_cards[i].area == g_room_sel;
      if (show)
        {
          lv_obj_remove_flag(g_cards[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_cards[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void hvac_event_cb(lv_event_t *e)
{
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  FAR const char *txt;
  char extra[40];

  if (g_sync || card == NULL || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
      return;
    }

  txt = lv_label_get_text(lv_obj_get_child(btn, 0));
  if (txt == NULL)
    {
      return;
    }

  if (strcmp(txt, "Off") == 0 || strcmp(txt, "关") == 0)
    {
      ha_client_call("climate", "turn_off", card->entity_id, NULL);
      return;
    }

  snprintf(extra, sizeof(extra), "\"hvac_mode\":\"%s\"",
           (strcmp(txt, "Cool") == 0 || strcmp(txt, "制冷") == 0) ?
           "cool" : "heat");
  ha_client_call("climate", "set_hvac_mode", card->entity_id, extra);
}

static void media_event_cb(lv_event_t *e)
{
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  FAR const char *txt;
  const char *svc = "media_play_pause";

  if (g_sync || card == NULL || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
      return;
    }

  txt = lv_label_get_text(lv_obj_get_child(btn, 0));
  if (txt != NULL && strcmp(txt, LV_SYMBOL_PLUS) == 0)
    {
      svc = "volume_up";
    }
  else if (txt != NULL && strcmp(txt, LV_SYMBOL_MINUS) == 0)
    {
      svc = "volume_down";
    }

  ha_client_call("media_player", svc, card->entity_id, NULL);
}

static void vacuum_event_cb(lv_event_t *e)
{
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  FAR const char *txt;
  const char *svc = "start";

  if (g_sync || card == NULL || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
      return;
    }

  txt = lv_label_get_text(lv_obj_get_child(btn, 0));
  if (txt != NULL && (strcmp(txt, "Dock") == 0 || strcmp(txt, "回充") == 0))
    {
      svc = "return_to_base";
    }
  else if (txt != NULL && (strcmp(txt, "Stop") == 0 ||
                           strcmp(txt, "停止") == 0))
    {
      svc = "stop";
    }

  ha_client_call("vacuum", svc, card->entity_id, NULL);
}

static void slider_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *slider = lv_event_get_target(e);
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  char extra[40];
  int v;

  if (g_sync || card == NULL)
    {
      return;
    }

  v = (int)lv_slider_get_value(slider);
  if (card->state != NULL)
    {
      char buf[16];

      lv_snprintf(buf, sizeof(buf), "%d%%", (v * 100) / 255);
      lv_label_set_text(card->state, buf);
    }

  if (code != LV_EVENT_RELEASED)
    {
      return;
    }

  snprintf(extra, sizeof(extra), "\"brightness\":%d", v);
  ha_client_call("light", "turn_on", card->entity_id, extra);
}

static void arc_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *arc = lv_event_get_target(e);
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  char extra[48];
  int v;

  if (g_sync || card == NULL)
    {
      return;
    }

  v = (int)lv_arc_get_value(arc);
  if (card->state != NULL)
    {
      lv_label_set_text_fmt(card->state, "%d.%d C", v / 10, v % 10);
    }

  if (code != LV_EVENT_RELEASED)
    {
      return;
    }

  snprintf(extra, sizeof(extra), "\"temperature\":%.1f", v / 10.0);
  ha_client_call("climate", "set_temperature", card->entity_id, extra);
}

static void cover_event_cb(lv_event_t *e)
{
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  FAR const char *txt;

  if (g_sync || card == NULL || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
      return;
    }

  txt = lv_label_get_text(lv_obj_get_child(btn, 0));
  if (txt == NULL)
    {
      return;
    }

  if (strcmp(txt, LV_SYMBOL_UP) == 0)
    {
      ha_client_call("cover", "open_cover", card->entity_id, NULL);
    }
  else if (strcmp(txt, LV_SYMBOL_DOWN) == 0)
    {
      ha_client_call("cover", "close_cover", card->entity_id, NULL);
    }
  else
    {
      ha_client_call("cover", "stop_cover", card->entity_id, NULL);
    }
}

static void activate_event_cb(lv_event_t *e)
{
  FAR struct ha_card_s *card = lv_event_get_user_data(e);
  const char *svc = "turn_on";

  if (g_sync || card == NULL || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
      return;
    }

  if (card->domain == HA_DOMAIN_BUTTON)
    {
      svc = "press";
    }
  else if (card->domain == HA_DOMAIN_AUTOMATION)
    {
      svc = "toggle";
    }

  ha_client_call(ha_domain_name(card->domain), svc, card->entity_id, NULL);
}

static void save_event_cb(lv_event_t *e)
{
  const char *server;
  const char *port;
  const char *token;
  long p;

  if (lv_event_get_code(e) != LV_EVENT_CLICKED || g_cfg == NULL)
    {
      return;
    }

  server = lv_textarea_get_text(g_ta_server);
  port = lv_textarea_get_text(g_ta_port);
  token = lv_textarea_get_text(g_ta_token);
  strncpy(g_cfg->server, server, sizeof(g_cfg->server) - 1);
  strncpy(g_cfg->token, token, sizeof(g_cfg->token) - 1);
  p = strtol(port, NULL, 10);
  if (p > 0 && p <= 65535)
    {
      g_cfg->port = (uint16_t)p;
    }

  if (ha_config_save(g_cfg) < 0)
    {
      lv_label_set_text(g_set_info, "Save failed");
    }
  else
    {
      lv_label_set_text(g_set_info, "Saved, reconnecting...");
    }

  ha_client_reconnect();
}

static void clock_cb(lv_timer_t *timer)
{
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);

  UNUSED(timer);
  if (g_clock == NULL || tm == NULL)
    {
      return;
    }

  lv_label_set_text_fmt(g_clock, "%02d:%02d", tm->tm_hour, tm->tm_min);
}

static FAR const char *symbol_for(enum ha_domain_e d)
{
  switch (d)
    {
      case HA_DOMAIN_LIGHT:         return LV_SYMBOL_CHARGE;
      case HA_DOMAIN_SWITCH:
      case HA_DOMAIN_INPUT_BOOLEAN: return LV_SYMBOL_POWER;
      case HA_DOMAIN_SENSOR:        return LV_SYMBOL_GPS;
      case HA_DOMAIN_BINARY_SENSOR: return LV_SYMBOL_EYE_OPEN;
      case HA_DOMAIN_CLIMATE:       return LV_SYMBOL_REFRESH;
      case HA_DOMAIN_COVER:         return LV_SYMBOL_UP;
      case HA_DOMAIN_SCENE:         return LV_SYMBOL_IMAGE;
      case HA_DOMAIN_SCRIPT:        return LV_SYMBOL_PLAY;
      case HA_DOMAIN_BUTTON:        return LV_SYMBOL_OK;
      case HA_DOMAIN_FAN:           return LV_SYMBOL_LOOP;
      case HA_DOMAIN_LOCK:          return LV_SYMBOL_WARNING;
      case HA_DOMAIN_MEDIA_PLAYER:  return LV_SYMBOL_VIDEO;
      case HA_DOMAIN_VACUUM:        return LV_SYMBOL_DRIVE;
      case HA_DOMAIN_AUTOMATION:    return LV_SYMBOL_SETTINGS;
      default:                      return LV_SYMBOL_DUMMY;
    }
}

static void add_text_btns(FAR lv_obj_t *parent, FAR struct ha_card_s *card,
                          FAR const char **labels, int n,
                          void (*cb)(lv_event_t *e))
{
  lv_obj_t *row = lv_obj_create(parent);
  int i;

  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  for (i = 0; i < n; i++)
    {
      lv_obj_t *b = lv_button_create(row);
      lv_obj_t *lab = lv_label_create(b);

      lv_obj_set_height(b, 36);
      lv_label_set_text(lab, labels[i]);
      lv_obj_center(lab);
      lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, card);
    }
}

static void add_cover_btns(FAR lv_obj_t *parent, FAR struct ha_card_s *card)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_t *b;
  lv_obj_t *lab;
  const char *syms[3] = { LV_SYMBOL_UP, LV_SYMBOL_STOP, LV_SYMBOL_DOWN };
  int i;

  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  for (i = 0; i < 3; i++)
    {
      b = lv_button_create(row);
      lv_obj_set_size(b, 48, 36);
      lab = lv_label_create(b);
      lv_label_set_text(lab, syms[i]);
      lv_obj_center(lab);
      lv_obj_add_event_cb(b, cover_event_cb, LV_EVENT_CLICKED, card);
    }
}

static FAR struct ha_card_s *card_create(FAR lv_obj_t *parent,
                                         FAR const struct ha_entity_s *ent)
{
  FAR struct ha_card_s *card;
  lv_obj_t *icon;
  lv_obj_t *col;

  if (g_ncards >= HA_UI_MAX_CARDS)
    {
      return NULL;
    }

  card = &g_cards[g_ncards++];
  memset(card, 0, sizeof(*card));
  cpy(card->entity_id, sizeof(card->entity_id), ent->entity_id);
  card->domain = ent->domain;
  card->area = ent->area;

  card->obj = lv_obj_create(parent);
  lv_obj_set_size(card->obj, g_card_w, g_card_h);
  lv_obj_set_flex_flow(card->obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card->obj, 10, 0);
  lv_obj_set_style_pad_row(card->obj, 6, 0);
  lv_obj_set_style_radius(card->obj, 12, 0);
  lv_obj_remove_flag(card->obj, LV_OBJ_FLAG_SCROLLABLE);

  col = lv_obj_create(card->obj);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(col, 8, 0);

  icon = lv_label_create(col);
  lv_obj_add_style(icon, &g_style_icon, 0);
  lv_label_set_text(icon, symbol_for(ent->domain));

  card->name = lv_label_create(col);
  lv_obj_add_style(card->name, &g_style_title, 0);
  lv_label_set_long_mode(card->name, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(card->name, 1);
  lv_label_set_text(card->name, ent->friendly_name);

  card->state = lv_label_create(card->obj);
  lv_obj_add_style(card->state, &g_style_muted, 0);
  lv_label_set_text(card->state, ent->state);

  if (ent->domain == HA_DOMAIN_LIGHT ||
      ent->domain == HA_DOMAIN_SWITCH ||
      ent->domain == HA_DOMAIN_INPUT_BOOLEAN ||
      ent->domain == HA_DOMAIN_FAN ||
      ent->domain == HA_DOMAIN_LOCK)
    {
      card->sw = lv_switch_create(card->obj);
      lv_obj_add_event_cb(card->sw, switch_event_cb, LV_EVENT_VALUE_CHANGED,
                          card);
    }

  if (ent->domain == HA_DOMAIN_LIGHT)
    {
      card->slider = lv_slider_create(card->obj);
      lv_obj_set_width(card->slider, LV_PCT(100));
      lv_slider_set_range(card->slider, 0, 255);
      lv_obj_add_event_cb(card->slider, slider_event_cb, LV_EVENT_ALL, card);
    }

  if (ent->domain == HA_DOMAIN_CLIMATE)
    {
      const char *modes[3];

      card->arc = lv_arc_create(card->obj);
      lv_obj_set_size(card->arc, 110, 110);
      lv_arc_set_rotation(card->arc, 135);
      lv_arc_set_bg_angles(card->arc, 0, 270);
      lv_arc_set_range(card->arc, 160, 300);
      lv_obj_add_event_cb(card->arc, arc_event_cb, LV_EVENT_ALL, card);

      modes[0] = g_cjk ? "制热" : "Heat";
      modes[1] = g_cjk ? "制冷" : "Cool";
      modes[2] = g_cjk ? "关" : "Off";
      add_text_btns(card->obj, card, modes, 3, hvac_event_cb);
    }

  if (ent->domain == HA_DOMAIN_COVER)
    {
      add_cover_btns(card->obj, card);
    }

  if (ent->domain == HA_DOMAIN_SCENE ||
      ent->domain == HA_DOMAIN_SCRIPT ||
      ent->domain == HA_DOMAIN_BUTTON ||
      ent->domain == HA_DOMAIN_AUTOMATION)
    {
      lv_obj_t *btn = lv_button_create(card->obj);
      lv_obj_t *lab = lv_label_create(btn);
      const char *txt = "Activate";

      if (ent->domain == HA_DOMAIN_BUTTON)
        {
          txt = g_cjk ? "按下" : "Press";
        }
      else if (ent->domain == HA_DOMAIN_AUTOMATION)
        {
          txt = g_cjk ? "切换" : "Toggle";
        }
      else
        {
          txt = g_cjk ? "执行" : "Activate";
        }

      lv_label_set_text(lab, txt);
      lv_obj_center(lab);
      lv_obj_add_event_cb(btn, activate_event_cb, LV_EVENT_CLICKED, card);
    }

  if (ent->domain == HA_DOMAIN_MEDIA_PLAYER)
    {
      const char *labs[3] =
        {
          LV_SYMBOL_PLAY, LV_SYMBOL_MINUS, LV_SYMBOL_PLUS
        };

      add_text_btns(card->obj, card, labs, 3, media_event_cb);
    }

  if (ent->domain == HA_DOMAIN_VACUUM)
    {
      const char *labs[3];

      labs[0] = g_cjk ? "清扫" : "Start";
      labs[1] = g_cjk ? "停止" : "Stop";
      labs[2] = g_cjk ? "回充" : "Dock";
      add_text_btns(card->obj, card, labs, 3, vacuum_event_cb);
    }

  return card;
}

static void card_update(FAR struct ha_card_s *card,
                        FAR const struct ha_entity_s *ent)
{
  char buf[48];

  g_sync = true;
  lv_label_set_text(card->name, ent->friendly_name);

  if (ent->domain == HA_DOMAIN_SENSOR ||
      ent->domain == HA_DOMAIN_BINARY_SENSOR)
    {
      if (ent->unit[0] != '\0')
        {
          snprintf(buf, sizeof(buf), "%s %s", ent->state, ent->unit);
          lv_label_set_text(card->state, buf);
        }
      else
        {
          lv_label_set_text(card->state, ent->state);
        }
    }
  else if (ent->domain == HA_DOMAIN_CLIMATE)
    {
      int t = ent->target_x10 > -1000 ? ent->target_x10 : ent->temp_x10;

      if (t > -1000)
        {
          lv_label_set_text_fmt(card->state, "%s  %d.%d C",
                                ent->hvac_mode[0] ? ent->hvac_mode :
                                ent->state,
                                t / 10, (t < 0 ? -t : t) % 10);
        }
      else
        {
          lv_label_set_text(card->state, ent->state);
        }

      if (card->arc != NULL && t > -1000)
        {
          lv_arc_set_value(card->arc, t);
        }
    }
  else if (ent->domain == HA_DOMAIN_LIGHT && ent->brightness >= 0)
    {
      lv_label_set_text_fmt(card->state, "%s  %d%%", ent->state,
                            (ent->brightness * 100) / 255);
    }
  else if ((ent->domain == HA_DOMAIN_COVER ||
            ent->domain == HA_DOMAIN_MEDIA_PLAYER ||
            ent->domain == HA_DOMAIN_FAN) && ent->position >= 0)
    {
      lv_label_set_text_fmt(card->state, "%s  %d%%", ent->state,
                            ent->position);
    }
  else
    {
      lv_label_set_text(card->state, ent->state);
    }

  if (card->sw != NULL)
    {
      set_checked(card->sw, ha_entity_is_on(ent));
      if (!ent->available || ha_client_conn() == HA_CONN_ERROR)
        {
          lv_obj_add_state(card->sw, LV_STATE_DISABLED);
        }
      else
        {
          lv_obj_remove_state(card->sw, LV_STATE_DISABLED);
        }
    }

  if (card->slider != NULL && ent->brightness >= 0)
    {
      lv_slider_set_value(card->slider, ent->brightness, LV_ANIM_OFF);
    }

  g_sync = false;
}

static void refresh_home(FAR const struct ha_entity_s *snap, int n)
{
  int lights = 0;
  int i;
  int climate = 0;
  char ver[32] = "";

  for (i = 0; i < n; i++)
    {
      if (snap[i].domain == HA_DOMAIN_LIGHT && ha_entity_is_on(&snap[i]))
        {
          lights++;
        }

      if (snap[i].domain == HA_DOMAIN_CLIMATE)
        {
          climate++;
        }
    }

  ha_store_lock(g_store);
  strncpy(ver, g_store->ha_version, sizeof(ver) - 1);
  ha_store_unlock(g_store);

  if (g_stats != NULL)
    {
      lv_label_set_text_fmt(g_stats,
                            g_cjk ?
                            "实体 %d    开灯 %d    空调 %d    HA %s" :
                            "Entities %d    Lights on %d    Climate %d    HA %s",
                            n, lights, climate, ver[0] ? ver : "-");
    }
}

static void refresh_cards(void)
{
  struct ha_entity_s snap[CONFIG_HA_PANEL_MAX_ENTITIES];
  int n;
  int i;

  n = ha_store_snapshot(g_store, snap, CONFIG_HA_PANEL_MAX_ENTITIES);
  refresh_home(snap, n);

  for (i = 0; i < n; i++)
    {
      FAR struct ha_card_s *card = card_find(snap[i].entity_id);
      FAR lv_obj_t *parent;

      if (card != NULL)
        {
          card_update(card, &snap[i]);
          continue;
        }

      if (snap[i].domain == HA_DOMAIN_CLIMATE)
        {
          parent = g_cli_cont;
        }
      else if (snap[i].domain == HA_DOMAIN_SCENE ||
               snap[i].domain == HA_DOMAIN_SCRIPT)
        {
          parent = g_scene_cont;
        }
      else
        {
          parent = g_dev_cont;
        }

      card = card_create(parent, &snap[i]);
      if (card != NULL)
        {
          card_update(card, &snap[i]);
        }
    }

  for (i = 0; i < g_ncards; i++)
    {
      bool show;

      if (g_cards[i].obj == NULL ||
          lv_obj_get_parent(g_cards[i].obj) != g_dev_cont)
        {
          continue;
        }

      show = g_room_all || g_cards[i].area == g_room_sel;
      if (show)
        {
          lv_obj_remove_flag(g_cards[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_cards[i].obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void set_status(enum ha_conn_e conn, FAR const char *text)
{
  const char *prefix = g_cjk ? "空闲" : "Idle";

  switch (conn)
    {
      case HA_CONN_DEMO:       prefix = g_cjk ? "演示" : "Demo"; break;
      case HA_CONN_CONNECTING: prefix = g_cjk ? "连接中" : "Connecting"; break;
      case HA_CONN_AUTH:       prefix = g_cjk ? "认证" : "Auth"; break;
      case HA_CONN_LIVE:       prefix = g_cjk ? "在线" : "Live"; break;
      case HA_CONN_ERROR:      prefix = g_cjk ? "错误" : "Error"; break;
      default: break;
    }

  if (g_status != NULL)
    {
      lv_label_set_text_fmt(g_status, "%s  %s", prefix,
                            text != NULL ? text : "");
    }
}

static void make_home(FAR lv_obj_t *parent)
{
  lv_obj_t *hero;
  lv_obj_t *title;

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 12, 0);

  hero = lv_obj_create(parent);
  lv_obj_set_size(hero, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(hero, 16, 0);
  lv_obj_set_style_radius(hero, 12, 0);

  title = lv_label_create(hero);
  lv_obj_add_style(title, &g_style_title, 0);
  lv_label_set_text(title, g_cjk ? "家庭控制面板" : "Home Assistant");

  g_clock = lv_label_create(hero);
  lv_obj_add_style(g_clock, &g_style_title, 0);
  lv_label_set_text(g_clock, "--:--");

  g_status = lv_label_create(hero);
  lv_obj_add_style(g_status, &g_style_muted, 0);
  lv_label_set_text(g_status, "Idle");

  g_stats = lv_label_create(parent);
  lv_label_set_text(g_stats, "Entities 0");

  {
    lv_obj_t *quick;
    lv_obj_t *hint;

    hint = lv_label_create(parent);
    lv_obj_add_style(hint, &g_style_title, 0);
    lv_label_set_text(hint, g_cjk ? "场景快捷" : "Scenes");

    quick = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(quick, g_cjk ? g_quick_map_zh : g_quick_map_en);
    lv_obj_set_width(quick, LV_PCT(100));
    lv_obj_set_height(quick, g_disp_size == DISP_SMALL ? 90 : 110);
    lv_obj_add_event_cb(quick, quick_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
  }

  lv_timer_create(clock_cb, 1000, NULL);
  clock_cb(NULL);
}

static FAR lv_obj_t *make_room_page(FAR lv_obj_t *tab)
{
  lv_obj_t *rooms;
  lv_obj_t *cont;

  lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(tab, 8, 0);

  rooms = lv_buttonmatrix_create(tab);
  lv_buttonmatrix_set_map(rooms, g_cjk ? g_room_map_zh : g_room_map_en);
  lv_obj_set_width(rooms, LV_PCT(100));
  lv_obj_set_height(rooms, 48);
  lv_obj_add_event_cb(rooms, room_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  cont = lv_obj_create(tab);
  lv_obj_set_width(cont, LV_PCT(100));
  lv_obj_set_flex_grow(cont, 1);
  lv_obj_add_style(cont, &g_style_wrap, 0);
  return cont;
}

static void make_flex_wrap(FAR lv_obj_t *parent)
{
  lv_obj_add_style(parent, &g_style_wrap, 0);
  lv_obj_set_size(parent, LV_PCT(100), LV_PCT(100));
}

static void make_settings(FAR lv_obj_t *parent)
{
  lv_obj_t *lab;
  lv_obj_t *btn;
  lv_obj_t *blab;
  char port[8];

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);

  lab = lv_label_create(parent);
  lv_obj_add_style(lab, &g_style_title, 0);
  lv_label_set_text(lab, g_cjk ? "连接" : "Connection");

  lab = lv_label_create(parent);
  lv_obj_add_style(lab, &g_style_muted, 0);
  lv_label_set_text(lab, "Server");
  g_ta_server = lv_textarea_create(parent);
  lv_textarea_set_one_line(g_ta_server, true);
  lv_textarea_set_text(g_ta_server, g_cfg->server);
  lv_obj_set_width(g_ta_server, LV_PCT(100));
  lv_obj_add_event_cb(g_ta_server, ta_event_cb, LV_EVENT_ALL, g_kb);

  lab = lv_label_create(parent);
  lv_obj_add_style(lab, &g_style_muted, 0);
  lv_label_set_text(lab, "Port");
  g_ta_port = lv_textarea_create(parent);
  lv_textarea_set_one_line(g_ta_port, true);
  snprintf(port, sizeof(port), "%u", (unsigned)g_cfg->port);
  lv_textarea_set_text(g_ta_port, port);
  lv_obj_set_width(g_ta_port, LV_PCT(100));
  lv_obj_add_event_cb(g_ta_port, ta_event_cb, LV_EVENT_ALL, g_kb);

  lab = lv_label_create(parent);
  lv_obj_add_style(lab, &g_style_muted, 0);
  lv_label_set_text(lab, "Long-lived access token");
  g_ta_token = lv_textarea_create(parent);
  lv_textarea_set_one_line(g_ta_token, true);
  lv_textarea_set_password_mode(g_ta_token, true);
  lv_textarea_set_placeholder_text(g_ta_token, "eyJ...");
  lv_textarea_set_text(g_ta_token, g_cfg->token);
  lv_obj_set_width(g_ta_token, LV_PCT(100));
  lv_obj_add_event_cb(g_ta_token, ta_event_cb, LV_EVENT_ALL, g_kb);

  btn = lv_button_create(parent);
  blab = lv_label_create(btn);
  lv_label_set_text(blab, g_cjk ? "保存并重连" : "Save & reconnect");
  lv_obj_center(blab);
  lv_obj_add_event_cb(btn, save_event_cb, LV_EVENT_CLICKED, NULL);

  g_set_info = lv_label_create(parent);
  lv_obj_add_style(g_set_info, &g_style_muted, 0);
  lv_label_set_text(g_set_info,
                    "Token: HA Profile -> Security -> Long-Lived Access Tokens");
}

static void load_cjk_font(void)
{
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  if (access(CONFIG_HA_PANEL_TTF_CJK, R_OK) == 0)
    {
      lv_font_t *font = lv_tiny_ttf_create_file(CONFIG_HA_PANEL_TTF_CJK, 18);

      if (font != NULL)
        {
          g_font_normal = font;
          g_font_large = font;
          g_cjk = true;
          g_tab_home = "首页";
          g_tab_scene = "场景";
          g_tab_dev = "设备";
          g_tab_cli = "气候";
          g_tab_set = "设置";
        }
    }
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ha_ui_create(FAR struct ha_store_s *store, FAR struct ha_config_s *cfg)
{
  lv_obj_t *tv;
  lv_obj_t *t1;
  lv_obj_t *t2;
  lv_obj_t *t3;
  lv_obj_t *t4;
  lv_obj_t *t5;
  int32_t tab_h;

  g_store = store;
  g_cfg = cfg;
  g_ncards = 0;
  g_cjk = false;

  /* Responsive sizes copied from demos/widgets/lv_demo_widgets.c */

  if (LV_HOR_RES <= 320)
    {
      g_disp_size = DISP_SMALL;
      g_card_w = LV_HOR_RES - 24;
      g_card_h = 150;
      tab_h = 45;
    }
  else if (LV_HOR_RES < 720)
    {
      g_disp_size = DISP_MEDIUM;
      g_card_w = 180;
      g_card_h = 170;
      tab_h = 45;
    }
  else
    {
      g_disp_size = DISP_LARGE;
      g_card_w = 230;
      g_card_h = 190;
      tab_h = 70;
    }

  g_font_large = LV_FONT_DEFAULT;
  g_font_normal = LV_FONT_DEFAULT;

  if (g_disp_size == DISP_LARGE)
    {
#if LV_FONT_MONTSERRAT_24
      g_font_large = &lv_font_montserrat_24;
#endif
#if LV_FONT_MONTSERRAT_16
      g_font_normal = &lv_font_montserrat_16;
#endif
    }
  else if (g_disp_size == DISP_MEDIUM)
    {
#if LV_FONT_MONTSERRAT_20
      g_font_large = &lv_font_montserrat_20;
#endif
#if LV_FONT_MONTSERRAT_14
      g_font_normal = &lv_font_montserrat_14;
#endif
    }
  else
    {
#if LV_FONT_MONTSERRAT_18
      g_font_large = &lv_font_montserrat_18;
#endif
#if LV_FONT_MONTSERRAT_12
      g_font_normal = &lv_font_montserrat_12;
#endif
    }

  load_cjk_font();

#if LV_USE_THEME_DEFAULT
  lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE),
                        lv_palette_main(LV_PALETTE_RED),
                        true, g_font_normal);
#endif

  lv_style_init(&g_style_muted);
  lv_style_set_text_opa(&g_style_muted, LV_OPA_50);

  lv_style_init(&g_style_title);
  lv_style_set_text_font(&g_style_title, g_font_large);

  lv_style_init(&g_style_icon);
  lv_style_set_text_color(&g_style_icon, lv_theme_get_color_primary(NULL));
  lv_style_set_text_font(&g_style_icon, g_font_large);

  /* Copied from examples/layouts/flex/lv_example_flex_2.c */

  lv_style_init(&g_style_wrap);
  lv_style_set_flex_flow(&g_style_wrap, LV_FLEX_FLOW_ROW_WRAP);
  lv_style_set_flex_main_place(&g_style_wrap, LV_FLEX_ALIGN_SPACE_EVENLY);
  lv_style_set_layout(&g_style_wrap, LV_LAYOUT_FLEX);
  lv_style_set_pad_row(&g_style_wrap, 10);
  lv_style_set_pad_column(&g_style_wrap, 10);

  lv_obj_set_style_text_font(lv_screen_active(), g_font_normal, 0);

  g_kb = lv_keyboard_create(lv_screen_active());
  lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);

  tv = lv_tabview_create(lv_screen_active());
  lv_tabview_set_tab_bar_size(tv, tab_h);

  t1 = lv_tabview_add_tab(tv, g_tab_home);
  t2 = lv_tabview_add_tab(tv, g_tab_scene);
  t3 = lv_tabview_add_tab(tv, g_tab_dev);
  t4 = lv_tabview_add_tab(tv, g_tab_cli);
  t5 = lv_tabview_add_tab(tv, g_tab_set);

  make_home(t1);
  g_scene_cont = t2;
  make_flex_wrap(t2);
  g_dev_cont = make_room_page(t3);
  g_cli_cont = t4;
  make_flex_wrap(t4);
  make_settings(t5);

  UNUSED(g_disp_size);
  return 0;
}

void ha_ui_apply_evt(FAR const struct ha_evt_s *evt)
{
  if (evt == NULL)
    {
      return;
    }

  if (evt->type == HA_EVT_CONN || evt->type == HA_EVT_ERROR)
    {
      set_status(evt->conn, evt->text);
    }

  if (evt->type == HA_EVT_STORE || evt->type == HA_EVT_CONN)
    {
      refresh_cards();
    }
}
