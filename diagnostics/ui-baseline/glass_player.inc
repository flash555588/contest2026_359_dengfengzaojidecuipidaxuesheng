/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_player.h"
static lv_obj_t *g_music_path, *g_music_status, *g_music_play;
static lv_obj_t *g_music_pause, *g_music_stop, *g_music_volume, *g_music_keyboard;
static lv_timer_t *g_music_timer;
static struct glass_player_status g_music_state;
static bool g_music_valid;

static void glass_music_refresh(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_music_status) return;
  struct glass_player_status state;
  glass_player_read(&state);
  if (g_music_valid && state.busy == g_music_state.busy &&
      state.error == g_music_state.error && state.state == g_music_state.state &&
      state.volume == g_music_state.volume) return;
  g_music_state = state;
  g_music_valid = true;
  char text[64];
  if (state.error) snprintf(text, sizeof(text), "播放操作失败 (%d)", state.error);
  else snprintf(text, sizeof(text), "%s", state.busy ? "处理中" :
                state.state == 1 ? "播放中" : state.state == 2 ? "已暂停" : "已停止");
  lv_label_set_text(g_music_status, text);
  lv_label_set_text(lv_obj_get_child(g_music_pause, 0),
                    state.state == 2 ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
  lv_obj_t *controls[] = {g_music_path, g_music_play, g_music_pause,
                         g_music_stop, g_music_volume, g_music_keyboard};
  for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); i++)
    {
      if (state.busy) lv_obj_add_state(controls[i], LV_STATE_DISABLED);
      else lv_obj_remove_state(controls[i], LV_STATE_DISABLED);
    }
  if (!state.state)
    {
      lv_obj_add_state(g_music_pause, LV_STATE_DISABLED);
      lv_obj_add_state(g_music_stop, LV_STATE_DISABLED);
    }
  if (!lv_obj_has_state(g_music_volume, LV_STATE_PRESSED))
    lv_slider_set_value(g_music_volume, state.volume, LV_ANIM_OFF);
}

static void glass_music_command(lv_event_t *event)
{
  enum glass_player_command command = (uintptr_t)lv_event_get_user_data(event);
  struct glass_player_status state;
  glass_player_read(&state);
  if (command == GLASS_PAUSE && state.state == 2) command = GLASS_RESUME;
  unsigned volume = lv_slider_get_value(g_music_volume);
  int ret = glass_player_submit(command, command == GLASS_PLAY ?
                                lv_textarea_get_text(g_music_path) : NULL, volume);
  g_music_valid = false;
  glass_music_refresh(NULL);
  if (ret)
    {
      char text[64];
      snprintf(text, sizeof(text), "请求失败 (%d)", ret);
      lv_label_set_text(g_music_status, text);
    }
}

static void glass_music_deleted(lv_event_t *event)
{
  LV_UNUSED(event);
  if (g_music_timer) lv_timer_delete(g_music_timer);
  g_music_timer = NULL;
  g_music_path = g_music_status = g_music_play = g_music_pause = NULL;
  g_music_stop = g_music_volume = g_music_keyboard = NULL;
  g_music_valid = false;
}

static void glass_music_player(lv_obj_t *card)
{
  lv_obj_add_event_cb(card, glass_music_deleted, LV_EVENT_DELETE, NULL);
  g_music_status = glass_text(card, "已停止", 24, 70, 800, 20, theme_primary());
  g_music_path = lv_textarea_create(card);
  lv_obj_set_pos(g_music_path, 24, 110);
  lv_obj_set_size(g_music_path, 800, 46);
  lv_textarea_set_one_line(g_music_path, true);
  lv_textarea_set_max_length(g_music_path, 255);
  lv_textarea_set_placeholder_text(g_music_path, "/data/music/track.wav");
  g_music_play = glass_button(card, "", 24, 172, 64,
                              theme_surface(), glass_music_command, (void *)GLASS_PLAY);
  g_music_pause = glass_button(card, "", 100, 172, 64,
                               theme_surface(), glass_music_command, (void *)GLASS_PAUSE);
  g_music_stop = glass_button(card, "", 176, 172, 64,
                              theme_surface(), glass_music_command, (void *)GLASS_STOP);
  lv_obj_t *volume_icon = glass_text(card, "", 270, 182, 36, 20, theme_primary());
  lv_obj_set_style_text_font(volume_icon, LV_FONT_DEFAULT, 0);
  lv_label_set_text(volume_icon, LV_SYMBOL_VOLUME_MAX);
  lv_obj_t *icon_buttons[] = {g_music_play, g_music_pause, g_music_stop};
  for (unsigned i = 0; i < 3; i++)
    lv_obj_set_style_text_font(lv_obj_get_child(icon_buttons[i], 0), LV_FONT_DEFAULT, 0);
  lv_label_set_text(lv_obj_get_child(g_music_play, 0), LV_SYMBOL_PLAY);
  lv_label_set_text(lv_obj_get_child(g_music_stop, 0), LV_SYMBOL_STOP);
  g_music_volume = lv_slider_create(card);
  lv_obj_set_pos(g_music_volume, 325, 186);
  lv_obj_set_size(g_music_volume, 470, 16);
  lv_slider_set_range(g_music_volume, 0, 100);
  lv_obj_add_event_cb(g_music_volume, glass_music_command, LV_EVENT_RELEASED,
                      (void *)GLASS_VOLUME);
  g_music_keyboard = lv_keyboard_create(card);
  lv_obj_set_pos(g_music_keyboard, 24, 242);
  lv_obj_set_size(g_music_keyboard, 800, 210);
  lv_keyboard_set_textarea(g_music_keyboard, g_music_path);
  g_music_valid = false;
  g_music_timer = lv_timer_create(glass_music_refresh, 100, NULL);
  glass_music_refresh(NULL);
  if (!g_music_timer)
    {
      lv_label_set_text(g_music_status, "状态刷新启动失败");
      lv_obj_add_state(g_music_play, LV_STATE_DISABLED);
      lv_obj_add_state(g_music_pause, LV_STATE_DISABLED);
      lv_obj_add_state(g_music_stop, LV_STATE_DISABLED);
      lv_obj_add_state(g_music_volume, LV_STATE_DISABLED);
    }
}
