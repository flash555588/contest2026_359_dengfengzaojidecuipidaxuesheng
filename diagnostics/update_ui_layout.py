"""Apply the reviewed desktop layout changes to the separate UI overlay."""
from pathlib import Path

workspace = Path(__file__).resolve().parent.parent
source = workspace / 'diagnostics/ui-baseline'
target = workspace / '04-v3-20260913/ui-layout-fix/overlay/apps/system/desktop'
main = (source / 'desktop_main.c').read_text(encoding='utf-8')
# QuickJS derives its palette from this transparent content object's color.
# Set the color explicitly even though its pixels come from the parent.
main = main.replace('  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);',
                    '  lv_obj_set_style_bg_color(content, lv_color_hex(theme_card()), 0);\n  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);')
main = main.replace('make_label(g_desktop.panel, text, 0xffffff, 20)',
                    'make_label(g_desktop.panel, text, theme_primary(), 20)')
main = main.replace('  lv_obj_set_style_radius(g_desktop.toast, 8, 0);',
                    '  lv_obj_set_style_radius(g_desktop.toast, 12, 0);\n  lv_obj_set_style_max_width(g_desktop.toast, 880, 0);')
# Score text must remain in separate columns as the game progresses.
main = main.replace("'最高 0', 150, 10", "'最高 0', 270, 10")
main = main.replace('  "const statusLabel = ui.text(',
                    '  "ui.setSize(scoreLabel, 230, 32);\\n"\n  "ui.setSize(bestLabel, 360, 32);\\n"\n  "const statusLabel = ui.text(')
main = main.replace('lv_obj_set_style_radius(button, 18, 0);', 'lv_obj_set_style_radius(button, 12, 0);')
main = main.replace('  lv_obj_set_style_border_color(button, lv_color_hex(0x3b484b), 0);',
                    '  lv_obj_set_style_border_color(button, lv_color_hex(theme_secondary()), 0);\n  lv_obj_set_style_border_opa(button, 45, 0);')
main = main.replace('  style_panel(card, theme_card());', '  style_panel(card, theme_card());\n  lv_obj_set_style_pad_all(card, 0, 0);')
main = main.replace('  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 22, 14);',
                    '  lv_obj_set_width(label, PANEL_WIDTH - 120);\n  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);\n  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 24, 18);')
main = main.replace('  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -12, -4);',
                    '  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -20, 14);')
main = main.replace('  "ui.text(info.packageName',
                    '  "const left = Math.floor((ui.getSize().width - 400) / 2);\\n"\n  "ui.text(info.packageName')
main = main.replace("info.versionName, 24, 60, 16", "info.versionName, left, 104, 16")
main = main.replace('info.name, 472, 92, 28', 'info.name, left, 52, 28')
main = main.replace('  "ui.number(String(launches), 472, 126, 120, 48, ui.primary);\\n"',
'''  "ui.panel(left, 150, 400, 96, ui.surface, 16, 255);\\n"
  "ui.number(String(launches), left + 20, 164, 80, 64, ui.primary);\\n"''')
main = main.replace("'本次启动次数', 606, 142, 16", "'累计启动次数', left + 120, 180, 20")
main = main.replace("'Toast 提示', 362, 150, 300, 54", "'Toast 提示', left, 274, 400, 54")
main = main.replace("'对话框', 362, 218, 300, 54", "'对话框', left, 344, 400, 54")
main = main.replace("'已运行 0 秒', 456, 338, 16", "'已运行 0 秒', left, 426, 16")
start = main.index('static void qpk_show_dialog(const char *text)')
end = main.index('\nstatic ', start + 1)
dialog = main[start:end]
dialog = dialog.replace('shade = lv_obj_create(g_desktop.panel);',
                        'shade = lv_obj_create(lv_layer_top());')
dialog = dialog.replace('  lv_obj_set_size(box, 500, 240);', '  lv_obj_set_size(box, 560, 320);')
dialog = dialog.replace('  style_panel(box, theme_card());', '''  style_panel(box, theme_card());
  lv_obj_set_style_pad_all(box, 24, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *body = lv_obj_create(box);
  if (!body) { dialog_cancel(); return; }
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, 508, 202);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);''')
dialog = dialog.replace('make_label(box, text ?', 'make_label(body, text ?')
dialog = dialog.replace('lv_obj_set_width(label, 440)', 'lv_obj_set_width(label, 508)')
dialog = dialog.replace('LV_ALIGN_TOP_LEFT, 12, 12', 'LV_ALIGN_TOP_LEFT, 0, 0')
dialog = dialog.replace('LV_ALIGN_BOTTOM_RIGHT, -8, -8', 'LV_ALIGN_BOTTOM_RIGHT, 0, 0')
main = main[:start] + dialog + main[end:]
main = main.replace('#include "glass_ui.inc"', '#include "glass_ui.inc"\n#include "glass_ui_probe.inc"')
main = main.replace('  uint32_t idle = 10;', '  uint32_t idle = 10;\n\n  desktop_ui_probe_service();')
marker = '  launch_camera = argc > 1 && strcmp(argv[1], "camera") == 0;'
main = main.replace(marker, '''  if (argc == 3 && strcmp(argv[1], "ui") == 0)
    return desktop_ui_probe_request(argv[2]);

''' + marker)
(target / 'desktop_main.c').write_text(main, encoding='utf-8')

ui = (source / 'glass_ui.inc').read_text(encoding='utf-8')
ui = ui.replace('  lv_obj_t *label = make_label(button, text, theme_primary(), 16);\n  lv_obj_center(label);',
'''  lv_obj_t *label = make_label(button, text, theme_primary(), 16);
  lv_obj_set_width(label, w - 24);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);''')
marker = '#include "glass_wifi.inc"'
helpers = '''/* Shared form styling includes the placeholder, which otherwise uses
 * LVGL's Latin-only theme font. Keep keyboard symbols in Montserrat. */
static void glass_input_style(lv_obj_t *input)
{
  lv_obj_set_style_text_font(input, zh_font(20), LV_PART_MAIN);
  lv_obj_set_style_text_font(input, zh_font(20), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_text_color(input, lv_color_hex(theme_primary()), LV_PART_MAIN);
  lv_obj_set_style_text_color(input, lv_color_hex(theme_secondary()), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(input, lv_color_hex(theme_surface()), 0);
  lv_obj_set_style_bg_opa(input, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(input, lv_color_hex(theme_secondary()), 0);
  lv_obj_set_style_border_opa(input, 55, 0);
  lv_obj_set_style_border_width(input, 1, 0);
  lv_obj_set_style_border_color(input, lv_color_hex(theme_accent()), LV_STATE_FOCUSED);
  lv_obj_set_style_border_opa(input, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_radius(input, 12, 0);
  lv_obj_set_style_pad_hor(input, 14, 0);
  lv_obj_set_style_pad_ver(input, 10, 0);
}

static void glass_keyboard_style(lv_obj_t *keyboard)
{
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(theme_card()), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(keyboard, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_all(keyboard, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_row(keyboard, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_column(keyboard, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(theme_surface()), LV_PART_ITEMS);
  lv_obj_set_style_text_color(keyboard, lv_color_hex(theme_primary()), LV_PART_ITEMS);
  lv_obj_set_style_radius(keyboard, 7, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
  lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
}

'''
ui = ui.replace(marker, helpers + marker)
start = ui.index('static void glass_settings(lv_event_t *e)\n{')
end = ui.index('#ifdef CONFIG_SYSTEM_GLASS_PLAYER\n#include "glass_player.inc"', start)
settings = '''static void glass_settings(lv_event_t *e)
{
  LV_UNUSED(e);
  glass_preferences_update();
  g_control_preferences = true;
  lv_obj_t *card = panel_card("控制面板");
  if (!card) return;
  g_control_card = card;
  lv_obj_add_event_cb(card, glass_control_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_clean(card);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, 0, 0);
  lv_obj_set_size(card, 1024, 600);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(g_desktop.light_theme ? 0xf3f4f8 : 0x191d28), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_remove_event_cb(g_desktop.panel, glass_control_outside);

  glass_text(card, "控制面板", 40, 28, 240, 28, theme_primary());
  g_preferences_status = glass_text(card, glass_preferences_status_text(),
                                    298, 36, 460, 16, theme_secondary());
  g_preferences_shown = glass_preferences_display_state();
  lv_obj_add_event_cb(g_preferences_status, glass_preferences_status_deleted, LV_EVENT_DELETE, NULL);
  glass_button(card, "返回桌面", 864, 24, 120, theme_surface(), glass_settings_back, NULL);

  lv_obj_t *display = glass_box(card, 40, 96, 544, 208, theme_card(), 20);
  glass_text(display, "屏幕显示", 24, 20, 280, 20, theme_primary());
  glass_text(display, "亮度", 24, 62, 200, 16, theme_secondary());
  lv_obj_t *value = glass_text(display, "100%", 404, 60, 112, 20, theme_primary());
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_t *brightness = lv_slider_create(display);
  g_glass.brightness_slider = brightness;
  lv_obj_set_pos(brightness, 40, 110);
  lv_obj_set_size(brightness, 464, 8);
  lv_obj_set_ext_click_area(brightness, 18);
  lv_obj_set_style_radius(brightness, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(brightness, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightness, lv_color_hex(theme_surface()), LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightness, lv_color_hex(theme_accent()), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightness, lv_color_hex(0xffffff), LV_PART_KNOB);
  lv_obj_set_style_pad_all(brightness, 8, LV_PART_KNOB);
  lv_obj_set_style_radius(brightness, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_border_width(brightness, 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(brightness, lv_color_hex(theme_accent()), LV_PART_KNOB);
  lv_obj_set_style_shadow_width(brightness, 0, LV_PART_KNOB);
  lv_slider_set_range(brightness, 10, 100);
  lv_slider_set_value(brightness, g_screen_brightness, LV_ANIM_OFF);
#ifdef GLASS_PWM_BACKLIGHT
  lv_label_set_text_fmt(value, "%d%%", g_screen_brightness);
  lv_obj_add_event_cb(brightness, glass_brightness_changed, LV_EVENT_RELEASED, value);
  glass_text(display, "拖动滑块调整屏幕亮度", 24, 158, 496, 16, theme_secondary());
#else
  lv_obj_add_state(brightness, LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(brightness, lv_color_hex(g_desktop.light_theme ? 0xaab2c5 : 0x64718a),
                            LV_PART_INDICATOR | LV_STATE_DISABLED);
  lv_obj_set_style_border_color(brightness, lv_color_hex(theme_secondary()),
                                LV_PART_KNOB | LV_STATE_DISABLED);
  glass_text(display, "当前屏幕使用固定亮度", 24, 158, 496, 16, theme_secondary());
#endif

  lv_obj_t *appearance = glass_box(card, 40, 324, 544, 236, theme_card(), 20);
  glass_text(appearance, "个性化", 24, 20, 180, 20, theme_primary());
  glass_text(appearance, "浅色外观", 24, 70, 160, 20, theme_primary());
  lv_obj_t *sw = lv_switch_create(appearance);
  g_glass.settings_switch = sw;
  lv_obj_set_pos(sw, 200, 64); lv_obj_set_size(sw, 60, 34);
  glass_control_switch_style(sw);
  if (g_desktop.light_theme) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, theme_changed, LV_EVENT_VALUE_CHANGED, NULL);
  glass_text(appearance, "减少动效", 292, 70, 140, 20, theme_primary());
  sw = lv_switch_create(appearance);
  g_glass.motion_switch = sw;
  lv_obj_set_pos(sw, 460, 64); lv_obj_set_size(sw, 60, 34);
  glass_control_switch_style(sw);
  if (g_glass.reduced_motion) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, glass_motion_changed, LV_EVENT_VALUE_CHANGED, NULL);
  glass_text(appearance, "主视觉配色", 24, 124, 160, 16, theme_secondary());
  const uint32_t colors[] = {0x6c619e, 0xb77860, 0x498d82};
  for (unsigned i = 0; i < 3; i++)
    {
      lv_obj_t *choice = glass_button(appearance, "", 216 + i * 104, 112, 88,
                                      colors[i], glass_palette_select, (void *)(uintptr_t)i);
      lv_obj_set_style_border_width(choice, g_glass.palette == i ? 2 : 0, 0);
      lv_obj_set_style_border_color(choice, lv_color_hex(0xffffff), 0);
      lv_obj_set_style_border_opa(choice, LV_OPA_COVER, 0);
      if (g_glass.palette == i)
        {
          lv_obj_t *check = glass_symbol(choice, LV_SYMBOL_OK, 0, 0, 0xffffff);
          lv_obj_center(check);
        }
    }
  glass_text(appearance, "桌面小组件", 24, 184, 176, 16, theme_secondary());
  lv_obj_t *dropdown = lv_dropdown_create(appearance);
  g_glass.widget_dropdown = dropdown;
  lv_dropdown_set_options(dropdown, "空气质量\\n环境湿度\\n系统运行");
  lv_dropdown_set_selected(dropdown, g_glass.widget);
  lv_obj_set_pos(dropdown, 216, 174); lv_obj_set_size(dropdown, 304, 44);
  glass_input_style(dropdown);
  lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_24, LV_PART_INDICATOR);
  lv_obj_t *list = lv_dropdown_get_list(dropdown);
  lv_obj_set_style_text_font(list, zh_font(20), 0);
  lv_obj_set_style_text_color(list, lv_color_hex(theme_primary()), 0);
  lv_obj_set_style_bg_color(list, lv_color_hex(theme_card()), 0);
  lv_obj_set_style_radius(list, 12, 0);
  lv_obj_add_event_cb(dropdown, glass_widget_select, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *connectivity = glass_box(card, 608, 96, 376, 208, theme_card(), 20);
  glass_text(connectivity, "网络与连接", 24, 20, 328, 20, theme_primary());
  glass_button(connectivity, "Wi-Fi 网络", 24, 70, 328, theme_surface(), glass_system_page_open, NULL);
  glass_button(connectivity, "蓝牙设备", 24, 136, 328, theme_surface(), glass_system_page_open, (void *)2);
  lv_obj_t *system = glass_box(card, 608, 324, 376, 236, theme_card(), 20);
  glass_text(system, "系统管理", 24, 20, 328, 20, theme_primary());
  glass_button(system, "存储与文件", 24, 70, 328, theme_surface(), glass_system_page_open, (void *)1);
  glass_button(system, "系统性能", 24, 136, 328, theme_surface(), glass_system_page_open, (void *)3);
  glass_text(system, "查看文件、空间和运行状态", 24, 202, 328, 16, theme_secondary());
}

'''
ui = ui[:start] + settings + ui[end:]
# Reserve separate columns for the main sensor value and secondary readings.
ui = ui.replace('glass_number(card, "--", 22, 52, 224, false, primary)',
                'glass_number(card, "--", 22, 60, 110, false, primary)')
ui = ui.replace('glass_text(card, "PM2.5 · 等待传感器", 22, 137, 224, 16, secondary)',
                'glass_text(card, "PM2.5 · 等待传感器", 22, 141, 224, 16, secondary)')
ui = ui.replace('glass_text(card, "温度 / C", 142, 42, 104, 16, secondary)',
                'glass_text(card, "温度 / C", 142, 58, 104, 16, secondary)')
ui = ui.replace('glass_text(card, "--", 168, 62, 78, 20, secondary)',
                'glass_text(card, "--", 142, 80, 104, 20, secondary)')
ui = ui.replace('glass_text(card, "湿度 --%", 142, 100, 104, 16, secondary)',
                'glass_text(card, "湿度 --%", 142, 110, 104, 16, secondary)')
# Give all ordinary child labels a Chinese font to inherit.
ui = ui.replace('  g_desktop.screen = lv_screen_active();',
                '  g_desktop.screen = lv_screen_active();\n  lv_obj_set_style_text_font(g_desktop.screen, zh_font(20), 0);')
(target / 'glass_ui.inc').write_text(ui, encoding='utf-8')

wifi = (source / 'glass_wifi.inc').read_text(encoding='utf-8')
wifi = wifi.replace('  g_wifi_ssid = lv_textarea_create(card);',
                    '  g_wifi_ssid = lv_textarea_create(card);\n  glass_input_style(g_wifi_ssid);')
wifi = wifi.replace('  g_wifi_password = lv_textarea_create(card);',
                    '  g_wifi_password = lv_textarea_create(card);\n  glass_input_style(g_wifi_password);')
wifi = wifi.replace('  g_wifi_keyboard = lv_keyboard_create(card);',
                    '  g_wifi_keyboard = lv_keyboard_create(card);\n  glass_keyboard_style(g_wifi_keyboard);')
wifi = wifi.replace('glass_box(card, 20, 120, 350, 330, theme_surface(), 0)',
                    'glass_box(card, 24, 120, 344, 356, theme_surface(), 12)')
wifi = wifi.replace('lv_obj_set_size(g_wifi_ssid, 310, 46)', 'lv_obj_set_size(g_wifi_ssid, 488, 48)')
wifi = wifi.replace('lv_obj_set_pos(g_wifi_password, 390, 176)', 'lv_obj_set_pos(g_wifi_password, 390, 182)')
wifi = wifi.replace('lv_obj_set_size(g_wifi_password, 310, 46)', 'lv_obj_set_size(g_wifi_password, 344, 48)')
wifi = wifi.replace('"连接", 720, 176, 130', '"连接", 750, 182, 128')
wifi = wifi.replace('lv_obj_set_size(g_wifi_keyboard, 460, 210)', 'lv_obj_set_size(g_wifi_keyboard, 488, 228)')
wifi = wifi.replace('LV_ALIGN_TOP_LEFT, 390, 240', 'LV_ALIGN_TOP_LEFT, 390, 248')
wifi = wifi.replace('lv_textarea_set_placeholder_text(g_wifi_ssid, "SSID")',
                    'lv_textarea_set_placeholder_text(g_wifi_ssid, "网络名称（SSID）")')
wifi = wifi.replace('  lv_obj_add_flag(g_wifi_list, LV_OBJ_FLAG_SCROLLABLE);',
                    '  lv_obj_add_flag(g_wifi_list, LV_OBJ_FLAG_SCROLLABLE);\n  lv_obj_set_scroll_dir(g_wifi_list, LV_DIR_VER);')
wifi = wifi.replace('  for (size_t i = 0; i < g_wifi_result.count; i++)',
'''  if (g_wifi_result.count == 0)
    glass_text(g_wifi_list, "点击扫描查找附近网络", 16, 20, 310, 16, theme_secondary());
  for (size_t i = 0; i < g_wifi_result.count; i++)''')
(target / 'glass_wifi.inc').write_text(wifi, encoding='utf-8')

for name in ['glass_files.inc', 'glass_player.inc']:
    text = (source / name).read_text(encoding='utf-8')
    import re
    text = re.sub(r'(?m)^(\s*)(\w+) = lv_textarea_create\(card\);',
                  r'\1\2 = lv_textarea_create(card);\1glass_input_style(\2);', text)
    text = text.replace('      lv_obj_t *preview = lv_textarea_create(card);',
                        '      lv_obj_t *preview = lv_textarea_create(card);\n      glass_input_style(preview);')
    text = text.replace('      lv_obj_t *keyboard = lv_keyboard_create(card);',
                        '      lv_obj_t *keyboard = lv_keyboard_create(card);\n      glass_keyboard_style(keyboard);')
    text = text.replace('  g_music_keyboard = lv_keyboard_create(card);',
                        '  g_music_keyboard = lv_keyboard_create(card);\n  glass_keyboard_style(g_music_keyboard);')
    if name == 'glass_files.inc':
        text = text.replace('lv_obj_set_style_radius(g_files_shell, 0, 0)',
                            'lv_obj_set_style_radius(g_files_shell, 24, 0)')
        text = text.replace('lv_obj_set_style_radius(row, 4, 0)', 'lv_obj_set_style_radius(row, 10, 0)')
        text = text.replace('      lv_obj_set_width(label, 745);',
                            '      lv_obj_set_width(label, 745);\n      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);')
        text = text.replace('glass_box(card, 24, 156, 850, 250, theme_surface(), 0)',
                            'glass_box(card, 24, 156, 850, 250, theme_surface(), 12)')
    (target / name).write_text(text, encoding='utf-8')

runtime = (source / 'qpk_runtime.c').read_text(encoding='utf-8')
runtime = runtime.replace('#define QPK_MAX_EVENTS    16',
'''/* The bundled Home Assistant screen retains 24 buttons, including hidden
 * settings controls. Keep a bounded pool with room for its complete UI. */
#define QPK_MAX_EVENTS    32''')
runtime = runtime.replace('  lv_label_set_text(label, title);\n  lv_obj_set_pos(label, 24, 22);',
'''  lv_label_set_text(label, title);
  lv_obj_set_width(label, root_width - 256);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(label, 24, 22);''')
runtime = runtime.replace('g_qpk.font_cb(20), 0);\n    }\n\n  keyboard =',
'''g_qpk.font_cb(20), 0);
      lv_obj_set_style_text_font(g_qpk.input_textarea,
                                 g_qpk.font_cb(20), LV_PART_TEXTAREA_PLACEHOLDER);
    }

  keyboard =''')
start = runtime.index('static JSValue js_ui_button(')
end = runtime.index('\nstatic ', start + 1)
button = runtime[start:end]
button = button.replace('  lv_obj_center(label);', '''  lv_obj_set_width(label, lv_pct(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);''')
runtime = runtime[:start] + button + runtime[end:]
# A button may change its background after creation (HA category selection).
# Recompute the label color at that point as well.
runtime = runtime.replace('      lv_obj_set_style_bg_color(widget, lv_color_hex(color), 0);', '''      lv_obj_set_style_bg_color(widget, lv_color_hex(color), 0);
      if (g_qpk.widget_types[handle - 1] == QPK_WIDGET_BUTTON)
        {
          lv_obj_t *label = lv_obj_get_child(widget, 0);
          unsigned brightness = ((color >> 16) & 255) +
                                ((color >> 8) & 255) + (color & 255);
          if (label) lv_obj_set_style_text_color(label,
              lv_color_hex(brightness > 510 ? 0x172033 : 0xffffff), 0);
        }''')
# Input dialogs follow the current application theme and keep readable text.
start = runtime.index('static JSValue js_prompt_input(')
end = runtime.index('\nstatic ', start + 1)
prompt = runtime[start:end]
prompt = prompt.replace('lv_color_hex(0x102129)', 'lv_color_hex(g_qpk.surface_color)')
prompt = prompt.replace('lv_color_hex(0xf4fbfc)', 'lv_color_hex(g_qpk.primary_color)')
prompt = prompt.replace('lv_color_hex(0x252c2f)', 'lv_color_hex(g_qpk.surface_color)')
prompt = prompt.replace('  lv_textarea_set_one_line(g_qpk.input_textarea, true);', '''  lv_textarea_set_one_line(g_qpk.input_textarea, true);
  lv_obj_set_style_text_color(g_qpk.input_textarea, lv_color_hex(g_qpk.primary_color), 0);
  lv_obj_set_style_text_color(g_qpk.input_textarea, lv_color_hex(g_qpk.secondary_color), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_pad_hor(g_qpk.input_textarea, 14, 0);
  lv_obj_set_style_pad_ver(g_qpk.input_textarea, 10, 0);''')
prompt = prompt.replace('  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);', '''  lv_obj_set_style_bg_color(keyboard, lv_color_hex(g_qpk.surface_color), 0);
  lv_obj_set_style_pad_all(keyboard, 12, 0);
  lv_obj_set_style_pad_row(keyboard, 8, 0);
  lv_obj_set_style_pad_column(keyboard, 6, 0);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(g_qpk.surface_color), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(g_qpk.primary_color == 0xffffff ? 0x414d66 : 0xffffff), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_text_color(keyboard, lv_color_hex(g_qpk.primary_color), LV_PART_ITEMS);
  lv_obj_set_style_radius(keyboard, 8, LV_PART_ITEMS);
  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);''')
runtime = runtime[:start] + prompt + runtime[end:]
(target / 'qpk_runtime.c').write_text(runtime, encoding='utf-8')

# Keep the generated resource reproducible from the shipped JS bytes.
import re
resource = (source / 'homeassistant_resource.c').read_text(encoding='utf-8')
ha = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', resource.split('};')[0])).decode('utf-8')
ha = ha.replace('const HUB_BG = 0x43cdb8;', 'const HUB_BG = ui.primary === 0xffffff ? 0x191d28 : 0xf3f4f8;')
ha = ha.replace('const HUB_CARD = 0xffffff;', 'const HUB_CARD = ui.surface;')
ha = ha.replace('const HUB_PICK = 0xd5f5ed;', 'const HUB_PICK = ui.primary === 0xffffff ? 0x365953 : 0xd5f5ed;')
ha = ha.replace('const HUB_TEXT = 0x141414;', 'const HUB_TEXT = ui.primary;')
ha = ha.replace('const HUB_MUTED = 0x285d54;', 'const HUB_MUTED = ui.secondary;')
ha = ha.replace('const BTN_W = 96;', 'const BTN_W = 120;')
ha = ha.replace("const rows = [];", """ui.setSize(statusLabel, W - 208, 24);
const emptyLabel = ui.text('暂无设备\\n点击设置，配置地址和访问令牌后同步', GRID_X + 24, GRID_Y + 60, 20, HUB_MUTED);
ui.setSize(emptyLabel, W - GRID_X - 48, 100);
const rows = [];""")
ha = ha.replace('  const pages = Math.max(1,', '''  if (!visibleIndices.length && !settingsOpen) ui.show(emptyLabel);
  else ui.hide(emptyLabel);
  const pages = Math.max(1,''')
ha = ha.replace("  deviceStates.push(ui.text('', x + 24, y + CARD_H - 36, 16, HUB_MUTED));", """  deviceStates.push(ui.text('', x + 24, y + CARD_H - 36, 16, HUB_MUTED));
  ui.setSize(deviceLabels[i], CARD_W - 48, 28);
  ui.setSize(deviceStates[i], CARD_W - 48, 22);""")
data = ha.encode('utf-8')
start, end = resource.index('{') + 1, resource.index('};')
encoded = '\n' + ''.join('  ' + ', '.join('0x%02x' % b for b in data[i:i+12]) + ',\n' for i in range(0, len(data), 12)) + '  0x00\n'
resource = resource.replace('*len = sizeof(g_homeassistant_app_js);', '*len = sizeof(g_homeassistant_app_js) - 1;')
(target / 'homeassistant_resource.c').write_text(resource[:start] + encoded + resource[end:], encoding='utf-8')
(workspace / 'diagnostics/homeassistant-updated.js').write_text(ha, encoding='utf-8')
print('Updated control panel, shared forms, Wi-Fi and home sensor spacing')
