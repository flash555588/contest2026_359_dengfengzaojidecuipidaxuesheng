#define main all_ui_reference_main
#include "render_all_ui.c"
#undef main
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

  for(unsigned theme=0;theme<2;theme++) {
    qpk_review_hass_fixture(true);panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();glass_homeassistant(NULL);
    assert(qpk_review_eval("__haStates=[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'书桌台灯',device_id:'device_lamp',device_name:'书桌台灯',brightness:160,supported_color_modes:['brightness']}},{entity_id:'sensor.lamp_battery',state:'98',attributes:{friendly_name:'灯电量',device_id:'device_lamp',device_class:'battery',unit_of_measurement:'%'}},{entity_id:'sensor.temperature',state:'24.5',attributes:{friendly_name:'温度',device_id:'device_environment',device_name:'温湿度计',device_class:'temperature',unit_of_measurement:'°C'}},{entity_id:'sensor.humidity',state:'48',attributes:{friendly_name:'湿度',device_id:'device_environment',device_name:'温湿度计',device_class:'humidity',unit_of_measurement:'%'}},{entity_id:'switch.power',state:'off',attributes:{friendly_name:'智能插座',device_id:'device_socket',device_name:'智能插座'}}];ESPHomeHA.refresh();for(let i=0;i<60;i++)__reviewTick();if(ESPHomeHA.snapshot().deviceCards.length!==3)throw Error('device grouping failed')")==0);
    assert(qpk_review_click_at(900,30)==0);assert(qpk_review_eval("for(let i=0;i<25;i++)__reviewTick()")==0);
    assert(qpk_review_click_at(245,210)==0);assert(qpk_review_eval("for(let i=0;i<50;i++)__reviewTick()")==0);
    assert(qpk_review_click_at(900,30)==0);assert(qpk_review_eval("for(let i=0;i<30;i++)__reviewTick();if(ESPHomeHA.snapshot().deviceCards.find(c=>c.entity_id==='light.desk').cardSize!=='large')throw Error('size selection failed')")==0);
    review_capture(display,"hass-grouped-mixed","设备分组后的大小卡片组合");
    assert(qpk_review_eval("var r=ESPHomeHA.snapshot().brightnessControls.find(c=>c.entity==='light.desk');if(!r)throw Error('missing slider');__reviewTouch('down',(r.x+r.w*.63)/1024,(r.y+r.h/2)/536);__reviewTouch('move',(r.x+r.w*.8)/1024,(r.y+r.h/2)/536);__reviewSwipe('left');for(let i=0;i<20;i++)__reviewTick();if(ESPHomeHA.snapshot().roomPage!==0)throw Error('slider turned page')")==0);
    lv_obj_t *preview=review_label_find(g_desktop.screen,"亮度 80%");assert(preview);
    review_capture(display,"hass-grouped-drag","大卡片进度条实时拖动");
    assert(qpk_review_eval("__reviewTouch('cancel',0,0);for(let i=0;i<20;i++)__reviewTick()")==0);
    assert(qpk_review_click_at(450,246)==0);assert(qpk_review_eval("for(let i=0;i<30;i++)__reviewTick();if(!ESPHomeHA.snapshot().detailAdvanced)throw Error('More failed')")==0);
    review_capture(display,"hass-grouped-more","设备全部功能 · 主控制");
    review_click("下一功能");assert(qpk_review_eval("for(let i=0;i<20;i++)__reviewTick();if(ESPHomeHA.snapshot().selectedId!=='sensor.lamp_battery')throw Error('function routing failed')")==0);
    review_capture(display,"hass-grouped-sensor","设备全部功能 · 只读传感器");
    panel_hide(NULL);
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);puts("GROUPED_NATIVE_UI_OK");return 0;
}
