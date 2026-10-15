/* SPDX-License-Identifier: Apache-2.0
 * Native LVGL implementation of the reviewed 1024 x 600 refined home.
 * No screenshot background, browser, per-frame bitmap or simulated telemetry.
 * Shared helpers and application lifetimes remain owned by glass_ui.inc.
 */
#include "glass_icons.h"
#include "glass_home_gradient.inc"

static lv_font_t *g_home_font12;
static lv_font_t *g_home_font14;
static lv_font_t *g_home_font32;
static lv_font_t *g_home_font34;
static lv_font_t *g_home_font40;
static lv_font_t g_hass_font20;
static lv_font_t *g_home_font64;

static void glass_home_fonts_init(const void *data, size_t bytes)
{
  g_home_font12 = lv_tiny_ttf_create_data_ex(data, bytes, 12,
    LV_FONT_KERNING_NORMAL, 128);
  g_home_font14 = lv_tiny_ttf_create_data_ex(data, bytes, 14,
    LV_FONT_KERNING_NORMAL, 192);
  g_home_font32 = lv_tiny_ttf_create_data_ex(data, bytes, 32,
    LV_FONT_KERNING_NORMAL, 24);
  g_home_font34 = lv_tiny_ttf_create_data_ex(data, bytes, 34,
    LV_FONT_KERNING_NORMAL, 32);
  g_home_font40 = lv_tiny_ttf_create_data_ex(data, bytes, 40,
    LV_FONT_KERNING_NORMAL, 32);
  g_home_font64 = lv_tiny_ttf_create_data_ex(data, bytes, 64,
    LV_FONT_KERNING_NORMAL, 20);
  /* The bundled CJK font has no degree mark. Native fallback avoids a tofu
   * box in temperature labels while keeping all text independently rendered. */
  if (g_home_font64) g_home_font64->fallback = glass_icon_font(64);
  if (g_home_font40) g_home_font40->fallback = glass_icon_font(32);
  /* A degree mark is text, so use its compact typographic advance rather
   * than the square icon-cell advance used by the shared icon fallback. */
  if (g_desktop.font20) {
    g_hass_font20 = *g_desktop.font20;
    g_hass_font20.fallback = &lv_font_montserrat_24;
  }
}

static uint32_t home_ink(void)
{ return g_desktop.light_theme ? 0x242430 : 0xf0edf6; }
static uint32_t home_muted(void)
{ return g_desktop.light_theme ? 0x787682 : 0xa6a2b2; }
static uint32_t home_paper(void)
{ return g_desktop.light_theme ? 0xfcfbfd : 0x272830; }
static uint32_t home_accent(void)
{ return g_desktop.light_theme ? 0x78668e : 0xbcadd3; }

static const lv_font_t *home_font(int size)
{
  if (size == 12 && g_home_font12) return g_home_font12;
  if (size == 14 && g_home_font14) return g_home_font14;
  if (size == 32 && g_home_font32) return g_home_font32;
  if (size == 34 && g_home_font34) return g_home_font34;
  if (size == 64 && g_home_font64) return g_home_font64;
  return zh_font(size);
}

/* Only HA requests this reading face; other quick apps retain their fonts. */
static const lv_font_t *hass_qpk_font(int size)
{
  if (size == 40 && g_home_font40) return g_home_font40;
  if (size == 20 && g_desktop.font20) return &g_hass_font20;
  return zh_font(size);
}

static lv_obj_t *home_text(lv_obj_t *parent, const char *text,
                           int x, int y, int w, int size, uint32_t color)
{
  lv_obj_t *label = glass_text(parent, text, x, y, w, size, color);
  lv_obj_set_style_text_font(label, home_font(size), 0);
  return label;
}

static lv_obj_t *home_icon(lv_obj_t *parent, const char *symbol,
                           int x, int y, int size, uint32_t color)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, symbol);
  lv_obj_set_style_text_font(label, glass_icon_font(size), 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_pos(label, x, y);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return label;
}

static lv_obj_t *home_button(lv_obj_t *parent, const char *text,
                             const char *symbol, int x, int y, int w,
                             uint32_t fill, uint32_t color,
                             lv_event_cb_t callback, void *data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, 44);
  lv_obj_set_style_radius(button, 11, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(fill), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(button, 210, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
  if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
  if (text && text[0])
    {
      int offset = symbol ? 27 : 0;
      lv_obj_t *label = home_text(button, text, offset, 0,
        w - offset - 6, 14, color);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(label, LV_ALIGN_LEFT_MID, offset, 0);
    }
  if (symbol)
    {
      lv_obj_t *label = home_icon(button, symbol, 0, 0, 16, color);
      if (text && text[0]) lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
      else lv_obj_center(label);
    }
  return button;
}

static lv_obj_t *home_card(lv_obj_t *root, unsigned index,
                           int x, int y, int w, int h, uint32_t color)
{
  lv_obj_t *card = glass_box(root, x, y, w, h, color, 20);
  lv_obj_set_style_clip_corner(card, true, 0);
  g_glass.home_cards[index] = card;
  return card;
}

static void home_rule(lv_obj_t *parent, int x, int y, int w, uint32_t color)
{
  lv_obj_t *rule = glass_box(parent, x, y, w, 1, color, 0);
  lv_obj_set_style_bg_opa(rule, 35, 0);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
}

static void home_arrow(lv_obj_t *parent, lv_event_cb_t callback)
{
  lv_obj_t *button = home_button(parent, NULL, GLASS_ICON_ARROW_NE,
    0, 7, 44,
    lv_color_to_u32(lv_obj_get_style_bg_color(parent, 0)),
    home_muted(), callback, NULL);
  /* Parent geometry is not resolved while the home is being constructed. */
  lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -11, 7);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
}

static void home_chat_input(lv_event_t *event)
{
  glass_chat_open(event);
  if (g_chat_ui.root) chat_ui_editor_open(NULL);
  if (event && lv_event_get_user_data(event) && g_chat_ui.keyboard)
    glass_ime_start_voice(g_chat_ui.keyboard);
}

static void home_vision_mode(lv_event_t *event)
{
  unsigned mode = (unsigned)(uintptr_t)lv_event_get_user_data(event);
  launch_builtin_espdl_qapp(NULL);
  if (qpk_runtime_select_espdl_mode(mode) < 0)
    show_toast("识别页面未就绪，请重新打开");
}

/* Cards animate only on entry. Dynamic values are updated by glass_update's
 * existing semantic cache; there is no perpetual background animation. */
static void glass_home_finish(void)
{
  for (unsigned i = 0; i < GLASS_HOME_CARD_COUNT; i++)
    {
      lv_obj_t *card = g_glass.home_cards[i];
      lv_obj_set_style_translate_y(card, 1, LV_STATE_PRESSED);
      if (!g_glass.reduced_motion)
        {
          lv_anim_t animation;
          lv_anim_init(&animation);
          lv_anim_set_var(&animation, card);
          lv_anim_set_values(&animation, 8, 0);
          lv_anim_set_delay(&animation, i * 25);
          lv_anim_set_duration(&animation, 180);
          lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
          lv_anim_set_exec_cb(&animation, glass_card_offset);
          lv_anim_start(&animation);
        }
    }
}

static void glass_rebuild(void)
{
  const bool light = g_desktop.light_theme;
  const uint32_t ink = home_ink(), muted = home_muted();
  const uint32_t paper = home_paper(), accent = home_accent();
  const uint32_t green = light ? 0x3e6b59 : 0xa3c4b3;
  const uint32_t blue = light ? 0x556e91 : 0xb2c2dd;
  const uint32_t background = light ? 0xf1f0f4 : 0x17181e;
  if (g_glass.home)
    {
      lv_anim_delete(g_glass.home, glass_fade);
      lv_obj_delete(g_glass.home);
    }
  g_glass_display.valid = false;
  lv_obj_t *root = glass_box(g_desktop.screen, 0, 0, 1024, 600, background, 0);
  g_glass.home = root;
  lv_obj_move_to_index(root, 0);

  /* Header. The network entry does not claim service connectivity. */
  lv_obj_t *mark = glass_box(root, 28, 22, 40, 40, ink, 13);
  lv_obj_t *label = home_text(mark, "v.", 0, 0, 40, 28, paper);
  lv_obj_center(label);
  home_text(root, "Vela", 80, 26, 50, 20, ink);
  home_text(root, "Desk", 127, 26, 58, 20, muted);
  g_glass.date = home_text(root, "时钟待校准", 378, 34, 346, 14, muted);
  g_glass.wifi_button = home_button(root, "网络", LV_SYMBOL_WIFI,
    764, 22, 90, background, muted, glass_system_page_open, NULL);
  g_glass.clock = home_text(root, "--:--", 858, 25, 87, 28, ink);
  g_glass.theme_button = home_button(root, NULL, GLASS_ICON_SLIDERS,
    952, 20, 44, paper, ink, settings_clicked, NULL);

  /* Persistent rail. All icons come from the same authored outline family. */
  const char *symbols[] = {GLASS_ICON_GRID, GLASS_ICON_CAMERA, LV_SYMBOL_HOME,
                          GLASS_ICON_SPARKLES, LV_SYMBOL_AUDIO, GLASS_ICON_ELLIPSIS};
  const char *names[] = {"桌面", "相机", "家居", "AI", "影音", "应用"};
  lv_event_cb_t callbacks[] = {panel_hide, launch_builtin_camera_qapp,
    glass_homeassistant, glass_chat_open, glass_music, qpk_clicked};
  for (unsigned i = 0; i < 6; i++)
    {
      lv_obj_t *item = glass_box(root, 20, 84 + i * 80, 68, 72,
                                i ? background : paper, 17);
      lv_obj_t *icon = home_icon(item, symbols[i], 0, 0, 20, i ? muted : accent);
      lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 12);
      label = home_text(item, names[i], 0, 47, 68, 12, i ? muted : accent);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
      glass_clickable(item, callbacks[i], NULL);
      lv_obj_set_style_bg_color(item, lv_color_hex(paper), LV_STATE_PRESSED);
    }

  /* One music feature, with adjacent entry points for video and live media. */
  lv_obj_t *card = home_card(root, 0, 108, 84, 567, 272, 0x272534);
  static const uint32_t ends[] = {0x695070, 0x754f48, 0x456e67};
  glass_gradient(card, ends[g_glass.palette < 3 ? g_glass.palette : 0], LV_GRAD_DIR_HOR);
  lv_obj_t *fill = lv_image_create(card);
  lv_image_set_src(fill, home_gradients[g_glass.palette < 3 ? g_glass.palette : 0]);
  lv_obj_set_size(fill, 567, 272);
  lv_image_set_inner_align(fill, LV_IMAGE_ALIGN_TILE);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
  home_text(card, "MUSIC SPACE", 26, 22, 230, 12, 0xd6c6de);
  home_icon(card, GLASS_ICON_HEADPHONES, 438, 20, 14, 0xd6c6de);
  home_text(card, "音乐空间", 458, 22, 91, 12, 0xd6c6de);
  /* Circles and actual LVGL assets, not a rasterized page background. */
  for (unsigned i = 0; i < 2; i++)
    {
      int d = i ? 294 : 246;
      lv_obj_t *orbit = glass_box(card, i ? 292 : 316, i ? 29 : 53, d, d, 0xffffff, d/2);
      lv_obj_set_style_bg_opa(orbit, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(orbit, 1, 0);
      lv_obj_set_style_border_color(orbit, lv_color_hex(0xd4b7de), 0);
      lv_obj_set_style_border_opa(orbit, 30, 0);
      lv_obj_remove_flag(orbit, LV_OBJ_FLAG_CLICKABLE);
    }
  const lv_image_dsc_t *covers[] = {&img_lv_demo_music_cover_3, &img_lv_demo_music_cover_1};
  const int widths[] = {129, 178};
  const int positions[][2] = {{300, 62}, {397, 101}};
  for (unsigned i = 0; i < 2; i++)
    {
      lv_obj_t *art = lv_image_create(card);
      lv_image_set_src(art, covers[i]);
      lv_image_set_pivot(art, 0, 0);
      lv_image_set_scale(art, widths[i] * 256 / covers[i]->header.w);
      lv_obj_set_pos(art, positions[i][0], positions[i][1]);
      lv_obj_remove_flag(art, LV_OBJ_FLAG_CLICKABLE);
    }
  label = home_text(card, "此刻，\n听见喜欢。", 26, 60, 265, 34, 0xfff8ef);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(label, 2, 0);
  home_text(card, "搜索好歌，收藏你的热爱", 26, 154, 264, 12, 0xd6c6de);
  home_button(card, "进入音乐", LV_SYMBOL_PLAY, 26, 179, 124,
    0xfff8ef, 0x302637, glass_music, NULL);
  home_rule(card, 26, 230, 515, 0xe4cfe3);
#ifdef CONFIG_SYSTEM_BILIBILI_LIVE
  label = home_text(card, "更多影音", 26, 246, 74, 12, 0xd6c6de);
  lv_obj_t *live = home_button(card, "网络直播", GLASS_ICON_RADIO,
    103, 228, 127, 0x373043, 0xfff8ef, launch_bilibili, NULL);
  lv_obj_t *video = home_button(card, "本地视频", LV_SYMBOL_VIDEO,
    234, 228, 127, 0x373043, 0xfff8ef, launch_local_video, NULL);
  lv_obj_set_style_bg_opa(live, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(video, LV_OPA_TRANSP, 0);
#else
  home_text(card, "搜索 · 播放 · 收藏", 26, 245, 264, 12, 0xd6c6de);
#endif

  /* Weather data is supplied by the existing worker. Unknown values stay --. */
  card = home_card(root, 3, 693, 84, 307, 272, paper);
  home_text(card, "天气与环境", 24, 24, 226, 16, ink);
  home_arrow(card, glass_weather_open);
  g_glass.temperature = home_text(card, "--°", 24, 60, 180, 64, ink);
  home_icon(card, GLASS_ICON_CLOUD_SUN, 224, 81, 64, light ? 0xc19b62 : 0xc6a56e);
  g_glass.weather_condition = home_text(card, "等待联网", 24, 134, 255, 12, muted);
  home_rule(card, 24, 170, 259, muted);
  g_glass.widget_title = home_text(card, "空气质量", 24, 188, 138, 12, muted);
  g_glass.widget_value = home_text(card, "--", 24, 207, 102, 28, ink);
  /* Retain the existing selectable widget, without repeating humidity. */
  g_glass.air = home_text(card, "AQI", 132, 215, 35, 12, muted);
  g_glass.widget_badge = glass_box(card, 24, 242, 137, 19, paper, 0);
  lv_obj_set_style_bg_opa(g_glass.widget_badge, LV_OPA_TRANSP, 0);
  g_glass.widget_category = home_text(g_glass.widget_badge, "暂无数据", 0, 0, 137, 12, muted);
  lv_obj_t *vertical = glass_box(card, 171, 184, 1, 77, muted, 0);
  lv_obj_set_style_bg_opa(vertical, 32, 0);
  lv_obj_remove_flag(vertical, LV_OBJ_FLAG_CLICKABLE);
  home_text(card, "环境湿度", 190, 188, 100, 12, muted);
  g_glass.humidity = home_text(card, "--%", 190, 207, 103, 28, ink);
  g_glass.humidity_status = home_text(card, "暂无数据", 190, 245, 96, 12, green);
  /* Service status is available in the detail page; this compact footer is
   * hidden but still owned by the home for the semantic updater. */
  g_glass.widget_hint = home_text(card, "等待联网", 0, 0, 260, 12, muted);
  lv_obj_add_flag(g_glass.widget_hint, LV_OBJ_FLAG_HIDDEN);
  glass_clickable(card, glass_widget_open, NULL);

  /* Bottom cards share geometry, typography and 44 px touch targets. */
  card = home_card(root, 1, 108, 374, 285, 178, light ? 0xe9e2f2 : 0x302a3e);
  struct glass_grok *home_ball=glass_grok_create_pixel(card,285,!g_desktop.light_theme,zh_font);
  glass_grok_set_reduced_motion(home_ball,g_glass.reduced_motion);
  glass_grok_set_obscured_by(home_ball,g_desktop.panel);
  home_arrow(card, glass_chat_open);
  home_rule(card,21,89,243,accent);
  home_text(card,"AI 助手",21,95,100,16,ink);
  label=home_text(card,"文字 · 语音",128,98,136,12,muted);
  lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_RIGHT,0);
  home_button(card, "开始对话", GLASS_ICON_CHAT, 21, 121, 143,
    light ? 0xf7f4fa : 0x3d354a, accent, home_chat_input, NULL);
  home_button(card, NULL, GLASS_ICON_MIC, 171, 121, 44,
    light ? 0xf7f4fa : 0x3d354a, accent, home_chat_input, (void *)1);
  glass_clickable(card, glass_chat_open, NULL);

  card = home_card(root, 2, 411, 374, 285, 178, light ? 0xe0ebe6 : 0x26372f);
  home_icon(card, LV_SYMBOL_HOME, 25, 20, 20, green);
  home_arrow(card, glass_homeassistant);
  home_text(card, "智能家居", 21, 60, 240, 20, ink);
  g_glass.devices = home_text(card, "-- / --", 21, 90, 145, 32, ink);
  home_text(card, "实体可用 / 全部", 169, 106, 100, 12, green);
  home_rule(card, 21, 140, 243, green);
  home_text(card, "Home Assistant", 21, 151, 122, 12, muted);
  g_glass.sync = home_text(card, "尚未同步", 144, 151, 122, 12, muted);
  lv_obj_set_style_text_align(g_glass.sync, LV_TEXT_ALIGN_RIGHT, 0);
  glass_clickable(card, glass_homeassistant, NULL);

  card = home_card(root, 4, 714, 374, 286, 178, light ? 0xe3e7f0 : 0x29313f);
  home_icon(card, GLASS_ICON_FACE, 25, 20, 20, blue);
  home_arrow(card, launch_builtin_camera_qapp);
  home_text(card, "相机与识别", 21, 60, 245, 20, ink);
  home_text(card, "共用相机，切换不同任务", 21, 90, 245, 12, muted);
  uint32_t chip = light ? 0xf5f6fa : 0x354154;
  home_button(card, "拍摄", NULL, 21, 121, 61, chip, blue, launch_builtin_camera_qapp, NULL);
  home_button(card, "人脸跟随", NULL, 88, 121, 103, chip, blue, home_vision_mode, (void *)1);
  home_button(card, "分类", NULL, 197, 121, 65, chip, blue, home_vision_mode, NULL);
  glass_clickable(card, launch_builtin_camera_qapp, NULL);

  home_text(root, "VelaDesk", 108, 573, 190, 12, muted);
  lv_obj_t *files = home_button(root, "文件", LV_SYMBOL_DIRECTORY,
    739, 556, 73, background, muted, glass_system_page_open, (void *)1);
  lv_obj_t *pet = home_button(root, "桌宠", GLASS_ICON_FISH,
    817, 556, 73, background, muted, launch_builtin_dafeiyu_qapp, NULL);
  lv_obj_t *portal = home_button(root, "扫码配置", GLASS_ICON_QR,
    895, 556, 105, background, muted, glass_portal_open, NULL);
  lv_obj_set_style_bg_opa(files, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(pet, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(portal, LV_OPA_TRANSP, 0);
  glass_update(NULL);
  glass_home_finish();
}
