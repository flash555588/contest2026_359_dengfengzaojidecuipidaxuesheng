/* SPDX-License-Identifier: Apache-2.0
 * One shared 32 px safe area. No extra thread, image buffer or animation.
 * Updated by the desktop's existing one-second timer; compare owned strings
 * because LV_LABEL_LONG_DOT may change the label's internal text buffer.
 */
static struct {
  lv_obj_t *bar, *network, *music, *more;
  uint32_t background;
  lv_obj_t *sheet, *sheet_network, *sheet_music, *sheet_play;
  char time[16], track[256];
  unsigned network_state, play_state;
  bool valid, light;
} g_status_ui;

static void glass_status_close(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_status_ui.sheet) lv_obj_delete(g_status_ui.sheet);
  g_status_ui.sheet = g_status_ui.sheet_network = NULL;
  g_status_ui.sheet_music = g_status_ui.sheet_play = NULL;
}

static void glass_status_action(lv_event_t *e)
{
  unsigned action = (uintptr_t)lv_event_get_user_data(e);
  glass_status_close(NULL);
  if (action == 1) glass_music(NULL);
  else if (action == 2) glass_lock_open(NULL);
  else if (action == 3) settings_clicked(NULL);
  else if (action == 4) glass_system_page_open(NULL);
}

static void glass_status_toggle(lv_event_t *e)
{ LV_UNUSED(e); glass_music_toggle(); glass_status_tick(); }
static void glass_status_skip(lv_event_t *e)
{ glass_music_skip((intptr_t)lv_event_get_user_data(e)); glass_status_tick(); }

static lv_obj_t *status_text(lv_obj_t *parent, const char *text,
                             int x, int y, int w)
{ return home_text(parent, text, x, y, w, 14, home_ink()); }

static lv_obj_t *status_button(lv_obj_t *parent, const char *text,
                               int x, int y, int w, int h,
                               lv_event_cb_t cb, void *data)
{
  lv_obj_t *button = glass_box(parent, x, y, w, h, detail_soft(), 9);
  glass_clickable(button, cb, data);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_bg_color(button, lv_color_hex(detail_line()), LV_STATE_PRESSED);
  lv_obj_t *label = status_text(button, text, 0, 0, w);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  return button;
}

static void glass_status_expand(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_status_ui.sheet) { glass_status_close(NULL); return; }
  if (g_lock_ui.root) return;
  lv_obj_t *shade = glass_box(lv_layer_top(), 0, GLASS_STATUS_HEIGHT,
                             1024, GLASS_CONTENT_HEIGHT, 0x000000, 0);
  g_status_ui.sheet = shade;
  lv_obj_set_style_bg_opa(shade, LV_OPA_40, 0);
  glass_clickable(shade, glass_status_close, NULL);
  lv_obj_remove_flag(shade, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_t *card = glass_box(shade, 594, 12, 414, 304, detail_paper(), 18);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  home_text(card, "快捷控制", 20, 18, 270, 20, home_ink());
  status_button(card, "收起", 334, 12, 64, 36, glass_status_close, NULL);
  g_status_ui.sheet_network = status_text(card, "", 20, 58, 374);
  g_status_ui.sheet_music = status_text(card, "", 20, 94, 374);
  status_button(card, "上一首", 20, 128, 112, 44, glass_status_skip, (void *)-1);
  lv_obj_t *play = status_button(card, "", 142, 128, 130, 44, glass_status_toggle, NULL);
  g_status_ui.sheet_play = lv_obj_get_child(play, 0);
  status_button(card, "下一首", 282, 128, 112, 44, glass_status_skip, (void *)1);
  status_button(card, "网络设置", 20, 190, 182, 44, glass_status_action, (void *)4);
  status_button(card, "系统设置", 212, 190, 182, 44, glass_status_action, (void *)3);
  status_button(card, "打开音乐", 20, 246, 182, 44, glass_status_action, (void *)1);
  status_button(card, "进入锁屏", 212, 246, 182, 44, glass_status_action, (void *)2);
  g_status_ui.valid = false;
  glass_status_tick();
}

static void glass_status_visibility(bool locked)
{
  glass_status_close(NULL);
  if (!g_status_ui.bar) return;
  if (locked) lv_obj_add_flag(g_status_ui.bar, LV_OBJ_FLAG_HIDDEN);
  else { lv_obj_remove_flag(g_status_ui.bar, LV_OBJ_FLAG_HIDDEN); glass_status_tick(); }
}

static void glass_status_tick(void)
{
  if (!g_status_ui.bar || lv_obj_has_flag(g_status_ui.bar, LV_OBJ_FLAG_HIDDEN)) return;
  bool first = !g_status_ui.valid, light = g_desktop.light_theme;
  lv_obj_t *page = g_desktop.current_card;
  if (!page || lv_obj_has_flag(g_desktop.panel, LV_OBJ_FLAG_HIDDEN)) page = g_glass.home;
  uint32_t background = page ? lv_color_to_u32(lv_obj_get_style_bg_color(page, 0)) & 0xffffff :
                              (light ? 0xf1f0f4 : 0x17181e);
  if (first || background != g_status_ui.background) {
    lv_obj_set_style_bg_color(g_status_ui.bar, lv_color_hex(background), 0);
    g_status_ui.background = background;
  }
  if (first || light != g_status_ui.light) {
    lv_obj_t *labels[] = {g_desktop.clock_label, g_status_ui.network, g_status_ui.music, g_status_ui.more};
    for (unsigned i = 0; i < sizeof(labels)/sizeof(labels[0]); i++)
      lv_obj_set_style_text_color(labels[i], lv_color_hex(home_muted()), 0);
    if (!first && g_status_ui.sheet) glass_status_close(NULL);
    g_status_ui.light = light;
  }
  char text[16]; struct tm local; time_t now = time(NULL);
#ifndef CONFIG_LIBC_LOCALTIME
  now += GLASS_CLOCK_OFFSET_SECONDS;
#endif
  if (now > 1700000000 && localtime_r(&now, &local)) strftime(text, sizeof(text), "%H:%M", &local);
  else strcpy(text, "--:--");
  if (first || strcmp(text, g_status_ui.time)) {
    strcpy(g_status_ui.time, text); lv_label_set_text(g_desktop.clock_label, text);
  }
  struct glass_status_network net = {0};
#ifdef CONFIG_SYSTEM_C6_DESKTOP
  glass_status_network_get(&net);
#endif
  unsigned network = net.wired ? 3 : net.wifi ? 2 : net.joining ? 1 : 0;
  static const char *const names[] = {"网络未连接", "Wi-Fi 获取地址", "Wi-Fi 已连接", "以太网已连接"};
  if (first || network != g_status_ui.network_state) {
    lv_label_set_text(g_status_ui.network, network == 3 ? GLASS_ICON_DEVICE_GENERIC : LV_SYMBOL_WIFI);
    lv_obj_set_style_opa(g_status_ui.network, network >= 2 ? LV_OPA_80 : LV_OPA_30, 0);
    if (g_status_ui.sheet_network) lv_label_set_text(g_status_ui.sheet_network, names[network]);
    g_status_ui.network_state = network;
  }
  struct music_now_playing music; glass_music_now_playing(&music);
  bool playing = music.state == MUSIC_PLAYING && !music.pause_requested;
  unsigned state = playing ? 1 : music.state == MUSIC_LOADING ? 2 : 0;
  const char *prefix = music.state == MUSIC_ERROR ? "播放失败" : music.state == MUSIC_ENDED ? "已播放完" :
    music.state == MUSIC_STOPPED ? "已停止" : state == 2 ? "载入中" : playing ? "正在播放" : "已暂停";
  char track[256];
  if (music.name[0]) snprintf(track, sizeof(track), "%s · %s", prefix, music.name);
  else strcpy(track, "声动 · 随时听音乐");
  if (first || strcmp(track, g_status_ui.track)) {
    strcpy(g_status_ui.track, track);
    if (g_status_ui.sheet_music) lv_label_set_text(g_status_ui.sheet_music, track);
  }
  if (first || state != g_status_ui.play_state) {
    lv_label_set_text(g_status_ui.music, state ? LV_SYMBOL_AUDIO : LV_SYMBOL_PAUSE);
    if (g_status_ui.sheet_play) lv_label_set_text(g_status_ui.sheet_play, playing ? "暂停" : "播放 / 继续");
    g_status_ui.play_state = state;
  }
  bool active = music.name[0] && (playing || music.state == MUSIC_PAUSED || music.state == MUSIC_LOADING || music.pause_requested);
  if (active) lv_obj_remove_flag(g_status_ui.music, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(g_status_ui.music, LV_OBJ_FLAG_HIDDEN);
  g_status_ui.valid = true;
}

static void glass_status_create(void)
{
  lv_obj_t *bar = glass_box(lv_layer_top(), 0, 0, 1024, GLASS_STATUS_HEIGHT, detail_paper(), 0);
  g_status_ui.bar = g_desktop.statusbar = bar;
  glass_clickable(bar, glass_status_expand, NULL);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  g_desktop.clock_label = status_text(bar, "--:--", 24, 8, 66);
  lv_obj_set_style_text_font(g_desktop.clock_label, home_font(12), 0);
  g_status_ui.music = home_icon(bar, LV_SYMBOL_AUDIO, 886, 8, 14, home_muted());
  g_status_ui.network = home_icon(bar, LV_SYMBOL_WIFI, 934, 8, 14, home_muted());
  g_status_ui.more = home_icon(bar, GLASS_ICON_ELLIPSIS, 982, 8, 14, home_muted());
  glass_status_tick();
}
