/* SPDX-License-Identifier: Apache-2.0 */
#define main review_full_pages_main
#include "render_all_ui.c"
#undef main
extern double qpk_review_number(const char *source);
extern void qpk_review_root_area(lv_area_t *area);
static lv_point_t lamp_point;
static lv_indev_state_t lamp_pressed;
static void lamp_read(lv_indev_t *input,lv_indev_data_t *data)
{LV_UNUSED(input);data->point=lamp_point;data->state=lamp_pressed;}
static void lamp_tick(lv_display_t *display)
{for(unsigned i=0;i<12;i++){assert(qpk_review_eval("__reviewTick()")==0);lv_tick_inc(20);lv_timer_handler();}lv_refr_now(display);}
static void lamp_press(lv_indev_t *input,const char *caption,lv_display_t *display)
{
  lv_obj_update_layout(g_desktop.screen);lv_obj_t *label=review_label_find(g_desktop.current_card,caption);assert(label);
  lv_area_t area;lv_obj_get_coords(label,&area);lamp_point=(lv_point_t){(area.x1+area.x2)/2,(area.y1+area.y2)/2};
  lamp_pressed=LV_INDEV_STATE_PRESSED;lv_indev_read(input);lv_tick_inc(25);
  lamp_pressed=LV_INDEV_STATE_RELEASED;lv_indev_read(input);lamp_tick(display);
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
    panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();qpk_review_hass_fixture(true);
    lv_obj_set_pos(g_desktop.panel,0,32);lv_obj_set_height(g_desktop.panel,568);
    glass_homeassistant(NULL);
    assert(qpk_review_eval(
      "var __oldGet=system.homeAssistantService.get;var __bedCalls=0;"
      "system.homeAssistantService.callService=function(domain,service,body){__bedCalls++;var data=JSON.parse(body);if(data.effect)__haStates[0].attributes.effect=data.effect;var id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:'[]'});return id};"
      "system.homeAssistantService.status=()=>({configured:true,busy:false,url:'http://192.168.1.88:8123',canControl:true,canConfigure:true,deviceGroups:true});"
      "__haStates=[{entity_id:'light.bedside',state:'on',attributes:{friendly_name:'床头灯',brightness:200,supported_color_modes:['color_temp','rgb'],rgb_color:[255,150,46],color_mode:'rgb',color_temp_kelvin:2700,min_color_temp_kelvin:2700,max_color_temp_kelvin:6500,supported_features:4,effect_list:['none','Day','Night','Relax'],effect:'Day'}},{entity_id:'switch.ambient',state:'on',attributes:{friendly_name:'辅助灯'}}];"
      "system.homeAssistantService.get=function(resource){if(resource!=='devices')return __oldGet(resource);var id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:JSON.stringify([['bedside','床头灯','卧室',['light.bedside','switch.ambient']]])});return id;};"
      "ESPHomeHA.refresh();for(let i=0;i<100;i++)__reviewTick();ESPHomeHA.openDevice('http://192.168.1.88:8123','light.bedside');for(let i=0;i<60;i++)__reviewTick();")==0);
    lamp_tick(display);lv_area_t root;qpk_review_root_area(&root);assert(root.y1==96&&lv_area_get_height(&root)==504);
    assert(qpk_review_number("!!ESPHomeHA.snapshot().colorWheel"));
    for(unsigned i=0;i<GLASS_MOTION_SLOTS;i++)assert(!g_motion_jobs[i].object||g_motion_jobs[i].kind!=MOTION_BATCH);
    review_capture(display,"bedside-presets","床头灯 · 常用预设");
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,lamp_read);
    assert(qpk_review_number("__bedCalls")==0);review_capture(display,"bedside-color","床头灯 · 自选颜色");
    lamp_press(input,"夜间",display);assert(qpk_review_number("__bedCalls")==1);
    lamp_press(input,"其他设备功能",display);assert(review_label_find(g_desktop.current_card,"返回灯光配色"));
    review_capture(display,"bedside-aux","床头灯 · 其他功能");
    lv_indev_delete(input);panel_hide(NULL);
    puts("BEDSIDE_NATIVE_OK: real 504px content; actual LVGL pointer reaches presets/color/effect/auxiliary; no card upward motion; both themes");
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);return 0;
}
