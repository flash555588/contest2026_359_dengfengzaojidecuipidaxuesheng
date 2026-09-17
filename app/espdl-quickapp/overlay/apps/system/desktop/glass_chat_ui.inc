/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_chat.h"
#include "glass_ime.h"
#include "glass_qpk_builder.h"

static struct {
  lv_obj_t *root, *messages, *status, *model, *count, *storage, *draft_label, *action, *action_label;
  lv_obj_t *editor, *input, *keyboard, *counter, *confirm, *waiting;
  lv_obj_t *app_status, *preview_button, *save_button, *discard_button;
  bool discard_requested;
  lv_obj_t *live_bubble, *live_reasoning;
  lv_obj_t *live_parts[24];
  unsigned live_count;
  int live_y;
  unsigned drawn_revision;
  uint32_t drawn_id;
  unsigned drawn_count;
  enum chat_turn_state drawn_state;
  unsigned rebuilds, live_updates;
  uint32_t refresh_max_ms;
  lv_obj_t *archive_panel, *archive_content, *archive_status;
  lv_obj_t *archive_previous, *archive_next;
  struct chat_page *archive_page;
  char archive_name[48];
  struct qpk_draft_info app;
  lv_timer_t *timer;
  struct chat_snapshot *snapshot;
  unsigned ticks;
  uint32_t began;
  uint32_t background, surface, sidebar, primary, secondary, accent;
} g_chat_ui;
static char chat_draft[CHAT_PROMPT_BYTES + 1];
static bool chat_portal_return_apps;
static void glass_chat_open(lv_event_t *e);
static void glass_chat_show(bool from_apps);
static void glass_chat_back(lv_event_t *e);
static void chat_ui_refresh(lv_timer_t *timer);
static void chat_ui_editor_open(lv_event_t *e);
static void chat_ui_config(lv_event_t *e);
static void chat_ui_discard(lv_event_t *e);
static lv_obj_t *chat_ui_text(lv_obj_t *, const char *, int, int, int, int, uint32_t, bool);

static void chat_ui_label(lv_obj_t *label, const char *text)
{ if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text); }

/* Small labels let LVGL discard off-screen text quickly instead of measuring
 * thousands of glyphs on every scroll or delta. Existing chunks stay intact. */
static int chat_ui_chunks(lv_obj_t *bubble, const char *text, int width, uint32_t color, bool live)
{
  int y = 16; unsigned index = 0;
  while (*text || (!index && live)) {
    char chunk[385]; size_t bytes = glass_chat_copy_utf8(chunk, sizeof(chunk), text);
    lv_obj_t *label = live && index < g_chat_ui.live_count ? g_chat_ui.live_parts[index] : NULL;
    bool changed = !label || strcmp(lv_label_get_text(label), chunk);
    if (!label) {
      label = chat_ui_text(bubble, chunk, 18, y, width - 36, 20, color, true);
      if (live && index < 24) { g_chat_ui.live_parts[index] = label; g_chat_ui.live_count = index + 1; }
    } else chat_ui_label(label, chunk);
    if (lv_obj_get_y(label) != y) lv_obj_set_y(label, y);
    if (changed) lv_obj_update_layout(label);
    y += lv_obj_get_height(label) + 6;
    text += bytes; index++;
    if (!bytes) break;
  }
  if (live) while (g_chat_ui.live_count > index) lv_obj_delete(g_chat_ui.live_parts[--g_chat_ui.live_count]);
  int height = y + 10; if (height < 60) height = 60;
  lv_obj_set_height(bubble, height); return height;
}

static void chat_qpk_preview_back(void)
{
  bool origin = chat_qpk_preview_origin;
  chat_qpk_preview_finish();
  glass_chat_show(origin);
}

static void chat_ui_preview_app(lv_event_t *e)
{
  LV_UNUSED(e);
  if (!g_chat_ui.root || g_chat_ui.snapshot->phase != CHAT_IDLE) return;
  struct qpk_draft_info draft;
  char *source = glass_qpk_preview_source(&draft);
  if (!source) { show_toast("请先让 AI 生成草稿，或等待保存结束"); return; }
  bool origin = g_desktop.chat_return_apps;
  char title[96], package[48];
  snprintf(title, sizeof(title), "预览 · %s", draft.name);
  snprintf(package, sizeof(package), "preview.%s", draft.slug);
  lv_obj_t *card = qapp_page_prepare(title);
  if (!card) { free(source); show_toast("内存不足，无法预览"); return; }
  int ret = qpk_runtime_launch(card, draft.name, package, "draft", "app.js", source, strlen(source),
                               zh_font, show_toast, qpk_show_dialog);
  free(source);
  const char *error = qpk_runtime_last_error();
  if (ret || error[0] || !qpk_runtime_widget_count()) {
    glass_qpk_preview_result(draft.revision, error[0] ? error : ret ? "预览启动失败，请检查脚本或可用内存。" : "脚本未创建任何可见控件。" );
    qpk_runtime_stop(); lv_obj_delete(lv_obj_get_parent(card));
    show_toast("预览失败，返回对话让 AI 根据错误修改"); return;
  }
  glass_qpk_preview_result(draft.revision, "");
  g_desktop.chat_page = false; g_desktop.chat_return_apps = false;
  qapp_page_commit(card);
  chat_qpk_preview_active = true; chat_qpk_preview_origin = origin;
  chat_qpk_preview_revision = draft.revision;
  show_toast("试用后点左上角返回，可以继续修改或保存应用");
}

static void chat_ui_save_app(lv_event_t *e)
{
  LV_UNUSED(e);
  if (!g_chat_ui.root || g_chat_ui.snapshot->phase != CHAT_IDLE) return;
  int ret = glass_qpk_save_async(g_chat_ui.app.revision);
  show_toast(ret ? glass_qpk_error_text(ret) : "正在保存应用…");
  chat_ui_refresh(NULL);
}

static lv_obj_t *chat_ui_text(lv_obj_t *parent, const char *text, int x, int y,
                              int width, int size, uint32_t color, bool wrap)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y); lv_obj_set_width(label, width);
  lv_obj_set_style_text_font(label, zh_font(size), 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_label_set_long_mode(label, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
  if (wrap) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_line_space(label, 6, 0);
  }
  lv_label_set_text(label, text);
  return label;
}

static lv_obj_t *chat_ui_button(lv_obj_t *parent, const char *text, int x, int y,
                                int width, int height, bool filled, lv_event_cb_t cb, void *data)
{
  lv_obj_t *button = glass_box(parent, x, y, width, height,
                               filled ? g_chat_ui.accent : g_chat_ui.surface, 12);
  glass_clickable(button, cb, data);
  lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_t *label = chat_ui_text(button, text, 0, 0, width - 20, 16,
                                filled ? 0xffffff : g_chat_ui.primary, false);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(label);
  return button;
}

static bool chat_ui_busy(void)
{ return g_chat_ui.snapshot->phase != CHAT_IDLE; }

static void chat_ui_editor_close(lv_event_t *e)
{
  LV_UNUSED(e);
  if (!g_chat_ui.editor) return;
  glass_ime_commit(g_chat_ui.keyboard);
  glass_chat_copy_utf8(chat_draft, sizeof(chat_draft), lv_textarea_get_text(g_chat_ui.input));
  lv_obj_delete(g_chat_ui.editor);
  g_chat_ui.editor = g_chat_ui.input = g_chat_ui.keyboard = g_chat_ui.counter = NULL;
  lv_label_set_text(g_chat_ui.draft_label, chat_draft[0] ? chat_draft : "聊点什么…");
}

static void chat_ui_send(lv_event_t *e)
{
  LV_UNUSED(e);
  struct qpk_draft_info app; glass_qpk_get(&app);
  if (app.discarding || g_chat_ui.discard_requested) { show_toast("正在放弃创意，请稍候"); return; }
  const char *text = chat_draft;
  if (g_chat_ui.editor) { glass_ime_commit(g_chat_ui.keyboard); text = lv_textarea_get_text(g_chat_ui.input); }
  if (strlen(text) > CHAT_PROMPT_BYTES) { show_toast("消息太长，请删减少量文字后再试"); return; }
  int ret = glass_chat_send(text);
  if (ret) {
    show_toast(ret == -ENOKEY ? "先扫码完成 AI 配置" : ret == -EBUSY ? "请等待当前回复结束" : "请输入有效的消息");
    return;
  }
  if (g_chat_ui.editor) {
    lv_obj_delete(g_chat_ui.editor);
    g_chat_ui.editor = g_chat_ui.input = g_chat_ui.keyboard = g_chat_ui.counter = NULL;
  }
  chat_draft[0] = 0; g_chat_ui.began = lv_tick_get();
  lv_label_set_text(g_chat_ui.draft_label, "聊点什么…"); chat_ui_refresh(NULL);
}

static void chat_ui_action(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_chat_ui.snapshot->phase == CHAT_REQUESTING || g_chat_ui.snapshot->phase == CHAT_QUEUED) {
    glass_chat_cancel(); chat_ui_refresh(NULL);
  } else if (chat_ui_busy()) return;
  else if (!g_chat_ui.snapshot->configured) chat_ui_config(NULL);
  else if (chat_draft[0]) chat_ui_send(NULL);
  else chat_ui_editor_open(NULL);
}

static void chat_ui_counter(lv_event_t *e)
{
  LV_UNUSED(e);
  if (!g_chat_ui.counter || !g_chat_ui.input) return;
  unsigned length = 0;
  for (const unsigned char *p = (const unsigned char *)lv_textarea_get_text(g_chat_ui.input); *p; p++)
    if ((*p & 0xc0) != 0x80) length++;
  lv_label_set_text_fmt(g_chat_ui.counter, "%u / 512", length);
}

static void chat_ui_editor_open(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_chat_ui.editor) return;
  lv_obj_t *editor = glass_box(g_chat_ui.root, 220, 0, 804, 600, g_chat_ui.background, 0);
  g_chat_ui.editor = editor;
  chat_ui_text(editor, "写下你的想法", 26, 28, 430, 28, g_chat_ui.primary, false);
  chat_ui_button(editor, "收起", 572, 23, 88, 46, false, chat_ui_editor_close, NULL);
  chat_ui_button(editor, "发送", 674, 23, 102, 46, true, chat_ui_send, NULL);
  g_chat_ui.input = lv_textarea_create(editor);
  glass_input_style(g_chat_ui.input);
  lv_obj_set_pos(g_chat_ui.input, 26, 92); lv_obj_set_size(g_chat_ui.input, 752, 148);
  lv_obj_set_style_bg_color(g_chat_ui.input, lv_color_hex(g_chat_ui.surface), 0);
  lv_obj_set_style_text_color(g_chat_ui.input, lv_color_hex(g_chat_ui.primary), 0);
  lv_obj_set_style_border_color(g_chat_ui.input, lv_color_hex(g_chat_ui.accent), LV_STATE_FOCUSED);
  lv_obj_set_style_text_line_space(g_chat_ui.input, 5, 0);
  lv_textarea_set_max_length(g_chat_ui.input, 512);
  lv_textarea_set_placeholder_text(g_chat_ui.input, "输入问题，也可以换行补充细节");
  lv_textarea_set_text(g_chat_ui.input, chat_draft);
  lv_textarea_set_cursor_pos(g_chat_ui.input, LV_TEXTAREA_CURSOR_LAST);
  chat_ui_text(editor, "中文 / English · 连续拼音选词 · 空格确认", 28, 254, 556, 16, g_chat_ui.secondary, false);
  g_chat_ui.counter = chat_ui_text(editor, "", 588, 254, 190, 16, g_chat_ui.secondary, false);
  lv_obj_set_style_text_align(g_chat_ui.counter, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_add_event_cb(g_chat_ui.input, chat_ui_counter, LV_EVENT_VALUE_CHANGED, NULL);
  g_chat_ui.keyboard = lv_keyboard_create(editor);
  glass_keyboard_style(g_chat_ui.keyboard);
  lv_obj_set_size(g_chat_ui.keyboard, 772, 244);
  /* lv_keyboard starts bottom-aligned; coordinates alone would move it
   * below the screen. Fix the alignment before attaching the candidate bar. */
  lv_obj_align(g_chat_ui.keyboard, LV_ALIGN_TOP_LEFT, 16, 340);
  lv_keyboard_set_mode(g_chat_ui.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(g_chat_ui.keyboard, g_chat_ui.input);
  glass_ime_attach(g_chat_ui.keyboard, zh_font(20), true,
                    g_chat_ui.primary, g_chat_ui.surface, g_chat_ui.accent);
  /* The keyboard's check mark closes the keyboard. Sending is explicit. */
  lv_obj_add_event_cb(g_chat_ui.keyboard, chat_ui_editor_close, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(g_chat_ui.keyboard, chat_ui_editor_close, LV_EVENT_CANCEL, NULL);
  chat_ui_counter(NULL);
}

static void chat_ui_suggestion(lv_event_t *e)
{
  snprintf(chat_draft, sizeof(chat_draft), "%s", (const char *)lv_event_get_user_data(e));
  lv_label_set_text(g_chat_ui.draft_label, chat_draft); chat_ui_editor_open(NULL);
}

static void chat_ui_retry(lv_event_t *e)
{
  LV_UNUSED(e);
  int ret = glass_chat_retry();
  if (ret) show_toast(ret == -ENOKEY ? "先扫码完成 AI 配置" : "请等待当前操作结束");
  else g_chat_ui.began = lv_tick_get();
  chat_ui_refresh(NULL);
}

static void chat_ui_archive_close(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_chat_ui.archive_panel) lv_obj_delete(g_chat_ui.archive_panel);
  g_chat_ui.archive_panel = g_chat_ui.archive_content = g_chat_ui.archive_status = NULL;
  g_chat_ui.archive_previous = g_chat_ui.archive_next = NULL;
  free(g_chat_ui.archive_page); g_chat_ui.archive_page = NULL;
}
static void chat_ui_archive_step(lv_event_t *e)
{
  struct chat_page *p = g_chat_ui.archive_page;
  if (!p || p->busy) return;
  int page = (int)p->page + (int)(intptr_t)lv_event_get_user_data(e);
  if (page < 0 || page >= (int)p->pages) return;
  if (glass_chat_page_request(g_chat_ui.archive_name, page)) show_toast("正在读取，请稍候");
  else {
    p->busy = true;
    glass_pager_enabled(g_chat_ui.archive_previous, false, g_chat_ui.primary);
    glass_pager_enabled(g_chat_ui.archive_next, false, g_chat_ui.primary);
  }
}
static void chat_ui_archive_open(lv_event_t *e)
{
  const char *name = lv_event_get_user_data(e);
  if (!name || g_chat_ui.archive_panel) return;
  struct chat_page *p = calloc(1, sizeof(*p));
  if (!p) { show_toast("内存不足"); return; }
  int error = glass_chat_page_request(name, 0);
  if (error) { free(p); show_toast("暂时无法读取完整回复"); return; }
  g_chat_ui.archive_page = p; snprintf(g_chat_ui.archive_name, sizeof(g_chat_ui.archive_name), "%s", name);
  lv_obj_t *panel = glass_box(g_chat_ui.root, 244, 86, 760, 496, g_chat_ui.background, 16);
  g_chat_ui.archive_panel = panel;
  g_chat_ui.archive_status = chat_ui_text(panel, "正在读取完整回复…", 20, 14, 680, 18, g_chat_ui.primary, false);
  g_chat_ui.archive_content = glass_box(panel, 8, 48, 744, 380, g_chat_ui.surface, 0);
  lv_obj_add_flag(g_chat_ui.archive_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_chat_ui.archive_content, LV_DIR_VER);
  g_chat_ui.archive_previous = chat_ui_button(panel, "上一页", 16, 440, 134, 42, false, chat_ui_archive_step, (void *)(intptr_t)-1);
  g_chat_ui.archive_next = chat_ui_button(panel, "下一页", 166, 440, 134, 42, false, chat_ui_archive_step, (void *)(intptr_t)1);
  glass_pager_enabled(g_chat_ui.archive_previous, false, g_chat_ui.primary);
  glass_pager_enabled(g_chat_ui.archive_next, false, g_chat_ui.primary);
  chat_ui_button(panel, "返回对话", 574, 440, 168, 42, true, chat_ui_archive_close, NULL);
}

static int chat_ui_bubble(const char *text, int y, bool user)
{
  int width = user ? 580 : 706, x = user ? 164 : 8;
  chat_ui_text(g_chat_ui.messages, user ? "你" : "ESPClaw", x + 4, y, width - 8, 16, g_chat_ui.secondary, false);
  lv_obj_t *bubble = glass_box(g_chat_ui.messages, x, y + 28, width, 60,
                               user ? g_chat_ui.accent : g_chat_ui.surface, 16);
  lv_obj_remove_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
  int height = chat_ui_chunks(bubble, text, width, user ? 0xffffff : g_chat_ui.primary, false);
  return y + 28 + height + 22;
}

static void chat_ui_messages(void)
{
  g_chat_ui.rebuilds++;
  struct chat_snapshot *s = g_chat_ui.snapshot;
  int scroll = lv_obj_get_scroll_y(g_chat_ui.messages);
  bool follow = lv_obj_get_scroll_bottom(g_chat_ui.messages) < 48;
  lv_obj_clean(g_chat_ui.messages); g_chat_ui.waiting = g_chat_ui.live_bubble = g_chat_ui.live_reasoning = NULL;
  g_chat_ui.live_count = 0;
  if (!s->count) {
    lv_obj_t *mark = glass_box(g_chat_ui.messages, 8, 18, 56, 56, g_chat_ui.accent, 18);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    glass_symbol(mark, LV_SYMBOL_EDIT, 16, 16, 0xffffff);
    chat_ui_text(g_chat_ui.messages, "有什么想聊的？", 8, 94, 700, 28, g_chat_ui.primary, false);
    chat_ui_text(g_chat_ui.messages, s->configured ? "聊想法、写文字，一起把问题想明白。" : "先连接你的 AI 服务，让灵感从这里开始。",
                  8, 144, 700, 20, g_chat_ui.secondary, false);
    if (!s->configured) chat_ui_button(g_chat_ui.messages, "扫码配置 AI", 8, 202, 178, 48, true, chat_ui_config, NULL);
    const char *titles[] = {"制作计数器", "制作计时器", "自由聊聊"};
    const char *prompts[] = {"做一个大数字计数器快应用，有加一、减一和清零，退出后保留计数。请先生成草稿让我预览。", "做一个可以开始、暂停和重置的倒计时快应用，先生成草稿让我预览。", "帮我安排今天的学习计划。"};
    int y = s->configured ? 212 : 280;
    for (unsigned i = 0; i < 3; i++) {
      lv_obj_t *tile = chat_ui_button(g_chat_ui.messages, titles[i], 8 + i * 246, y, 232, 70,
                                      false, chat_ui_suggestion, (void *)prompts[i]);
      lv_obj_set_style_border_width(tile, 1, 0);
      lv_obj_set_style_border_color(tile, lv_color_hex(g_chat_ui.secondary), 0);
      lv_obj_set_style_border_opa(tile, 24, 0);
    }
  } else {
    int y = 4;
    for (unsigned i = 0; i < s->count; i++) {
      const struct chat_turn *t = &s->turns[i];
      y = chat_ui_bubble(t->prompt, y, true);
      if (t->state == CHAT_DONE) {
        y = chat_ui_bubble(t->reply, y, false);
        if (t->clipped) {
          if (t->archive[0]) {
            chat_ui_button(g_chat_ui.messages, "阅读完整回复", 8, y, 188, 42, false, chat_ui_archive_open, (void *)t->archive);
            y += 52;
          } else {
            chat_ui_text(g_chat_ui.messages, "完整回复未能保存；当前为已保留的片段，请检查存储空间。", 12, y, 704, 16, g_chat_ui.secondary, true);
            y += 58;
          }
        }
        if (t->output_limited) { chat_ui_text(g_chat_ui.messages, "已达到本次输出 Token 上限，可调整设置后继续。", 12, y, 704, 16, g_chat_ui.secondary, true); y += 38; }
      } else if (t->state == CHAT_WAITING) {
        g_chat_ui.waiting = chat_ui_text(g_chat_ui.messages, "等待服务响应…", 12, y + 8, 700, 16, g_chat_ui.secondary, false);
        g_chat_ui.live_reasoning = chat_ui_text(g_chat_ui.messages, "", 12, y + 36, 700, 16, g_chat_ui.secondary, false);
        lv_obj_set_height(g_chat_ui.live_reasoning, 56);
        lv_label_set_long_mode(g_chat_ui.live_reasoning, LV_LABEL_LONG_DOT);
        g_chat_ui.live_y = y + 102;
        g_chat_ui.live_bubble = glass_box(g_chat_ui.messages, 8, g_chat_ui.live_y, 706, 60, g_chat_ui.surface, 16);
        lv_obj_remove_flag(g_chat_ui.live_bubble, LV_OBJ_FLAG_CLICKABLE);
        y = g_chat_ui.live_y + chat_ui_chunks(g_chat_ui.live_bubble, t->reply, 706, g_chat_ui.primary, true) + 22;
      } else {
        if (t->reply[0]) y = chat_ui_bubble(t->reply, y, false);
        if (t->archive[0]) { chat_ui_button(g_chat_ui.messages, "阅读已收到的内容", 8, y, 220, 42, false, chat_ui_archive_open, (void *)t->archive); y += 52; }
        const char *text = t->state == CHAT_CANCELLED ? "已停止这次回复。" : glass_chat_error_text(t->error);
        y = chat_ui_bubble(text, y, false);
        if (i == s->count - 1) {
          chat_ui_button(g_chat_ui.messages, "重新发送", 8, y - 6, 128, 42, false, chat_ui_retry, NULL);
          y += 52;
        }
      }
    }
  }
  lv_obj_update_layout(g_chat_ui.messages);
  if (follow) lv_obj_scroll_to_y(g_chat_ui.messages, LV_COORD_MAX, LV_ANIM_OFF);
  else lv_obj_scroll_to_y(g_chat_ui.messages, scroll, LV_ANIM_OFF);
  g_chat_ui.drawn_revision = s->revision; g_chat_ui.drawn_count = s->count;
  g_chat_ui.drawn_id = s->count ? s->turns[s->count - 1].id : 0;
  g_chat_ui.drawn_state = s->count ? s->turns[s->count - 1].state : CHAT_DONE;
}

static void chat_ui_live(void)
{
  g_chat_ui.live_updates++;
  struct chat_snapshot *s = g_chat_ui.snapshot;
  if (!s->count || !g_chat_ui.live_bubble) return;
  const struct chat_turn *t = &s->turns[s->count - 1];
  bool follow = lv_obj_get_scroll_bottom(g_chat_ui.messages) < 48;
  chat_ui_chunks(g_chat_ui.live_bubble, t->reply, 706, g_chat_ui.primary, true);
  const char *tail = t->reasoning; size_t size = strlen(tail);
  if (size > 240) { tail += size - 240; while ((*tail & 0xc0) == 0x80) tail++; }
  chat_ui_label(g_chat_ui.live_reasoning, tail);
  char progress[192];
  if (t->has_reasoning_tokens) snprintf(progress, sizeof(progress), "%s · 思考 %lu token · 已收 %lu 字节", t->progress, (unsigned long)t->reasoning_tokens, (unsigned long)t->text_bytes);
  else snprintf(progress, sizeof(progress), "%s · 思考 %lu 字节 · 已收 %lu 字节", t->progress, (unsigned long)t->reasoning_bytes, (unsigned long)t->text_bytes);
  chat_ui_label(g_chat_ui.waiting, progress);
  lv_obj_update_layout(g_chat_ui.messages);
  if (follow) lv_obj_scroll_to_y(g_chat_ui.messages, LV_COORD_MAX, LV_ANIM_OFF);
  g_chat_ui.drawn_revision = s->revision;
}

static void chat_ui_confirm_close(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_chat_ui.confirm) lv_obj_delete(g_chat_ui.confirm);
  g_chat_ui.confirm = NULL;
}

static void chat_ui_new_confirm(lv_event_t *e)
{
  LV_UNUSED(e);
  int ret = glass_chat_clear();
  if (ret) { show_toast("请等待对话载入后再试"); return; }
  if (g_chat_ui.editor) chat_ui_editor_close(NULL);
  chat_draft[0] = 0;
  lv_label_set_text(g_chat_ui.draft_label, "聊点什么…");
  chat_ui_confirm_close(NULL); chat_ui_refresh(NULL);
}

static void chat_ui_discard_confirm(lv_event_t *e)
{
  LV_UNUSED(e);
  glass_chat_cancel();
  int ret = glass_qpk_discard_async();
  if (ret) { show_toast(glass_qpk_error_text(ret)); return; }
  g_chat_ui.discard_requested = true;
  chat_ui_confirm_close(NULL);
  show_toast("正在放弃创意…");
  chat_ui_refresh(NULL);
}

static void chat_ui_discard(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_chat_ui.confirm) return;
  struct qpk_draft_info app; glass_qpk_get(&app);
  if (app.busy) { show_toast("正在处理草稿，请稍后重试"); return; }
  lv_obj_t *shade = glass_box(g_chat_ui.root, 0, 0, 1024, 600, 0x000000, 0);
  lv_obj_set_style_bg_opa(shade, LV_OPA_60, 0); g_chat_ui.confirm = shade;
  lv_obj_t *card = glass_box(shade, 252, 166, 520, 268, g_chat_ui.background, 22);
  chat_ui_text(card, "放弃这个创意？", 28, 26, 464, 28, g_chat_ui.primary, false);
  chat_ui_text(card, "停止本次生成并清除当前应用草稿。\n聊天记录和已安装的应用会保留。", 28, 84, 464, 20, g_chat_ui.secondary, true);
  chat_ui_button(card, "继续创作", 28, 196, 214, 46, false, chat_ui_confirm_close, NULL);
  chat_ui_button(card, "放弃创意", 258, 196, 234, 46, true, chat_ui_discard_confirm, NULL);
}

static void chat_ui_new(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_chat_ui.confirm) return;
  if (!g_chat_ui.snapshot->count) { chat_ui_editor_open(NULL); return; }
  lv_obj_t *shade = glass_box(g_chat_ui.root, 0, 0, 1024, 600, 0x000000, 0);
  lv_obj_set_style_bg_opa(shade, LV_OPA_60, 0); g_chat_ui.confirm = shade;
  lv_obj_t *card = glass_box(shade, 272, 178, 480, 242, g_chat_ui.background, 22);
  chat_ui_text(card, "开始新对话？", 28, 26, 424, 28, g_chat_ui.primary, false);
  chat_ui_text(card, "当前对话记录将被清空。\n正在等待的回复也会停止。", 28, 82, 424, 20, g_chat_ui.secondary, true);
  chat_ui_button(card, "保留对话", 28, 174, 186, 46, false, chat_ui_confirm_close, NULL);
  chat_ui_button(card, "清空并开始", 238, 174, 214, 46, true, chat_ui_new_confirm, NULL);
}

static void chat_ui_return_from_portal(lv_event_t *e)
{
  LV_UNUSED(e);
  glass_chat_show(chat_portal_return_apps);
}

static void chat_ui_config(lv_event_t *e)
{
  LV_UNUSED(e);
  chat_portal_return_apps = g_desktop.chat_return_apps;
  if (g_chat_ui.editor) chat_ui_editor_close(NULL);
  glass_portal_show(chat_ui_return_from_portal);
}

static void chat_ui_refresh(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_chat_ui.root) return;
  uint32_t refresh_started = lv_tick_get();
  if (g_chat_ui.archive_page && glass_chat_page_get(g_chat_ui.archive_page)) {
    struct chat_page *page = g_chat_ui.archive_page;
    bool ready = !page->busy && !page->error;
    glass_pager_enabled(g_chat_ui.archive_previous, ready && page->page > 0, g_chat_ui.primary);
    glass_pager_enabled(g_chat_ui.archive_next, ready && page->page + 1 < page->pages, g_chat_ui.primary);
    if (page->busy) chat_ui_label(g_chat_ui.archive_status, "正在读取完整回复…");
    else if (page->error) chat_ui_label(g_chat_ui.archive_status, "完整回复读取失败，请检查设备存储");
    else {
      char text[128]; snprintf(text, sizeof(text), "完整回复 · 第 %u / %u 页", page->page + 1, page->pages);
      chat_ui_label(g_chat_ui.archive_status, text);
      lv_obj_clean(g_chat_ui.archive_content);
      lv_obj_t *content = glass_box(g_chat_ui.archive_content, 0, 0, 730, 60, g_chat_ui.surface, 0);
      chat_ui_chunks(content, page->text, 730, g_chat_ui.primary, false);
      lv_obj_scroll_to_y(g_chat_ui.archive_content, 0, LV_ANIM_OFF);
    }
  }
  struct chat_snapshot *s = g_chat_ui.snapshot;
  int old_storage = s->storage_error; unsigned previous_count = s->count;
  uint32_t previous_id = s->count ? s->turns[s->count - 1].id : 0;
  bool update = glass_chat_get(s);
  struct qpk_draft_info app;
  glass_qpk_get(&app);
  if (g_chat_ui.discard_requested && !app.discarding) {
    g_chat_ui.discard_requested = false;
    if (!app.error) show_toast("已放弃创意，可以开始新的想法");
  }
  if (app.saved_revision && app.saved_revision != g_chat_ui.app.saved_revision)
    show_toast("应用已保存，可以从全部应用打开");
  if (app.error && app.error != g_chat_ui.app.error) show_toast(glass_qpk_error_text(app.error));
  g_chat_ui.app = app;
  char app_text[192];
  if (app.discarding) snprintf(app_text, sizeof(app_text), "正在放弃创意…");
  else if (app.busy && app.save_stage) snprintf(app_text, sizeof(app_text), "%s\n请稍候，完成后会提示", app.save_stage == 1 ? "正在检查保存位置" : app.save_stage == 2 ? "正在写入应用脚本" : "正在写入应用信息");
  else if (app.busy) snprintf(app_text, sizeof(app_text), "正在处理草稿…");
  else if (app.error) snprintf(app_text, sizeof(app_text), "草稿操作未完成\n可检查后重试");
  else if (app.name[0]) snprintf(app_text, sizeof(app_text), "%s · 第 %lu 版\n%s", app.name,
    (unsigned long)app.revision, app.saved_revision == app.revision ? "已保存到全部应用" :
    app.preview_error[0] ? "预览出错，可反馈修改" : app.previewed_revision == app.revision ? "已预览，可修改或保存" : "草稿已就绪，等待预览");
  else snprintf(app_text, sizeof(app_text), "描述想做的应用\nAI 会先生成预览草稿");
  chat_ui_label(g_chat_ui.app_status, app_text);
  bool can_preview = app.loaded && app.name[0] && !app.busy && s->phase == CHAT_IDLE;
  bool can_discard = app.loaded && !app.busy && (app.name[0] || s->phase == CHAT_REQUESTING || s->phase == CHAT_QUEUED);
  if (can_discard) lv_obj_remove_state(g_chat_ui.discard_button, LV_STATE_DISABLED);
  else lv_obj_add_state(g_chat_ui.discard_button, LV_STATE_DISABLED);
  if (can_preview) lv_obj_remove_state(g_chat_ui.preview_button, LV_STATE_DISABLED);
  else lv_obj_add_state(g_chat_ui.preview_button, LV_STATE_DISABLED);
  if (can_preview && app.previewed_revision == app.revision && app.saved_revision != app.revision)
    lv_obj_remove_state(g_chat_ui.save_button, LV_STATE_DISABLED);
  else lv_obj_add_state(g_chat_ui.save_button, LV_STATE_DISABLED);
  if (update) {
    if (s->storage_error && s->storage_error != old_storage) show_toast("对话记录未能保存，请检查设备存储");
    if (previous_count != s->count || (s->count && previous_id != s->turns[s->count - 1].id)) {
      lv_obj_scroll_to_y(g_chat_ui.messages, LV_COORD_MAX, LV_ANIM_OFF);
      g_chat_ui.began = lv_tick_get();
    }
    chat_ui_label(g_chat_ui.model, s->model[0] ? s->model : "选择你的 AI 服务，开始第一段对话");
    char count_text[48]; snprintf(count_text, sizeof(count_text), "%u 轮交流", s->count); chat_ui_label(g_chat_ui.count, count_text);
    chat_ui_label(g_chat_ui.storage,
      s->storage_error == -EINVAL ? "历史记录无法读取\n原文件已保留" :
      s->storage_error == -ENOSPC ? "存储空间不足\n请清理设备文件" :
      s->storage_error ? "记录暂未保存\n可继续聊天后重试" : "最近 6 轮对话\n自动保存在设备");
    chat_ui_label(g_chat_ui.action_label, s->phase == CHAT_REQUESTING || s->phase == CHAT_QUEUED ? "停止" :
                       s->phase == CHAT_STOPPING ? "停止中" : s->phase == CHAT_CLEARING ? "请稍候" :
                       !s->configured ? "去配置" : "发送");
    if (s->phase == CHAT_LOADING || s->phase == CHAT_CLEARING || s->phase == CHAT_STOPPING)
      lv_obj_add_state(g_chat_ui.action, LV_STATE_DISABLED);
    else lv_obj_remove_state(g_chat_ui.action, LV_STATE_DISABLED);
  }
  /* Receiving continues in the worker while the user scrolls. */
  if (s->revision != g_chat_ui.drawn_revision && !lv_obj_is_scrolling(g_chat_ui.messages)) {
    const struct chat_turn *last = s->count ? &s->turns[s->count - 1] : NULL;
    if (last && g_chat_ui.drawn_count == s->count && g_chat_ui.drawn_id == last->id &&
        g_chat_ui.drawn_state == CHAT_WAITING && last->state == CHAT_WAITING) chat_ui_live();
    else chat_ui_messages();
  }
  const char *status = !s->loaded ? "正在载入对话" : s->phase == CHAT_CLEARING ? "正在新建对话" :
    s->phase == CHAT_STOPPING ? "正在停止回复" : s->phase == CHAT_REQUESTING || s->phase == CHAT_QUEUED ? "正在思考" :
    s->configured ? "随时可以开始" : "需要配置 AI 服务";
  if (s->phase == CHAT_REQUESTING || s->phase == CHAT_QUEUED) {
    unsigned seconds = (lv_tick_get() - g_chat_ui.began) / 1000;
    char text[160];
    const struct chat_turn *t = s->count ? &s->turns[s->count - 1] : NULL;
    snprintf(text, sizeof(text), "%s · %u 秒", t && t->progress[0] ? t->progress : status, seconds);
    chat_ui_label(g_chat_ui.status, text);
  } else {
    chat_ui_label(g_chat_ui.status, status);
    if (g_chat_ui.waiting && s->phase != CHAT_IDLE) chat_ui_label(g_chat_ui.waiting, s->phase == CHAT_CLEARING ? "正在开始新对话…" : "正在停止回复…");
  }
  if (++g_chat_ui.ticks % 20 == 0 && s->phase == CHAT_IDLE) glass_chat_refresh_config();
  uint32_t elapsed = lv_tick_get() - refresh_started;
  if (elapsed > g_chat_ui.refresh_max_ms) g_chat_ui.refresh_max_ms = elapsed;
}

static void chat_ui_deleted(lv_event_t *e)
{
  LV_UNUSED(e);
  free(g_chat_ui.archive_page); g_chat_ui.archive_page = NULL;
  if (g_chat_ui.editor && g_chat_ui.input) {
    glass_ime_commit(g_chat_ui.keyboard);
    glass_chat_copy_utf8(chat_draft, sizeof(chat_draft), lv_textarea_get_text(g_chat_ui.input));
  }
  if (g_chat_ui.timer) lv_timer_delete(g_chat_ui.timer);
  free(g_chat_ui.snapshot);
  memset(&g_chat_ui, 0, sizeof(g_chat_ui));
}

static void glass_chat_back(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_chat_ui.confirm) chat_ui_confirm_close(NULL);
  else if (g_chat_ui.editor) chat_ui_editor_close(NULL);
  else if (g_desktop.chat_return_apps) glass_apps(NULL);
  else panel_hide(NULL);
}

static void glass_chat_open(lv_event_t *e)
{
  LV_UNUSED(e);
  bool from_apps = g_desktop.apps_page || (g_desktop.chat_page && g_desktop.chat_return_apps);
  glass_chat_show(from_apps);
}

static void glass_chat_show(bool from_apps)
{
  lv_obj_t *card = panel_card("ESPClaw"); if (!card) return;
  lv_obj_clean(card); memset(&g_chat_ui, 0, sizeof(g_chat_ui)); g_chat_ui.root = card;
  g_chat_ui.snapshot = calloc(1, sizeof(*g_chat_ui.snapshot));
  if (!g_chat_ui.snapshot) { memset(&g_chat_ui, 0, sizeof(g_chat_ui)); panel_hide(NULL); show_toast("内存不足，暂时无法打开聊天"); return; }
  g_desktop.chat_page = true; g_desktop.chat_return_apps = from_apps;
  bool light = g_desktop.light_theme;
  g_chat_ui.background = light ? 0xf5f7f7 : 0x151b21;
  g_chat_ui.surface = light ? 0xffffff : 0x242d35;
  g_chat_ui.sidebar = light ? 0xe9efed : 0x1b252b;
  g_chat_ui.primary = light ? 0x1c3433 : 0xe7f0ee;
  g_chat_ui.secondary = light ? 0x647d79 : 0x98afa9;
  g_chat_ui.accent = light ? 0x18796c : 0x246f64;
  lv_obj_set_pos(card, 0, 0); lv_obj_set_size(card, 1024, 600);
  lv_obj_set_style_radius(card, 0, 0); lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(g_chat_ui.background), 0);
  lv_obj_add_event_cb(card, chat_ui_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_t *sidebar = glass_box(card, 0, 0, 220, 600, g_chat_ui.sidebar, 0);
  chat_ui_text(sidebar, "ESPClaw", 24, 34, 180, 28, g_chat_ui.primary, false);
  chat_ui_text(sidebar, "聊想法，也能做应用", 24, 82, 180, 16, g_chat_ui.secondary, false);
  chat_ui_button(sidebar, "新对话", 20, 118, 180, 44, true, chat_ui_new, NULL);
  g_chat_ui.count = chat_ui_text(sidebar, "0 轮交流", 26, 179, 172, 16, g_chat_ui.secondary, false);
  g_chat_ui.app_status = chat_ui_text(sidebar, "正在载入草稿", 24, 220, 174, 16, g_chat_ui.primary, true);
  lv_obj_set_height(g_chat_ui.app_status, 96);
  g_chat_ui.preview_button = chat_ui_button(sidebar, "预览草稿", 20, 324, 180, 44, false, chat_ui_preview_app, NULL);
  g_chat_ui.save_button = chat_ui_button(sidebar, "保存应用", 20, 378, 180, 44, false, chat_ui_save_app, NULL);
  g_chat_ui.discard_button = chat_ui_button(sidebar, "放弃创意", 20, 430, 180, 38, false, chat_ui_discard, NULL);
  g_chat_ui.storage = chat_ui_text(sidebar, "最近 6 轮对话\n自动保存在设备", 26, 480, 170, 16, g_chat_ui.secondary, true);
  chat_ui_button(sidebar, from_apps ? "返回应用" : "返回桌面", 20, 528, 180, 48, false, glass_chat_back, NULL);
  chat_ui_text(card, "对话", 250, 24, 150, 28, g_chat_ui.primary, false);
  g_chat_ui.status = chat_ui_text(card, "正在载入对话", 402, 34, 420, 16, g_chat_ui.secondary, false);
  g_chat_ui.model = chat_ui_text(card, "", 250, 71, 706, 16, g_chat_ui.secondary, false);
  chat_ui_button(card, "AI 设置", 874, 22, 124, 42, false, chat_ui_config, NULL);
  lv_obj_t *rule = glass_box(card, 246, 103, 752, 1, g_chat_ui.secondary, 0);
  lv_obj_set_style_bg_opa(rule, 36, 0);
  g_chat_ui.messages = glass_box(card, 242, 115, 764, 382, g_chat_ui.background, 0);
  lv_obj_add_flag(g_chat_ui.messages, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_chat_ui.messages, LV_DIR_VER); lv_obj_set_scrollbar_mode(g_chat_ui.messages, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_t *draft = glass_box(card, 250, 514, 614, 58, g_chat_ui.surface, 16);
  glass_clickable(draft, chat_ui_editor_open, NULL);
  g_chat_ui.draft_label = chat_ui_text(draft, chat_draft[0] ? chat_draft : "聊点什么…", 20, 18, 574, 20, g_chat_ui.secondary, false);
  g_chat_ui.action = chat_ui_button(card, "发送", 880, 514, 118, 58, true, chat_ui_action, NULL);
  g_chat_ui.action_label = lv_obj_get_child(g_chat_ui.action, 0);
  chat_ui_text(card, "轻点输入框开始 · 支持中文、英文与换行", 252, 575, 746, 16, g_chat_ui.secondary, false);
  glass_chat_refresh_config();
  (void)glass_qpk_start();
  g_chat_ui.timer = lv_timer_create(chat_ui_refresh, 250, NULL); chat_ui_refresh(NULL);
}
