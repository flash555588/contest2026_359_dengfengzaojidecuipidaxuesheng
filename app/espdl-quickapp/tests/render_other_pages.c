/* SPDX-License-Identifier: Apache-2.0 */
#define main review_all_pages_main
#include "render_all_ui.c"
#undef main

static lv_point_t sidebar_pointer_point;
static lv_indev_state_t sidebar_pointer_state;
static unsigned sidebar_actions[2];
static bool sidebar_delete_on_action;
static void sidebar_pointer_read(lv_indev_t *input,lv_indev_data_t *data)
{LV_UNUSED(input);data->point=sidebar_pointer_point;data->state=sidebar_pointer_state;}
static void sidebar_pointer_step(lv_indev_t *input,int x,int y,bool pressed)
{sidebar_pointer_point=(lv_point_t){x,y};sidebar_pointer_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_obj_update_layout(g_desktop.screen);lv_indev_read(input);lv_tick_inc(25);}
static void sidebar_action(lv_event_t *event)
{sidebar_actions[(uintptr_t)lv_event_get_user_data(event)]++;if(sidebar_delete_on_action)lv_obj_delete(lv_obj_get_parent(lv_event_get_target(event)));}
static void test_sidebar_drag(lv_display_t *display)
{
  memset(sidebar_actions,0,sizeof(sidebar_actions));
  lv_obj_t *root=glass_box(g_desktop.screen,0,0,164,568,detail_paper(),0);
  lv_obj_t *first=glass_button(root,"One",12,104,140,detail_paper(),sidebar_action,NULL);glass_sidebar_item(first,true);
  lv_obj_t *second=glass_button(root,"Two",12,156,140,detail_paper(),sidebar_action,(void *)1);glass_sidebar_item(second,false);
  lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,sidebar_pointer_read);
  sidebar_pointer_step(input,80,126,true);sidebar_pointer_step(input,80,152,true);
  assert(g_sidebar_drag.cursor&&lv_obj_get_style_y(g_sidebar_drag.cursor,0)==130);
  assert(sidebar_actions[0]==0&&sidebar_actions[1]==0);
  sidebar_pointer_step(input,80,178,true);sidebar_pointer_step(input,80,178,false);
  assert(!g_sidebar_drag.source&&!g_sidebar_drag.cursor&&sidebar_actions[0]==0&&sidebar_actions[1]==1);
  sidebar_pointer_step(input,80,126,true);sidebar_pointer_step(input,80,126,false);assert(sidebar_actions[0]==1&&sidebar_actions[1]==1);
  sidebar_pointer_step(input,80,126,true);sidebar_pointer_step(input,80,178,true);sidebar_pointer_step(input,250,178,false);
  assert(sidebar_actions[0]==1&&sidebar_actions[1]==1&&!g_sidebar_drag.cursor);
  sidebar_pointer_step(input,80,126,true);sidebar_pointer_step(input,80,178,true);lv_obj_delete(root);
  assert(!g_sidebar_drag.source&&!g_sidebar_drag.cursor);sidebar_pointer_step(input,80,178,false);lv_indev_delete(input);
  /* Navigation may synchronously destroy both the source and target rows. */
  root=glass_box(g_desktop.screen,0,0,164,568,detail_paper(),0);
  first=glass_button(root,"One",12,104,140,detail_paper(),sidebar_action,NULL);glass_sidebar_item(first,true);
  second=glass_button(root,"Two",12,156,140,detail_paper(),sidebar_action,(void *)1);glass_sidebar_item(second,false);
  input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,sidebar_pointer_read);
  sidebar_delete_on_action=true;
  sidebar_pointer_step(input,80,126,true);sidebar_pointer_step(input,80,178,true);sidebar_pointer_step(input,80,178,false);
  sidebar_delete_on_action=false;
  assert(!g_sidebar_drag.source&&!g_sidebar_drag.cursor&&sidebar_actions[1]==2);
  lv_indev_delete(input);
  puts("SIDEBAR_NATIVE_POINTER_OK: follow, nearest release once, tap, outside, deletion during drag/navigation");
}
static void assert_sidebar(lv_obj_t *parent)
{
  lv_obj_t *sidebar=NULL;
  for(unsigned i=0;i<lv_obj_get_child_count(parent);i++){
    lv_obj_t *child=lv_obj_get_child(parent,i);
    if(lv_obj_get_style_x(child,0)==0&&lv_obj_get_style_y(child,0)==0&&lv_obj_get_style_width(child,0)==GLASS_SIDEBAR_WIDTH){sidebar=child;break;}
  }
  assert(sidebar);
  assert(lv_color_to_u32(lv_obj_get_style_bg_color(sidebar,0))==lv_color_to_u32(lv_color_hex(detail_paper())));
  for(unsigned i=0;i<lv_obj_get_child_count(sidebar);i++){
    lv_obj_t *child=lv_obj_get_child(sidebar,i);
    if(!lv_obj_check_type(child,&lv_button_class))continue;
    assert(lv_obj_get_style_x(child,0)==GLASS_SIDEBAR_PAD);
    assert(lv_obj_get_style_width(child,0)==GLASS_SIDEBAR_ITEM_WIDTH);
    assert(lv_obj_get_style_height(child,0)==GLASS_SIDEBAR_ITEM_HEIGHT);
    assert(lv_obj_get_style_radius(child,0)==GLASS_SIDEBAR_RADIUS);
  }
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];
  FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
  void *font=malloc(size);assert(font);fread(font,1,size,f);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(font,size,16);
  g_desktop.font20=lv_tiny_ttf_create_data(font,size,20);
  g_desktop.font28=lv_tiny_ttf_create_data(font,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);
  g_glass.reduced_motion=true;g_glass.temperature_c=-999;g_glass.humidity_pct=-1;g_glass.pm25=-1;
  g_desktop.screen=lv_screen_active();lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0x000000,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);

  for(unsigned theme=0;theme<2;theme++){
    review_music_fixture(0);review_chat_fixture(0);review_weather_fixture(0);review_ble_fixture(0);
    review_qpk_fixture(NULL);strcpy(g_files_path,"/data");g_files_clipboard[0]=0;g_files_clipboard_move=false;
    panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();
    review_navigation_entries();test_sidebar_drag(display);review_capture(display,"home","首页");
    SHOT("settings","控制面板",glass_settings(NULL));
    assert_sidebar(g_control_card);
    lv_obj_t *value=review_label_find(g_control_card,"100%");assert(value);
    lv_slider_set_value(g_glass.brightness_slider,63,LV_ANIM_OFF);
    lv_obj_send_event(g_glass.brightness_slider,LV_EVENT_VALUE_CHANGED,NULL);
    assert(!strcmp(lv_label_get_text(value),"63%")&&g_screen_brightness==100);
    lv_obj_send_event(g_glass.brightness_slider,LV_EVENT_PRESS_LOST,NULL);
    assert(lv_slider_get_value(g_glass.brightness_slider)==100&&!strcmp(lv_label_get_text(value),"100%"));
    SHOT("apps","全部应用",glass_apps(NULL));
    lv_obj_t *tiles[9];for(unsigned i=0;i<9;i++)tiles[i]=g_apps_slots[i].tile;
    assert(lv_obj_get_child_count(g_apps_grid)==9);
    lv_obj_send_event(tiles[0],LV_EVENT_PRESSED,NULL);
    glass_apps_page(NULL,1);lv_obj_send_event(tiles[0],LV_EVENT_CLICKED,NULL);
    assert(g_desktop.current_card==g_apps_card);
    for(unsigned round=0;round<30;round++){
      glass_apps_page(NULL,round%2);
      assert(lv_obj_get_child_count(g_apps_grid)==9);
      for(unsigned i=0;i<9;i++)assert(g_apps_slots[i].tile==tiles[i]);
    }
    assert(strstr(lv_label_get_text(g_apps_count),"第 2 / 2 页"));
    review_capture(display,"apps-page2","应用第二页");
    struct qpk_entry_s app={0};strcpy(app.name,"示例快应用");strcpy(app.package,"review.fixture");strcpy(app.dir,"/offline-fixtures/qpk/example");
    g_desktop.qpk=&app;g_desktop.nqpk=1;glass_apps_render();
    lv_obj_t *remove=NULL;
    for(unsigned i=0;i<9;i++)if(!lv_obj_has_flag(g_apps_slots[i].tile,LV_OBJ_FLAG_HIDDEN)&&!lv_obj_has_flag(g_apps_slots[i].remove,LV_OBJ_FLAG_HIDDEN))remove=g_apps_slots[i].remove;
    assert(remove);lv_obj_send_event(remove,LV_EVENT_PRESSED,NULL);
    g_desktop.nqpk=0;g_desktop.qpk=NULL;glass_apps_render();
    lv_obj_send_event(remove,LV_EVENT_CLICKED,NULL);assert(!g_desktop.dialog);
    g_desktop.qpk=&app;g_desktop.nqpk=1;glass_apps_render();
    review_click("删除");assert(g_desktop.dialog&&!strcmp(g_apps_remove_target.package,"review.fixture"));
    review_capture(display,"apps-delete-confirm","删除应用确认");dialog_cancel();g_desktop.nqpk=0;g_desktop.qpk=NULL;
    SHOT("files","文件管理",glass_storage_page(NULL));
    glass_files_tick(NULL);review_capture(display,"files-loaded","文件列表");
    struct files_tile *file=&g_files_browser.tiles[0];assert(file->button);
    struct glass_file_entry replacement={0};strcpy(replacement.name,"replacement");replacement.directory=true;
    char path_before[sizeof(g_files_path)],selected_before[sizeof(g_files_selected)];
    strcpy(path_before,g_files_path);strcpy(selected_before,g_files_selected);
    lv_obj_send_event(file->button,LV_EVENT_PRESSED,NULL);files_tile_set(0,&replacement);
    lv_obj_send_event(file->button,LV_EVENT_SHORT_CLICKED,NULL);
    assert(!strcmp(path_before,g_files_path)&&!strcmp(selected_before,g_files_selected)&&!g_files_pending);
    lv_obj_send_event(file->button,LV_EVENT_PRESSED,NULL);files_tile_set(0,&replacement);
    lv_obj_send_event(file->button,LV_EVENT_LONG_PRESSED,NULL);assert(!g_files_pending);
    lv_obj_send_event(file->button,LV_EVENT_PRESSED,NULL);
    lv_obj_send_event(file->button,LV_EVENT_SHORT_CLICKED,NULL);
    assert(g_files_pending&&strstr(g_files_path,"/replacement"));glass_files_cancel_calls();g_files_pending=false;
    SHOT("wifi","Wi-Fi 网络",glass_wifi_page(NULL));
    SHOT("resources","系统性能",glass_resources_page(NULL));
    SHOT("album","相册",launch_local_video(NULL));
    SHOT("music","音乐",glass_music(NULL));assert_sidebar(g_music_ui.root);
    SHOT("chat-sidebar","AI 助手侧栏",glass_chat_open(NULL));assert_sidebar(g_chat_ui.root);
    panel_hide(NULL);assert(!g_apps_card&&!g_apps_slots[0].tile);
    puts("OTHER_PAGES_OK: 30 retained app flips; stale launch/remove blocked; brightness preview/cancel; refreshed file short/long press safety; light/dark");
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);return 0;
}
