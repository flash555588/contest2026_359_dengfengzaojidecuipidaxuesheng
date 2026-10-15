/* Scene and session-log navigation, with production QuickJS/LVGL and offline I/O. */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_unified_devices.c"

int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];
  FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
  void *font=malloc(size);assert(font);assert(fread(font,1,size,f)==size);fclose(f);
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


  for(unsigned theme=0;theme<2;theme++)
    {
      qpk_review_hass_fixture(true);panel_hide(NULL);g_desktop.light_theme=theme==0;
      glass_rebuild();glass_homeassistant(NULL);
      assert(qpk_review_eval("system.homeAssistantService.control=system.homeAssistantService.callService=()=>{throw Error('navigation must never control')};")==0);
      assert(qpk_runtime_open_ha_device("http://192.168.1.88:8123","light.desk")==0);
      assert(qpk_review_eval("for(let i=0;i<70;i++)__reviewTick();if(!ESPHomeHA.snapshot().detailOpen||ESPHomeHA.snapshot().selectedId!=='light.desk')throw Error('deferred navigation failed')")==0);
      review_capture(display,"home-device-detail-unpinned","桌面设备详情 · 固定入口");
      lv_obj_t *pin=review_label_find(g_desktop.screen,"固定到桌面");assert(pin);
      lv_obj_update_layout(g_desktop.screen);lv_area_t box,root;
      lv_obj_get_coords(pin,&box);qpk_review_root_area(&root);
      assert(box.x1>=root.x1&&box.x2<=root.x2&&box.y2<root.y1+60);
      assert(qpk_review_click_at((box.x1+box.x2)/2-root.x1,(box.y1+box.y2)/2-root.y1)==0);
      assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick();if(ESPHomeHA.snapshot().desktopPins.join(',')!=='light.desk')throw Error('pin failed')")==0);
      assert(review_label_find(g_desktop.screen,"取消桌面固定"));
      review_capture(display,"home-device-detail-pinned","桌面设备详情 · 已固定");
      panel_hide(NULL);
    }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("HOME_SHORTCUT_DETAIL_NATIVE_OK: %u captures, real native deferred navigation and pin callbacks, no device controls\n",review_count);
  return 0;
}
