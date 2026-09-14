/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_voice.h"

static struct {
  lv_obj_t *card, *status, *heard, *answer, *start, *finish, *cancel, *record, *level;
  lv_timer_t *timer;
  struct claw_voice_snapshot view;
  enum claw_voice_state previous;
} g_voice_ui;

static void glass_voice_tick(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_voice_ui.card) return;
  claw_voice_read(&g_voice_ui.view);
  struct claw_voice_snapshot *s = &g_voice_ui.view;
  lv_bar_set_value(g_voice_ui.level, s->peak * 100 / 32768, LV_ANIM_OFF);
  if (s->state == CLAW_VOICE_RECORDING)
    lv_label_set_text_fmt(g_voice_ui.status, "正在录音 · %u.%u 秒", s->milliseconds / 1000,
                         s->milliseconds % 1000 / 100);
  else if (s->state == CLAW_VOICE_STOPPING)
    lv_label_set_text(g_voice_ui.status, "正在取消，请稍候");
  else if (s->state == CLAW_VOICE_IDLE)
    lv_label_set_text(g_voice_ui.status, "点击开始说话，每次最多 15 秒");
  else if (s->error)
    lv_label_set_text_fmt(g_voice_ui.status, "%s (%d)", s->detail, s->error);
  else
    lv_label_set_text(g_voice_ui.status, s->detail);
  if (s->state != g_voice_ui.previous) {
    lv_label_set_text(g_voice_ui.heard, *s->transcript ? s->transcript : "识别文字将在这里显示");
    lv_label_set_text(g_voice_ui.answer, *s->answer ? s->answer : "回答将在这里显示");
    g_voice_ui.previous = s->state;
  }
  if (s->busy) {
    lv_obj_add_state(g_voice_ui.start, LV_STATE_DISABLED);
    lv_obj_add_state(g_voice_ui.record, LV_STATE_DISABLED);
    lv_obj_remove_state(g_voice_ui.cancel, LV_STATE_DISABLED);
  } else {
    lv_obj_remove_state(g_voice_ui.start, LV_STATE_DISABLED);
    lv_obj_remove_state(g_voice_ui.record, LV_STATE_DISABLED);
    lv_obj_add_state(g_voice_ui.cancel, LV_STATE_DISABLED);
  }
  if (s->state == CLAW_VOICE_RECORDING)
    lv_obj_remove_state(g_voice_ui.finish, LV_STATE_DISABLED);
  else lv_obj_add_state(g_voice_ui.finish, LV_STATE_DISABLED);
}

static void glass_voice_action(lv_event_t *e)
{
  int action = (int)(intptr_t)lv_event_get_user_data(e);
  if (action == 1) claw_voice_finish_recording();
  else if (action == 2) claw_voice_cancel();
  else {
    int ret = claw_voice_start(action == 3 ? 5 : 15, action != 3);
    if (ret) lv_label_set_text_fmt(g_voice_ui.status, "暂时无法开始 (%d)", ret);
  }
  glass_voice_tick(NULL);
}

static void glass_voice_deleted(lv_event_t *e)
{
  if (lv_event_get_target(e) != g_voice_ui.card) return;
  claw_voice_cancel();
  if (g_voice_ui.timer) lv_timer_delete(g_voice_ui.timer);
  memset(&g_voice_ui, 0, sizeof(g_voice_ui));
}

static void glass_voice(lv_event_t *e)
{
  LV_UNUSED(e);
  lv_obj_t *card = panel_card("ESP-Claw 语音助手");
  if (!card) return;
  g_voice_ui.card = card;
  g_voice_ui.previous = (enum claw_voice_state)-1;
  lv_obj_add_event_cb(card, glass_voice_deleted, LV_EVENT_DELETE, NULL);
  g_voice_ui.status = glass_text(card, "准备中", 24, 72, 860, 16, theme_secondary());
  g_voice_ui.level = lv_bar_create(card);
  lv_obj_set_pos(g_voice_ui.level, 24, 106);
  lv_obj_set_size(g_voice_ui.level, 860, 8);
  lv_bar_set_range(g_voice_ui.level, 0, 100);
  lv_obj_t *heard = glass_box(card, 24, 128, 414, 210, theme_surface(), 12);
  lv_obj_add_flag(heard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(heard, LV_DIR_VER);
  glass_text(heard, "你说的话", 14, 10, 372, 20, theme_primary());
  g_voice_ui.heard = glass_text(heard, "", 14, 48, 372, 20, theme_primary());
  lv_obj_t *answer = glass_box(card, 454, 128, 430, 210, theme_surface(), 12);
  lv_obj_add_flag(answer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(answer, LV_DIR_VER);
  glass_text(answer, "ESP-Claw 的回答", 14, 10, 388, 20, theme_primary());
  g_voice_ui.answer = glass_text(answer, "", 14, 48, 388, 20, theme_primary());
  glass_text(card, "开始说话会将本次录音发送给已配置的识别服务；关闭页面会取消。",
             24, 352, 860, 16, theme_secondary());
  g_voice_ui.start = glass_button(card, "开始说话", 24, 394, 204, theme_accent(),
                                  glass_voice_action, (void *)0);
  g_voice_ui.finish = glass_button(card, "说完了", 240, 394, 204, theme_surface(),
                                   glass_voice_action, (void *)1);
  g_voice_ui.cancel = glass_button(card, "取消", 456, 394, 204, theme_surface(),
                                   glass_voice_action, (void *)2);
  g_voice_ui.record = glass_button(card, "录音测试", 672, 394, 212, theme_surface(),
                                   glass_voice_action, (void *)3);
  g_voice_ui.timer = lv_timer_create(glass_voice_tick, 150, NULL);
  glass_voice_tick(NULL);
}
