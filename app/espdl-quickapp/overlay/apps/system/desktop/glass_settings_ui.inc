/* SPDX-License-Identifier: Apache-2.0 */
static void glass_settings(lv_event_t *e)
{
  LV_UNUSED(e);glass_preferences_update();g_control_preferences=true;
  lv_obj_t *card=panel_card("控制面板");if(!card)return;
  detail_page(card);g_control_card=card;
  lv_obj_add_event_cb(card,glass_control_deleted,LV_EVENT_DELETE,NULL);
  lv_obj_remove_event_cb(g_desktop.panel,glass_control_outside);
  detail_sidebar(card,"设置",GLASS_ICON_SLIDERS,glass_settings_back);
  lv_obj_t *selected=glass_box(card,12,104,140,44,detail_soft(),12);
  lv_obj_add_flag(selected,LV_OBJ_FLAG_CLICKABLE);glass_sidebar_track(selected);
  home_icon(selected,GLASS_ICON_GRID,12,12,20,detail_accent());
  detail_text(selected,"控制面板",42,12,94,14,detail_accent());
  lv_obj_t *row=detail_nav(card,"Wi-Fi 网络",LV_SYMBOL_WIFI,156,140,glass_system_page_open,NULL);glass_sidebar_item(row,false);
  row=detail_nav(card,"存储与文件",LV_SYMBOL_DIRECTORY,208,140,glass_system_page_open,(void *)1);glass_sidebar_item(row,false);
  row=detail_nav(card,"系统性能",GLASS_ICON_SLIDERS,260,140,glass_system_page_open,(void *)3);glass_sidebar_item(row,false);
  detail_text(card,"控制面板",216,26,300,28,detail_ink());
  detail_text(card,"显示、连接与个性化",216,66,400,14,detail_muted());
  g_preferences_status=detail_text(card,glass_preferences_status_text(),626,36,374,14,detail_muted());
  g_preferences_shown=glass_preferences_display_state();
  lv_obj_add_event_cb(g_preferences_status,glass_preferences_status_deleted,LV_EVENT_DELETE,NULL);

  lv_obj_t *display=detail_tile(card,216,104,444,180);
  home_icon(display,GLASS_ICON_SUN,24,22,24,detail_accent());
  detail_text(display,"屏幕显示",60,22,300,20,detail_ink());
  lv_obj_t *value=detail_text(display,"100%",24,74,130,32,detail_ink());
  lv_obj_t *brightness=lv_slider_create(display);g_glass.brightness_slider=brightness;
  lv_obj_set_pos(brightness,166,100);lv_obj_set_size(brightness,242,6);
  lv_obj_set_ext_click_area(brightness,18);
  lv_obj_set_style_radius(brightness,LV_RADIUS_CIRCLE,LV_PART_MAIN);
  lv_obj_set_style_radius(brightness,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightness,lv_color_hex(detail_soft()),LV_PART_MAIN);
  lv_obj_set_style_bg_color(brightness,lv_color_hex(detail_accent()),LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brightness,lv_color_hex(detail_paper()),LV_PART_KNOB);
  lv_obj_set_style_pad_all(brightness,7,LV_PART_KNOB);
  lv_obj_set_style_radius(brightness,LV_RADIUS_CIRCLE,LV_PART_KNOB);
  lv_obj_set_style_border_width(brightness,2,LV_PART_KNOB);
  lv_obj_set_style_border_color(brightness,lv_color_hex(detail_accent()),LV_PART_KNOB);
  lv_obj_set_style_shadow_width(brightness,0,LV_PART_KNOB);
  lv_slider_set_range(brightness,10,100);
  lv_slider_set_value(brightness,g_screen_brightness,LV_ANIM_OFF);
  lv_obj_add_event_cb(brightness,glass_brightness_preview,LV_EVENT_VALUE_CHANGED,value);
  lv_obj_add_event_cb(brightness,glass_brightness_preview,LV_EVENT_PRESS_LOST,value);
#ifdef GLASS_PWM_BACKLIGHT
  lv_label_set_text_fmt(value,"%d%%",g_screen_brightness);
  lv_obj_add_event_cb(brightness,glass_brightness_changed,LV_EVENT_RELEASED,value);
  detail_text(display,"拖动滑块调整屏幕亮度",24,140,396,12,detail_muted());
#else
  LV_UNUSED(value);
  lv_obj_add_state(brightness,LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(brightness,lv_color_hex(g_desktop.light_theme?0xb8bac8:0x626878),LV_PART_INDICATOR|LV_STATE_DISABLED);
  detail_text(display,"当前屏幕使用固定亮度",24,140,396,12,detail_muted());
#endif

  lv_obj_t *connect=detail_tile(card,676,104,324,180);
  home_icon(connect,LV_SYMBOL_WIFI,24,22,24,detail_accent());
  detail_text(connect,"网络与连接",60,22,240,20,detail_ink());
  detail_nav(connect,"Wi-Fi 网络",LV_SYMBOL_WIFI,62,284,glass_system_page_open,NULL);
  detail_nav(connect,"蓝牙设备",LV_SYMBOL_BLUETOOTH,114,284,glass_system_page_open,(void *)2);

  lv_obj_t *appearance=detail_tile(card,216,300,444,266);
  home_icon(appearance,GLASS_ICON_SPARKLES,24,22,24,detail_accent());
  detail_text(appearance,"个性化",60,22,280,20,detail_ink());
  detail_text(appearance,"浅色外观",24,70,260,16,detail_ink());
  lv_obj_t *sw=lv_switch_create(appearance);g_glass.settings_switch=sw;
  glass_control_switch_style(sw);
  lv_obj_set_pos(sw,366,66);lv_obj_set_size(sw,52,28);
  lv_obj_set_ext_click_area(sw,8);
  lv_obj_set_style_bg_opa(sw,LV_OPA_COVER,LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw,lv_color_hex(detail_accent()),LV_PART_INDICATOR|LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(sw,LV_OPA_COVER,LV_PART_INDICATOR|LV_STATE_CHECKED);
  if(g_desktop.light_theme)lv_obj_add_state(sw,LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw,theme_changed,LV_EVENT_VALUE_CHANGED,NULL);
  detail_text(appearance,"减少动效",24,114,260,16,detail_ink());
  sw=lv_switch_create(appearance);g_glass.motion_switch=sw;
  glass_control_switch_style(sw);
  lv_obj_set_pos(sw,366,110);lv_obj_set_size(sw,52,28);
  lv_obj_set_ext_click_area(sw,8);
  lv_obj_set_style_bg_opa(sw,LV_OPA_COVER,LV_PART_MAIN);
  lv_obj_set_style_bg_color(sw,lv_color_hex(detail_accent()),LV_PART_INDICATOR|LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(sw,LV_OPA_COVER,LV_PART_INDICATOR|LV_STATE_CHECKED);
  if(g_glass.reduced_motion)lv_obj_add_state(sw,LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw,glass_motion_changed,LV_EVENT_VALUE_CHANGED,NULL);
  glass_box(appearance,24,151,396,1,detail_line(),0);
  detail_text(appearance,"主视觉配色",24,174,120,14,detail_muted());
  const uint32_t colors[]={0x77658f,0xb77860,0x498d82};
  for(unsigned i=0;i<3;i++){
    lv_obj_t *choice=glass_button(appearance,"",150+i*62,164,52,colors[i],glass_palette_select,(void *)(uintptr_t)i);
    lv_obj_set_size(choice,52,36);lv_obj_set_style_radius(choice,10,0);
    lv_obj_set_style_border_width(choice,g_glass.palette==i?2:0,0);
    lv_obj_set_style_border_color(choice,lv_color_hex(g_desktop.light_theme?0xffffff:0xe0d7ee),0);
    lv_obj_set_style_border_opa(choice,LV_OPA_COVER,0);
    if(g_glass.palette==i){lv_obj_t *check=home_icon(choice,LV_SYMBOL_OK,0,0,20,0xffffff);lv_obj_center(check);}
  }
  detail_text(appearance,"桌面小组件",24,228,120,14,detail_muted());
  lv_obj_t *dropdown=lv_dropdown_create(appearance);g_glass.widget_dropdown=dropdown;
  lv_dropdown_set_options(dropdown,"空气质量\n环境湿度\n系统运行");
  lv_dropdown_set_selected(dropdown,g_glass.widget);
  lv_obj_set_pos(dropdown,150,216);lv_obj_set_size(dropdown,268,36);
  glass_input_style(dropdown);
  lv_obj_set_style_text_font(dropdown,home_font(14),LV_PART_MAIN);
  lv_obj_set_style_text_color(dropdown,lv_color_hex(detail_ink()),LV_PART_MAIN);
  lv_obj_set_style_bg_color(dropdown,lv_color_hex(detail_soft()),0);
  lv_obj_set_style_border_color(dropdown,lv_color_hex(detail_line()),0);
  lv_obj_set_style_border_opa(dropdown,LV_OPA_COVER,0);
  lv_obj_set_style_text_font(dropdown,glass_icon_font(16),LV_PART_INDICATOR);
  lv_obj_t *list=lv_dropdown_get_list(dropdown);
  lv_obj_set_style_text_font(list,home_font(16),0);
  lv_obj_set_style_text_color(list,lv_color_hex(detail_ink()),0);
  lv_obj_set_style_bg_color(list,lv_color_hex(detail_paper()),0);
  lv_obj_set_style_radius(list,12,0);
  lv_obj_add_event_cb(dropdown,glass_widget_select,LV_EVENT_VALUE_CHANGED,NULL);

  lv_obj_t *system=detail_tile(card,676,300,324,266);
  home_icon(system,LV_SYMBOL_SETTINGS,24,22,24,detail_accent());
  detail_text(system,"系统管理",60,22,240,20,detail_ink());
  detail_nav(system,"存储与文件",LV_SYMBOL_DIRECTORY,62,284,glass_system_page_open,(void *)1);
  detail_nav(system,"系统性能",GLASS_ICON_SLIDERS,114,284,glass_system_page_open,(void *)3);
  detail_nav(system,"扫码配置",GLASS_ICON_QR,166,284,glass_portal_open,NULL);
  detail_text(system,"扫码设置 · 文件传输 · 应用安装",24,234,280,12,detail_muted());
}
