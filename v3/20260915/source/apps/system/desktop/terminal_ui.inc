/* SPDX-License-Identifier: Apache-2.0
 * Included after the existing QPK launchers. All LVGL access stays on the
 * desktop task; persistent writes happen only on the explicit Save action.
 */
#ifndef TERM_LAYOUT_PATH_A
#  define TERM_LAYOUT_PATH_A "/data/terminal-a.layout"
#  define TERM_LAYOUT_PATH_B "/data/terminal-b.layout"
#endif
#define TERM_MESSAGES 8

struct term_app_s
{
  const char *name;
  const char *symbol;
  const char *key;
  uint32_t color;
};

static struct term_app_s g_apps[TERM_MAX_APPS] =
{
  {"智能家居", LV_SYMBOL_HOME, "builtin:ha", 0x47c7ad},
  {"相机", LV_SYMBOL_IMAGE, "builtin:camera", 0x779df2},
  {"OuO", LV_SYMBOL_EYE_OPEN, "builtin:ouo", 0xf1bc73},
  {"2048", LV_SYMBOL_KEYBOARD, "builtin:2048", 0xdf927d},
  {"示例应用", LV_SYMBOL_EDIT, "builtin:hello", 0xa29cec},
  {"控制中心", LV_SYMBOL_SETTINGS, "builtin:controls", 0x8cbdcb},
  {"设备信息", LV_SYMBOL_LIST, "builtin:about", 0xa3b8a0}
};

static struct
{
  struct term_layout_s layout;
  lv_obj_t *icons[TERM_MAX_APPS];
  lv_obj_t *dock;
  lv_obj_t *editor;
  lv_obj_t *edit_label;
  lv_obj_t *size_slider;
  lv_obj_t *pocket_label;
  lv_obj_t *badge;
  lv_obj_t *shade;
  lv_obj_t *size_label;
  char messages[TERM_MESSAGES][192];
  unsigned int messages_count;
  unsigned int unread;
  int count;
  int selected;
  int drag_id;
  lv_point_t origin;
  int origin_x;
  int origin_y;
  bool editing;
  bool moved;
  bool long_press;
  bool dirty;
  int active_slot;
} g_terminal;

static void settings_clicked(lv_event_t *e);
static void about_clicked(lv_event_t *e);
static void term_pocket(lv_event_t *e);
static void term_messages(lv_event_t *e);
static void term_render_icons(bool animate);

static void term_translate(void *object, int32_t value)
{
  lv_obj_set_style_translate_y(object, value, 0);
}

static void term_opacity(void *object, int32_t value)
{
  lv_obj_set_style_opa(object, value, 0);
}

static void term_animate(lv_obj_t *object, lv_anim_exec_xcb_t cb,
                         int from, int to, int delay)
{
  lv_anim_t animation;
  lv_anim_delete(object, cb);
  if (g_terminal.layout.reduced)
    {
      cb(object, to);
      return;
    }
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, object);
  lv_anim_set_exec_cb(&animation, cb);
  lv_anim_set_values(&animation, from, to);
  lv_anim_set_duration(&animation, 240);
  lv_anim_set_delay(&animation, delay);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_start(&animation);
}

static void terminal_enter(lv_obj_t *object, int delay)
{
  term_animate(object, term_translate, 18, 0, delay);
  term_animate(object, term_opacity, 100, 255, delay);
}

static lv_obj_t *term_label(lv_obj_t *parent, const char *text,
                             int x, int y, int size, bool secondary)
{
  lv_obj_t *label = make_label(parent, text,
                             secondary ? theme_secondary() : theme_primary(),
                             size);
  lv_obj_set_pos(label, x, y);
  return label;
}

static lv_obj_t *term_button(lv_obj_t *parent, const char *text,
                              int x, int y, int w, int h,
                              lv_event_cb_t cb, intptr_t data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *label;
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, h);
  style_button(button, theme_surface());
  lv_obj_set_style_radius(button, 14, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  label = make_label(button, text, theme_primary(), 16);
  lv_obj_center(label);
  if (cb != NULL)
    {
      lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, (void *)data);
    }
  return button;
}

static int term_pocket_count(void)
{
  int i;
  int count = 0;
  for (i = 0; i < g_terminal.count; i++)
    {
      count += g_terminal.layout.items[i].pocket != 0;
    }
  return count;
}

static void term_update_badges(void)
{
  if (g_terminal.pocket_label != NULL)
    {
      lv_label_set_text_fmt(g_terminal.pocket_label, "口袋收纳  %d",
                           term_pocket_count());
    }
  if (g_terminal.badge != NULL)
    {
      lv_label_set_text_fmt(g_terminal.badge, "消息  %u",
                           g_terminal.unread);
    }
}

static void terminal_notify(const char *text)
{
  unsigned int slot = g_terminal.messages_count % TERM_MESSAGES;
  snprintf(g_terminal.messages[slot], sizeof(g_terminal.messages[slot]),
           "%s", text);
  g_terminal.messages_count++;
  if (g_terminal.unread < TERM_MESSAGES)
    {
      g_terminal.unread++;
    }
  term_update_badges();
}

static bool terminal_quiet(void)
{
  return g_terminal.layout.quiet != 0;
}

static void term_defaults(void)
{
  int i;
  memset(&g_terminal.layout, 0, sizeof(g_terminal.layout));
  g_terminal.layout.magic = TERM_LAYOUT_MAGIC;
  g_terminal.layout.version = TERM_LAYOUT_VERSION;
  g_terminal.layout.count = g_terminal.count;
  for (i = 0; i < g_terminal.count; i++)
    {
      struct term_item_s *item = &g_terminal.layout.items[i];
      snprintf(item->key, sizeof(item->key), "%s", g_apps[i].key);
      item->size = 76;
      item->x = 12 + (i % 8) * 118;
      item->y = 12 + (i / 8) * 142;
      term_constrain(item);
    }
}

static void term_load(void)
{
  struct term_layout_s *saved = malloc(sizeof(*saved));
  struct term_layout_s *candidate = malloc(sizeof(*candidate));
  static const char *paths[] = {TERM_LAYOUT_PATH_A, TERM_LAYOUT_PATH_B};
  FILE *stream;
  int slot;
  int i;
  int j;
  bool valid = false;
  g_terminal.active_slot = -1;
  if (saved == NULL || candidate == NULL)
    {
      free(saved);
      free(candidate);
      return;
    }
  for (slot = 0; slot < 2; slot++)
    {
      stream = fopen(paths[slot], "rb");
      if (stream != NULL)
        {
          bool complete = fread(candidate, 1, sizeof(*candidate), stream) ==
                          sizeof(*candidate) && fgetc(stream) == EOF &&
                          !ferror(stream) && term_layout_valid(candidate);
          fclose(stream);
          if (complete && (!valid ||
              (int32_t)(candidate->generation - saved->generation) > 0))
            {
              *saved = *candidate;
              valid = true;
              g_terminal.active_slot = slot;
            }
          else if (!complete)
            {
              terminal_notify("一份布局记录不可用，已保留另一份有效记录或默认布局。");
            }
        }
    }
  if (valid)
    {
      g_terminal.layout.light = saved->light;
      g_terminal.layout.quiet = saved->quiet;
      g_terminal.layout.reduced = saved->reduced;
      g_terminal.layout.warm = saved->warm;
      g_terminal.layout.generation = saved->generation;
      /* Match by stable application key, never by discovery order. */
      for (i = 0; i < g_terminal.count; i++)
        {
          for (j = 0; j < saved->count; j++)
            {
              if (!strcmp(g_terminal.layout.items[i].key,
                          saved->items[j].key))
                {
                  g_terminal.layout.items[i] = saved->items[j];
                  break;
                }
            }
        }
    }
  free(saved);
  free(candidate);
}

static void term_save(lv_event_t *e)
{
  FILE *stream;
  int slot = g_terminal.active_slot == 0 ? 1 : 0;
  struct term_layout_s *candidate = malloc(sizeof(*candidate));
  bool ok;
  LV_UNUSED(e);
  if (candidate == NULL)
    {
      show_toast("内存不足，暂时无法保存布局。");
      return;
    }
  *candidate = g_terminal.layout;
  candidate->generation++;
  candidate->checksum = term_checksum(candidate);
  stream = fopen(slot == 0 ? TERM_LAYOUT_PATH_A : TERM_LAYOUT_PATH_B, "wb");
  if (stream == NULL)
    {
      free(candidate);
      show_toast("保存失败：/data 不可写，当前布局仍保留在本次会话。");
      return;
    }
  ok = fwrite(candidate, 1, sizeof(*candidate), stream) == sizeof(*candidate);
  if (fflush(stream) != 0)
    {
      ok = false;
    }
  if (fsync(fileno(stream)) != 0)
    {
      ok = false;
    }
  if (fclose(stream) != 0)
    {
      ok = false;
    }
  if (ok)
    {
      g_terminal.layout = *candidate;
      g_terminal.active_slot = slot;
      g_terminal.dirty = false;
      show_toast("桌面布局、口袋与控制选项已保存。");
    }
  else
    {
      show_toast("保存失败，原有布局文件已保留。");
    }
  free(candidate);
}

static void term_recolor(lv_obj_t *object)
{
  uint32_t i;
  if (lv_obj_check_type(object, &lv_label_class))
    {
      lv_obj_set_style_text_color(object, lv_color_hex(theme_primary()), 0);
    }
  else if (lv_obj_check_type(object, &lv_button_class))
    {
      lv_obj_set_style_bg_color(object, lv_color_hex(theme_surface()), 0);
    }
  for (i = 0; i < lv_obj_get_child_count(object); i++)
    {
      term_recolor(lv_obj_get_child(object, i));
    }
}

static void term_settle(lv_obj_t *object)
{
  uint32_t i;
  lv_anim_delete(object, term_translate);
  lv_anim_delete(object, term_opacity);
  term_translate(object, 0);
  term_opacity(object, 255);
  for (i = 0; i < lv_obj_get_child_count(object); i++)
    {
      term_settle(lv_obj_get_child(object, i));
    }
}

static void terminal_theme(void)
{
  g_desktop.light_theme = g_terminal.layout.light != 0;
  lv_obj_set_style_bg_color(g_desktop.screen,
      lv_color_hex(g_desktop.light_theme ? 0xe9f0ee : 0x101c23), 0);
  if (g_desktop.statusbar != NULL)
    {
      lv_obj_set_style_bg_color(g_desktop.statusbar,
                               lv_color_hex(theme_card()), 0);
      term_recolor(g_desktop.statusbar);
    }
  if (g_desktop.home_title != NULL)
    {
      lv_obj_set_style_text_color(g_desktop.home_title,
                                 lv_color_hex(theme_primary()), 0);
      lv_obj_set_style_text_color(g_desktop.home_hint,
                                 lv_color_hex(theme_secondary()), 0);
      lv_obj_set_style_text_color(g_desktop.clock_label,
                                 lv_color_hex(theme_primary()), 0);
    }
  if (g_terminal.shade != NULL)
    {
      lv_obj_set_style_bg_opa(g_terminal.shade,
                              g_terminal.layout.warm ? 24 : 0, 0);
    }
  if (g_terminal.dock != NULL)
    {
      lv_obj_set_style_bg_color(g_terminal.dock, lv_color_hex(theme_card()), 0);
      lv_obj_set_style_bg_color(g_terminal.editor, lv_color_hex(theme_card()), 0);
      term_recolor(g_terminal.dock);
      term_recolor(g_terminal.editor);
      term_recolor(lv_obj_get_parent(g_terminal.pocket_label));
    }
  if (g_terminal.layout.reduced)
    {
      term_settle(g_desktop.screen);
    }
}

static void term_launch(int id)
{
  if (id < 0 || id >= g_terminal.count)
    {
      return;
    }
  switch (id)
    {
      case 0: launch_builtin_homeassistant_qapp(NULL); break;
      case 1: launch_builtin_camera_qapp(NULL); break;
      case 2: launch_builtin_ouo_qapp(NULL); break;
      case 3: launch_builtin_2048_qapp(NULL); break;
      case 4: launch_builtin_qapp(NULL); break;
      case 5: settings_clicked(NULL); break;
      case 6: about_clicked(NULL); break;
      default: external_qapp_launch(id - 7); break;
    }
}

static void term_select(int id)
{
  int i;
  g_terminal.selected = id;
  g_terminal.editing = true;
  lv_obj_remove_flag(g_terminal.editor, LV_OBJ_FLAG_HIDDEN);
  for (i = 0; i < g_terminal.count; i++)
    {
      if (g_terminal.icons[i] != NULL)
        {
          lv_obj_set_style_border_width(g_terminal.icons[i], i == id ? 2 : 0, 0);
          lv_obj_set_style_border_color(g_terminal.icons[i],
                                       lv_color_hex(0x47c7ad), 0);
        }
    }
  lv_label_set_text(g_terminal.edit_label, g_apps[id].name);
  lv_slider_set_value(g_terminal.size_slider,
                     g_terminal.layout.items[id].size, LV_ANIM_OFF);
  lv_label_set_text_fmt(g_terminal.size_label, "%u px",
                       g_terminal.layout.items[id].size);
  lv_label_set_text(g_desktop.home_hint,
                   "拖动调整位置 · 拖到右下角收纳 · 调整后点击保存");
}

static void term_icon_event(lv_event_t *e)
{
  int id = (intptr_t)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *input = lv_indev_active();
  lv_point_t point;
  struct term_item_s *item = &g_terminal.layout.items[id];
  lv_obj_t *object = lv_event_get_target(e);
  if (code == LV_EVENT_PRESSED && input != NULL)
    {
      lv_indev_get_point(input, &g_terminal.origin);
      g_terminal.origin_x = item->x;
      g_terminal.origin_y = item->y;
      g_terminal.drag_id = id;
      g_terminal.moved = false;
      g_terminal.long_press = false;
      lv_anim_delete(object, term_translate);
      term_translate(object, 0);
    }
  else if (code == LV_EVENT_LONG_PRESSED)
    {
      g_terminal.long_press = true;
      term_select(id);
    }
  else if (code == LV_EVENT_PRESSING && input != NULL &&
           g_terminal.editing && g_terminal.drag_id == id)
    {
      lv_indev_get_point(input, &point);
      if (abs(point.x - g_terminal.origin.x) > 6 ||
          abs(point.y - g_terminal.origin.y) > 6)
        {
          g_terminal.moved = true;
          item->x = g_terminal.origin_x + point.x - g_terminal.origin.x;
          item->y = g_terminal.origin_y + point.y - g_terminal.origin.y;
          term_constrain(item);
          lv_obj_set_pos(object, item->x, item->y);
          g_terminal.dirty = true;
        }
    }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
      if (code == LV_EVENT_RELEASED && input != NULL &&
          g_terminal.editing && g_terminal.moved)
        {
          lv_indev_get_point(input, &point);
          if (point.x >= 800 && point.y >= 490)
            {
              item->pocket = 1;
              lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
              term_update_badges();
              terminal_notify("快捷方式已收入口袋，可在口袋中恢复。");
            }
        }
      g_terminal.drag_id = -1;
    }
  else if (code == LV_EVENT_CLICKED && !g_terminal.moved &&
           !g_terminal.long_press)
    {
      if (g_terminal.editing)
        {
          term_select(id);
        }
      else
        {
          term_launch(id);
        }
    }
}

static lv_obj_t *term_icon(lv_obj_t *parent, int id, int x, int y,
                            int size, lv_event_cb_t callback)
{
  lv_obj_t *button = lv_obj_create(parent);
  lv_obj_t *face;
  lv_obj_t *symbol;
  lv_obj_t *label;
  lv_obj_set_size(button, size, size + TERM_LABEL_H);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, (void *)(intptr_t)id);

  face = lv_obj_create(button);
  lv_obj_set_size(face, size - 4, size - 4);
  lv_obj_set_pos(face, 2, 2);
  lv_obj_set_style_pad_all(face, 0, 0);
  lv_obj_set_style_radius(face, size / 4, 0);
  lv_obj_set_style_bg_color(face, lv_color_hex(g_apps[id].color), 0);
  lv_obj_set_style_border_width(face, 0, 0);
  lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  symbol = lv_label_create(face);
  lv_label_set_text(symbol, g_apps[id].symbol);
  lv_obj_set_style_text_font(symbol, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(symbol, lv_color_hex(0x14262a), 0);
  lv_obj_center(symbol);
  label = make_label(button, g_apps[id].name, theme_primary(), 16);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, size);
  lv_obj_set_height(label, 22);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(label, 0, size + 3);
  return button;
}

static void term_render_icons(bool animate)
{
  int i;
  lv_obj_clean(g_desktop.grid);
  memset(g_terminal.icons, 0, sizeof(g_terminal.icons));
  for (i = 0; i < g_terminal.count; i++)
    {
      struct term_item_s *item = &g_terminal.layout.items[i];
      g_terminal.icons[i] = term_icon(g_desktop.grid, i, item->x, item->y,
                                      item->size, term_icon_event);
      if (item->pocket)
        {
          lv_obj_add_flag(g_terminal.icons[i], LV_OBJ_FLAG_HIDDEN);
        }
      else if (animate)
        {
          terminal_enter(g_terminal.icons[i], i * 25);
        }
    }
  term_update_badges();
}

static void term_size(lv_event_t *e)
{
  int id = g_terminal.selected;
  if (id < 0 || id >= g_terminal.count)
    {
      return;
    }
  g_terminal.layout.items[id].size =
    lv_slider_get_value(lv_event_get_target(e));
  term_constrain(&g_terminal.layout.items[id]);
  g_terminal.dirty = true;
  term_render_icons(false);
  term_select(id);
}

static void term_stash(lv_event_t *e)
{
  int id = g_terminal.selected;
  LV_UNUSED(e);
  if (id >= 0 && id < g_terminal.count)
    {
      g_terminal.layout.items[id].pocket = 1;
      g_terminal.dirty = true;
      term_render_icons(false);
      show_toast("已收入口袋；点击保存可保留到下次开机。");
    }
}

static void term_done(lv_event_t *e)
{
  LV_UNUSED(e);
  g_terminal.editing = false;
  g_terminal.selected = -1;
  lv_obj_add_flag(g_terminal.editor, LV_OBJ_FLAG_HIDDEN);
  term_render_icons(false);
  lv_label_set_text(g_desktop.home_hint,
                   "轻点打开应用 · 长按自由摆放 · 口袋收纳常用工具");
}

static void terminal_home(void)
{
  if (g_desktop.grid != NULL)
    {
      terminal_enter(g_desktop.grid, 0);
    }
}

static void term_restore(lv_event_t *e)
{
  int id = (intptr_t)lv_event_get_user_data(e);
  g_terminal.layout.items[id].pocket = 0;
  g_terminal.dirty = true;
  term_render_icons(false);
  /* Keep the callback target alive until the event returns. */
  lv_obj_add_flag(lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);
  show_toast("已放回桌面；点击保存可保留布局。");
}

static void term_drawer_icon(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
      term_launch((intptr_t)lv_event_get_user_data(e));
    }
}

static lv_obj_t *term_drawer(const char *title, const char *hint)
{
  lv_obj_t *card = panel_card(title);
  lv_obj_t *content;
  if (card == NULL)
    {
      return NULL;
    }
  lv_obj_set_size(card, 940, 500);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -18);
  term_label(card, hint, 24, 54, 16, true);
  content = lv_obj_create(card);
  lv_obj_set_pos(content, 16, 90);
  lv_obj_set_size(content, 908, 390);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  terminal_enter(card, 0);
  return content;
}

static void term_pocket(lv_event_t *e)
{
  lv_obj_t *content;
  int i;
  int slot = 0;
  LV_UNUSED(e);
  content = term_drawer("口袋收纳", "应用仍可直接打开；点击放回桌面恢复快捷方式。");
  if (content == NULL) return;
  for (i = 0; i < g_terminal.count; i++)
    {
      if (!g_terminal.layout.items[i].pocket) continue;
      term_icon(content, i, 22 + (slot % 7) * 126,
                8 + (slot / 7) * 166, 82, term_drawer_icon);
      term_button(content, "放回桌面", 12 + (slot % 7) * 126,
                  124 + (slot / 7) * 166, 104, 34, term_restore, i);
      slot++;
    }
  if (slot == 0)
    {
      term_label(content, "口袋还是空的", 350, 108, 28, false);
      term_label(content, "长按桌面图标，再拖到右下角口袋，或点击收纳。",
                 205, 165, 16, true);
    }
}

static void qpk_clicked(lv_event_t *e)
{
  lv_obj_t *content;
  int i;
  LV_UNUSED(e);
  content = term_drawer("所有应用", "每个应用都有独立入口；桌面与口袋共享同一份应用。");
  if (content == NULL) return;
  for (i = 0; i < g_terminal.count; i++)
    {
      lv_obj_t *icon = term_icon(content, i, 22 + (i % 7) * 126,
                                  8 + (i / 7) * 128, 82,
                                  term_drawer_icon);
      terminal_enter(icon, i * 20);
    }
}

static void term_read_all(lv_event_t *e)
{
  LV_UNUSED(e);
  g_terminal.unread = 0;
  term_update_badges();
  lv_obj_add_state(lv_event_get_target(e), LV_STATE_DISABLED);
}

static void term_messages(lv_event_t *e)
{
  lv_obj_t *content;
  unsigned int total;
  unsigned int i;
  LV_UNUSED(e);
  content = term_drawer("消息中心", "系统事件与应用提示 · 本次开机最多保留最近 8 条");
  if (content == NULL) return;
  term_button(content, "全部已读", 740, 0, 138, 38, term_read_all, 0);
  total = g_terminal.messages_count < TERM_MESSAGES ?
          g_terminal.messages_count : TERM_MESSAGES;
  for (i = 0; i < total; i++)
    {
      unsigned int slot = (g_terminal.messages_count - i - 1) % TERM_MESSAGES;
      lv_obj_t *card = lv_obj_create(content);
      lv_obj_t *label;
      lv_obj_set_pos(card, 8, 50 + i * 82);
      lv_obj_set_size(card, 874, 72);
      style_panel(card, theme_surface());
      lv_obj_set_style_pad_all(card, 12, 0);
      lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
      label = make_label(card, g_terminal.messages[slot], theme_primary(), 16);
      lv_obj_set_width(label, 842);
      lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
      terminal_enter(card, i * 20);
    }
}

static void term_control(lv_event_t *e)
{
  int option = (intptr_t)lv_event_get_user_data(e);
  uint8_t *value = option == 0 ? &g_terminal.layout.light :
                   option == 1 ? &g_terminal.layout.quiet :
                   option == 2 ? &g_terminal.layout.reduced :
                                 &g_terminal.layout.warm;
  *value = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  g_terminal.dirty = true;
  apply_theme();
  /* Rebuild only after the toggle event returns. */
  if (!g_desktop.settings_rebuild_pending &&
      lv_async_call(settings_rebuild_async, NULL) == LV_RESULT_OK)
    {
      g_desktop.settings_rebuild_pending = true;
    }
}

static void settings_rebuild_async(void *data)
{
  LV_UNUSED(data);
  g_desktop.settings_rebuild_pending = false;
  term_render_icons(false);
  settings_clicked(NULL);
}

static void term_reset(lv_event_t *e)
{
  int i;
  LV_UNUSED(e);
  for (i = 0; i < g_terminal.count; i++)
    {
      struct term_item_s *item = &g_terminal.layout.items[i];
      item->size = 76;
      item->x = 12 + (i % 8) * 118;
      item->y = 12 + (i / 8) * 142;
      item->pocket = 0;
    }
  g_terminal.dirty = true;
  term_done(NULL);
  show_toast("图标已恢复默认布局；点击保存以保留。");
}

static void settings_clicked(lv_event_t *e)
{
  static const char *titles[] = {"浅色外观", "消息免打扰", "减少动效", "暖色显示"};
  static const char *hints[] = {"切换桌面与卡片配色", "保留消息，关闭浮动提示",
                              "关闭位移与淡入效果", "叠加轻度暖色，不改变背光"};
  uint8_t values[] = {g_terminal.layout.light, g_terminal.layout.quiet,
                      g_terminal.layout.reduced, g_terminal.layout.warm};
  lv_obj_t *content;
  int i;
  LV_UNUSED(e);
  content = term_drawer("控制中心", "本地控制即时生效；保存后在下次开机恢复。");
  if (content == NULL) return;
  for (i = 0; i < 4; i++)
    {
      int x = 12 + (i % 2) * 446;
      int y = 10 + (i / 2) * 106;
      lv_obj_t *card = lv_obj_create(content);
      lv_obj_t *sw;
      lv_obj_set_pos(card, x, y);
      lv_obj_set_size(card, 426, 90);
      lv_obj_set_style_pad_all(card, 0, 0);
      style_panel(card, theme_surface());
      lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
      term_label(card, titles[i], 18, 14, 20, false);
      term_label(card, hints[i], 18, 52, 16, true);
      sw = lv_switch_create(card);
      lv_obj_set_pos(sw, 340, 18);
      lv_obj_set_size(sw, 60, 32);
      if (values[i]) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, term_control, LV_EVENT_VALUE_CHANGED,
                          (void *)(intptr_t)i);
      terminal_enter(card, i * 30);
    }
  term_label(content, "连接状态", 24, 244, 20, false);
#ifdef CONFIG_ESPRESSIF_EMAC
  term_label(content, "有线网络由系统管理；连接状态请查看智能家居应用。",
             24, 279, 16, true);
#else
  term_label(content, "当前固件为离线桌面；网络应用需要可用网络接口。",
             24, 279, 16, true);
#endif
  term_button(content, "恢复图标布局", 24, 330, 182, 44, term_reset, 0);
  term_button(content, "保存桌面设置", 676, 330, 202, 44, term_save, 0);
}

static void about_clicked(lv_event_t *e)
{
  lv_obj_t *content;
  char info[320];
  LV_UNUSED(e);
  content = term_drawer("设备信息", "ESPClaw 桌面终端 · Terminal 1.0");
  if (content == NULL) return;
  snprintf(info, sizeof(info),
           "ESP32-P4 %s · %d MHz 双核\n\nLVGL 9.2.1 · openvela / NuttX\n"
           "本地应用 %d 个 · 口袋 %d 个\n\n"
           "布局：%s\n"
           "桌面与现有 QuickJS 应用共用触摸和显示运行时。",
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
           "v1",
#else
           "v3",
#endif
           CONFIG_ESPRESSIF_CPU_FREQ_MHZ,
           g_terminal.count, term_pocket_count(),
           g_terminal.dirty ? "有未保存的改动" : "已载入 / 默认布局");
  term_label(content, info, 28, 20, 20, false);
}

static void clock_timer_cb(lv_timer_t *timer)
{
  struct timespec ts;
  LV_UNUSED(timer);
  clock_gettime(CLOCK_MONOTONIC, &ts);
  lv_label_set_text_fmt(g_desktop.clock_label, "已运行 %02lu:%02lu:%02lu",
                       (unsigned long)ts.tv_sec / 3600,
                       (unsigned long)ts.tv_sec / 60 % 60,
                       (unsigned long)ts.tv_sec % 60);
}

static void desktop_ui_create(void)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *button;
  size_t font_size = (size_t)(g_desktop_font_end - g_desktop_font_start);
  int i;
  memset(&g_desktop, 0, sizeof(g_desktop));
  memset(&g_terminal, 0, sizeof(g_terminal));
  g_terminal.selected = -1;
  g_terminal.drag_id = -1;
  g_desktop.font16 = lv_tiny_ttf_create_data_ex(g_desktop_font_start,
      font_size, 16, LV_FONT_KERNING_NORMAL, DESKTOP_FONT_CACHE_GLYPHS);
  g_desktop.font20 = lv_tiny_ttf_create_data_ex(g_desktop_font_start,
      font_size, 20, LV_FONT_KERNING_NORMAL, DESKTOP_FONT_CACHE_GLYPHS);
  g_desktop.font28 = lv_tiny_ttf_create_data_ex(g_desktop_font_start,
      font_size, 28, LV_FONT_KERNING_NORMAL, DESKTOP_FONT_CACHE_GLYPHS);
  g_desktop.screen = screen;
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(screen, 0, 0);
  qpk_scan();
  g_terminal.count = 7 + g_desktop.nqpk;
  for (i = 0; i < g_desktop.nqpk; i++)
    {
      g_apps[i + 7].name = g_desktop.qpk[i].name;
      g_apps[i + 7].key = g_desktop.qpk[i].dir;
      g_apps[i + 7].symbol = LV_SYMBOL_FILE;
      g_apps[i + 7].color = 0x9cafd2;
    }
  term_defaults();
  term_load();
  g_desktop.light_theme = g_terminal.layout.light != 0;

  g_desktop.statusbar = lv_obj_create(screen);
  lv_obj_set_size(g_desktop.statusbar, 1024, 56);
  lv_obj_set_style_radius(g_desktop.statusbar, 0, 0);
  lv_obj_set_style_border_width(g_desktop.statusbar, 0, 0);
  lv_obj_set_style_pad_all(g_desktop.statusbar, 0, 0);
  lv_obj_remove_flag(g_desktop.statusbar, LV_OBJ_FLAG_SCROLLABLE);
  term_label(g_desktop.statusbar, "ESPClaw", 28, 17, 20, false);
  g_desktop.clock_label = term_label(g_desktop.statusbar, "", 194, 19, 16, true);
  button = term_button(g_desktop.statusbar, "消息  0", 750, 8, 104, 40,
                       term_messages, 0);
  g_terminal.badge = lv_obj_get_child(button, 0);
  term_button(g_desktop.statusbar, "控制中心", 866, 8, 130, 40,
              settings_clicked, 0);
  g_desktop.home_title = term_label(screen, "我的桌面", 38, 84, 28, false);
  g_desktop.home_hint = term_label(screen,
      "轻点打开应用 · 长按自由摆放 · 口袋收纳常用工具", 40, 126, 16, true);
  g_desktop.grid = lv_obj_create(screen);
  lv_obj_set_pos(g_desktop.grid, 32, 166);
  lv_obj_set_size(g_desktop.grid, TERM_AREA_W, TERM_AREA_H);
  lv_obj_set_style_bg_opa(g_desktop.grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_desktop.grid, 0, 0);
  lv_obj_set_style_pad_all(g_desktop.grid, 0, 0);
  lv_obj_remove_flag(g_desktop.grid, LV_OBJ_FLAG_SCROLLABLE);

  g_terminal.dock = lv_obj_create(screen);
  lv_obj_set_pos(g_terminal.dock, 32, 494);
  lv_obj_set_size(g_terminal.dock, 750, 78);
  style_panel(g_terminal.dock, theme_card());
  lv_obj_set_style_pad_all(g_terminal.dock, 0, 0);
  lv_obj_remove_flag(g_terminal.dock, LV_OBJ_FLAG_SCROLLABLE);
  term_button(g_terminal.dock, "所有应用", 16, 16, 134, 46, qpk_clicked, 0);
  term_button(g_terminal.dock, "消息中心", 162, 16, 134, 46, term_messages, 0);
  term_label(g_terminal.dock, "桌面终端 / 本地优先", 324, 29, 16, true);
  term_button(g_terminal.dock, "保存布局", 590, 16, 140, 46, term_save, 0);
  button = term_button(screen, "口袋收纳  0", 800, 494, 192, 78, term_pocket, 0);
  g_terminal.pocket_label = lv_obj_get_child(button, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x47c7ad), 0);

  g_terminal.editor = lv_obj_create(screen);
  lv_obj_set_pos(g_terminal.editor, 32, 484);
  lv_obj_set_size(g_terminal.editor, 750, 98);
  style_panel(g_terminal.editor, theme_card());
  lv_obj_set_style_pad_all(g_terminal.editor, 0, 0);
  lv_obj_remove_flag(g_terminal.editor, LV_OBJ_FLAG_SCROLLABLE);
  g_terminal.edit_label = term_label(g_terminal.editor, "", 18, 10, 16, false);
  g_terminal.size_label = term_label(g_terminal.editor, "", 18, 52, 16, true);
  g_terminal.size_slider = lv_slider_create(g_terminal.editor);
  lv_obj_set_pos(g_terminal.size_slider, 160, 40);
  lv_obj_set_size(g_terminal.size_slider, 196, 16);
  lv_slider_set_range(g_terminal.size_slider, TERM_MIN_SIZE, TERM_MAX_SIZE);
  lv_obj_add_event_cb(g_terminal.size_slider, term_size,
                      LV_EVENT_VALUE_CHANGED, NULL);
  term_button(g_terminal.editor, "收纳", 392, 28, 94, 46, term_stash, 0);
  term_button(g_terminal.editor, "保存", 498, 28, 94, 46, term_save, 0);
  term_button(g_terminal.editor, "完成", 604, 28, 122, 46, term_done, 0);
  lv_obj_add_flag(g_terminal.editor, LV_OBJ_FLAG_HIDDEN);

  g_desktop.panel = lv_obj_create(screen);
  lv_obj_set_size(g_desktop.panel, 1024, 600);
  lv_obj_set_style_pad_all(g_desktop.panel, 0, 0);
  lv_obj_set_style_border_width(g_desktop.panel, 0, 0);
  lv_obj_set_style_radius(g_desktop.panel, 0, 0);
  lv_obj_remove_flag(g_desktop.panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_desktop.panel, LV_OBJ_FLAG_HIDDEN);
  g_terminal.shade = lv_obj_create(lv_layer_top());
  lv_obj_set_size(g_terminal.shade, 1024, 600);
  lv_obj_set_style_bg_color(g_terminal.shade, lv_color_hex(0xffae52), 0);
  lv_obj_set_style_border_width(g_terminal.shade, 0, 0);
  lv_obj_set_style_radius(g_terminal.shade, 0, 0);
  lv_obj_remove_flag(g_terminal.shade, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  apply_theme();
  term_render_icons(true);
  terminal_enter(g_desktop.home_title, 30);
  terminal_enter(g_terminal.dock, 130);
  lv_timer_create(clock_timer_cb, 1000, NULL);
  clock_timer_cb(NULL);
  terminal_notify("桌面已就绪。长按图标可自由摆放，口袋用于收纳应用快捷方式。");
  printf("desktop: terminal 1.0 ready, apps=%d, pocket=%d\n",
         g_terminal.count, term_pocket_count());
}
