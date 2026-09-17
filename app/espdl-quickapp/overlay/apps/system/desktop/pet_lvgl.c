/****************************************************************************
 * apps/system/desktop/pet_lvgl.c
 *
 * Always-on floating whale overlay.  Other apps talk to the engine via
 * system.pet; this file only draws and handles drag/poke.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "pet_lvgl.h"
#include "pet_engine.h"

#include <string.h>

#define BUBBLE_H  36
#define BODY_BLUE 0x3d9ad1
#define BODY_DEEP 0x2b6f9c
#define BELLY     0xf4ead6
#define BLUSH     0xf3a6b5
#define INK       0x243040

struct pet_ui_s
{
  lv_obj_t *root;
  lv_obj_t *bubble;
  lv_obj_t *body;
  lv_obj_t *belly;
  lv_obj_t *tail;
  lv_obj_t *eye_l;
  lv_obj_t *eye_r;
  lv_obj_t *pupil_l;
  lv_obj_t *pupil_r;
  lv_obj_t *blush_l;
  lv_obj_t *blush_r;
  lv_obj_t *mouth;
  const lv_font_t *font_zh;
  lv_timer_t *timer;
  int press_x;
  int press_y;
  bool dragging;
  bool press_moved;
  bool open;
};

static struct pet_ui_s g_ui;

static lv_obj_t *make_blob(lv_obj_t *parent, uint32_t color, int radius)
{
  lv_obj_t *obj = lv_obj_create(parent);

  if (obj == NULL)
    {
      return NULL;
    }

  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

static void layout_parts(const struct pet_state_s *state)
{
  int w = state->w;
  int h = state->h;
  int body_w = (w * 78) / 100;
  int body_h = (h * 70) / 100;
  int body_x = (w - body_w) / 2;
  int body_y = BUBBLE_H + (h - body_h) / 2;
  bool side = state->dir == PET_LEFT || state->dir == PET_RIGHT;
  bool back = state->dir == PET_UP;
  int face = state->facing;

  lv_obj_set_size(g_ui.root, w, h + BUBBLE_H);
  lv_obj_set_size(g_ui.body, body_w, body_h);
  lv_obj_set_pos(g_ui.body, body_x, body_y);
  lv_obj_set_size(g_ui.belly, (body_w * 62) / 100, (body_h * 42) / 100);
  lv_obj_set_pos(g_ui.belly, body_x + (body_w * 19) / 100,
                 body_y + (body_h * 42) / 100);

  if (side)
    {
      int tail_x = face > 0 ? body_x + body_w - 6 : body_x - (w * 18) / 100 + 6;

      lv_obj_set_size(g_ui.tail, (w * 22) / 100, (h * 28) / 100);
      lv_obj_set_pos(g_ui.tail, tail_x, body_y + (body_h * 28) / 100);
      lv_obj_remove_flag(g_ui.tail, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_ui.eye_r, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_ui.pupil_r, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_ui.blush_r, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(g_ui.eye_l, 10, 10);
      lv_obj_set_pos(g_ui.eye_l,
                     face > 0 ? body_x + 14 : body_x + body_w - 24,
                     body_y + 14);
      lv_obj_set_size(g_ui.pupil_l, 4, 4);
      lv_obj_set_pos(g_ui.pupil_l,
                     face > 0 ? body_x + 17 : body_x + body_w - 21,
                     body_y + 17);
      lv_obj_set_size(g_ui.blush_l, 8, 5);
      lv_obj_set_pos(g_ui.blush_l,
                     face > 0 ? body_x + 12 : body_x + body_w - 22,
                     body_y + 28);
      lv_obj_set_size(g_ui.mouth, 12, 4);
      lv_obj_set_pos(g_ui.mouth,
                     face > 0 ? body_x + 18 : body_x + body_w - 30,
                     body_y + 34);
    }
  else
    {
      lv_obj_set_size(g_ui.tail, (w * 18) / 100, (h * 16) / 100);
      lv_obj_set_pos(g_ui.tail, body_x + body_w / 2 - (w * 9) / 100,
                     body_y + body_h - 8);
      lv_obj_remove_flag(g_ui.tail, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(g_ui.eye_l, 11, 11);
      lv_obj_set_size(g_ui.eye_r, 11, 11);
      lv_obj_set_pos(g_ui.eye_l, body_x + body_w / 2 - 18, body_y + 14);
      lv_obj_set_pos(g_ui.eye_r, body_x + body_w / 2 + 7, body_y + 14);
      lv_obj_set_size(g_ui.pupil_l, 4, 4);
      lv_obj_set_size(g_ui.pupil_r, 4, 4);
      lv_obj_set_pos(g_ui.pupil_l, body_x + body_w / 2 - 15, body_y + 17);
      lv_obj_set_pos(g_ui.pupil_r, body_x + body_w / 2 + 10, body_y + 17);
      lv_obj_set_size(g_ui.blush_l, 9, 5);
      lv_obj_set_size(g_ui.blush_r, 9, 5);
      lv_obj_set_pos(g_ui.blush_l, body_x + body_w / 2 - 22, body_y + 28);
      lv_obj_set_pos(g_ui.blush_r, body_x + body_w / 2 + 13, body_y + 28);
      lv_obj_set_size(g_ui.mouth, 16, 5);
      lv_obj_set_pos(g_ui.mouth, body_x + body_w / 2 - 8, body_y + 36);
      if (back)
        {
          lv_obj_add_flag(g_ui.eye_l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.eye_r, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.pupil_l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.pupil_r, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.blush_l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.blush_r, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(g_ui.mouth, LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_remove_flag(g_ui.eye_l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.eye_r, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.pupil_l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.pupil_r, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.blush_l, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.blush_r, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(g_ui.mouth, LV_OBJ_FLAG_HIDDEN);
        }
    }

  if (!back && side)
    {
      lv_obj_remove_flag(g_ui.eye_l, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_ui.pupil_l, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_ui.blush_l, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_ui.mouth, LV_OBJ_FLAG_HIDDEN);
    }
}

static int pointer_xy(lv_event_t *e, int *x, int *y)
{
  lv_indev_t *indev = lv_event_get_indev(e);
  lv_point_t point;

  if (indev == NULL || x == NULL || y == NULL)
    {
      return -1;
    }

  lv_indev_get_point(indev, &point);
  *x = point.x;
  *y = point.y;
  return 0;
}

static void pet_event_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  int x;
  int y;

  if (code == LV_EVENT_PRESSED)
    {
      if (pointer_xy(e, &x, &y) < 0)
        {
          return;
        }

      g_ui.press_x = x;
      g_ui.press_y = y;
      g_ui.dragging = false;
      g_ui.press_moved = false;
    }
  else if (code == LV_EVENT_PRESSING)
    {
      if (pointer_xy(e, &x, &y) < 0)
        {
          return;
        }

      if (!g_ui.dragging)
        {
          int dx = x - g_ui.press_x;
          int dy = y - g_ui.press_y;

          if (dx * dx + dy * dy > 64)
            {
              g_ui.dragging = true;
              g_ui.press_moved = true;
              pet_engine_drag_begin(g_ui.press_x, g_ui.press_y);
            }
        }

      if (g_ui.dragging)
        {
          pet_engine_drag_move(x, y);
          pet_lvgl_sync();
        }
    }
  else if (code == LV_EVENT_RELEASED)
    {
      if (g_ui.dragging)
        {
          pet_engine_drag_end();
        }

      g_ui.dragging = false;
    }
  else if (code == LV_EVENT_CLICKED)
    {
      if (!g_ui.press_moved)
        {
          pet_engine_poke();
          pet_lvgl_sync();
        }
    }
  else if (code == LV_EVENT_DOUBLE_CLICKED)
    {
      pet_engine_feed("fish");
      pet_lvgl_sync();
    }
}

static void pet_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  pet_engine_tick(50);
  pet_lvgl_sync();
}

int pet_lvgl_create(lv_obj_t *parent, const lv_font_t *font_zh)
{
  struct pet_state_s state;

  if (parent == NULL)
    {
      return -1;
    }

  pet_lvgl_destroy();
  pet_engine_get(&state);
  g_ui.font_zh = font_zh;
  g_ui.root = lv_obj_create(parent);
  if (g_ui.root == NULL)
    {
      return -1;
    }

  lv_obj_set_style_bg_opa(g_ui.root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_ui.root, 0, 0);
  lv_obj_set_style_pad_all(g_ui.root, 0, 0);
  lv_obj_set_style_shadow_width(g_ui.root, 0, 0);
  lv_obj_remove_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_ui.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_ui.root, pet_event_cb, LV_EVENT_ALL, NULL);

  g_ui.bubble = lv_label_create(g_ui.root);
  if (g_ui.bubble == NULL)
    {
      pet_lvgl_destroy();
      return -1;
    }
  lv_label_set_text(g_ui.bubble, "");
  lv_label_set_long_mode(g_ui.bubble, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_ui.bubble, state.w);
  lv_obj_set_style_bg_color(g_ui.bubble, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(g_ui.bubble, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.bubble, lv_color_hex(INK), 0);
  lv_obj_set_style_pad_hor(g_ui.bubble, 8, 0);
  lv_obj_set_style_pad_ver(g_ui.bubble, 4, 0);
  lv_obj_set_style_radius(g_ui.bubble, 10, 0);
  lv_obj_set_style_text_align(g_ui.bubble, LV_TEXT_ALIGN_CENTER, 0);
  if (font_zh != NULL)
    {
      lv_obj_set_style_text_font(g_ui.bubble, font_zh, 0);
    }

  lv_obj_add_flag(g_ui.bubble, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_ui.bubble, LV_OBJ_FLAG_CLICKABLE);

  g_ui.body = make_blob(g_ui.root, BODY_BLUE, 40);
  g_ui.belly = make_blob(g_ui.root, BELLY, 24);
  g_ui.tail = make_blob(g_ui.root, BODY_DEEP, 12);
  g_ui.eye_l = make_blob(g_ui.root, 0xffffff, 8);
  g_ui.eye_r = make_blob(g_ui.root, 0xffffff, 8);
  g_ui.pupil_l = make_blob(g_ui.root, INK, 4);
  g_ui.pupil_r = make_blob(g_ui.root, INK, 4);
  g_ui.blush_l = make_blob(g_ui.root, BLUSH, 4);
  g_ui.blush_r = make_blob(g_ui.root, BLUSH, 4);
  g_ui.mouth = make_blob(g_ui.root, 0xd46a7a, 4);
  if (g_ui.bubble == NULL || g_ui.body == NULL || g_ui.belly == NULL ||
      g_ui.tail == NULL || g_ui.eye_l == NULL || g_ui.eye_r == NULL ||
      g_ui.pupil_l == NULL || g_ui.pupil_r == NULL ||
      g_ui.blush_l == NULL || g_ui.blush_r == NULL || g_ui.mouth == NULL)
    {
      pet_lvgl_destroy();
      return -1;
    }

  g_ui.timer = lv_timer_create(pet_timer_cb, 50, NULL);
  if (g_ui.timer == NULL)
    {
      pet_lvgl_destroy();
      return -1;
    }

  g_ui.open = true;
  pet_lvgl_sync();
  return 0;
}

void pet_lvgl_sync(void)
{
  struct pet_state_s state;
  int jump;

  if (!g_ui.open || g_ui.root == NULL)
    {
      return;
    }

  pet_engine_get(&state);
  if (!state.visible)
    {
      lv_obj_add_flag(g_ui.root, LV_OBJ_FLAG_HIDDEN);
      return;
    }

  lv_obj_remove_flag(g_ui.root, LV_OBJ_FLAG_HIDDEN);
  layout_parts(&state);
  jump = state.jump_t > 0 ? (state.jump_t / 30) : 0;
  lv_obj_set_pos(g_ui.root, state.x, state.y - BUBBLE_H - jump);
  if (state.bubble[0] != '\0')
    {
      lv_label_set_text(g_ui.bubble, state.bubble);
      lv_obj_set_style_text_color(g_ui.bubble,
                                  lv_color_hex(state.bubble_inner ?
                                               0x7d7d8a : INK), 0);
      lv_obj_remove_flag(g_ui.bubble, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(g_ui.bubble, LV_ALIGN_TOP_MID, 0, 0);
    }
  else
    {
      lv_obj_add_flag(g_ui.bubble, LV_OBJ_FLAG_HIDDEN);
    }
}

void pet_lvgl_destroy(void)
{
  if (g_ui.timer != NULL)
    {
      lv_timer_delete(g_ui.timer);
      g_ui.timer = NULL;
    }

  if (g_ui.root != NULL)
    {
      lv_obj_delete(g_ui.root);
      g_ui.root = NULL;
    }

  memset(&g_ui, 0, sizeof(g_ui));
}

bool pet_lvgl_is_open(void)
{
  return g_ui.open && g_ui.root != NULL;
}
