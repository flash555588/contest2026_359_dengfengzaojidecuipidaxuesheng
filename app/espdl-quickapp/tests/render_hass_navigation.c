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
      qpk_review_hass_fixture(true);panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();glass_homeassistant(NULL);
      assert(qpk_review_eval(
        "system.homeAssistantService.callService=system.homeAssistantService.control=()=>{throw Error('preview must never send commands')};"
        "globalThis.__sceneStatus=system.homeAssistantService.status;system.homeAssistantService.status=()=>Object.assign({},__sceneStatus(),{sceneSync:true});"
        "globalThis.__sceneGet=system.homeAssistantService.get;system.homeAssistantService.get=r=>{"
        "if(r!=='scenes'&&r!=='services')return __sceneGet(r);const id=__haId++;"
        "const body=r==='scenes'?{items:__haStates.filter(s=>/^(scene|script)\\./.test(s.entity_id)),total:2}:"
        "[{domain:'scene',services:{turn_on:{}}},{domain:'script',services:{turn_on:{}}}];"
        "__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:JSON.stringify(body)});return id;};"
        "__haStates=[{entity_id:'scene.evening',state:'2026-10-15T12:00:00',attributes:{friendly_name:'晚间场景'}},"
        "{entity_id:'script.sleep',state:'off',attributes:{friendly_name:'睡眠脚本'}},"
        "{entity_id:'sensor.room',state:'24.5',attributes:{friendly_name:'室温',device_class:'temperature',unit_of_measurement:'C'}}];"
        "ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();")==0);
      assert(qpk_review_click_at(60,181)==0);
      assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(ESPHomeHA.snapshot().nav!=='scenes'||ESPHomeHA.snapshot().layoutPages[0].length!==2)throw Error('scene navigation failed')")==0);
      review_capture(display,"ha-scenes","场景");
      check_visible_icons(g_desktop.screen,0);
      assert(qpk_review_eval("globalThis.__row=ESPHomeHA.snapshot().layoutPages[0][0]")==0);
      assert(qpk_review_click_at((int)qpk_review_number("__row.x+__row.w/2"),(int)qpk_review_number("__row.y+__row.h-24"))==0);
      assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(!ESPHomeHA.snapshot().functionControls.some(s=>s.service==='turn_on'))throw Error('scene execution missing')")==0);
      review_capture(display,"ha-scene-detail","场景详情");check_visible_icons(g_desktop.screen,0);
      assert(qpk_review_click_at(60,227)==0);
      assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(ESPHomeHA.snapshot().nav!=='logs'||!ESPHomeHA.snapshot().activityLog.length)throw Error('log navigation failed')")==0);
      review_capture(display,"ha-logs","日志");
      check_visible_icons(g_desktop.screen,0);
      assert(qpk_review_click_at(60,135)==0);
      assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(!ESPHomeHA.snapshot().layoutPages.flat().some(row=>row.entity_id==='sensor.room'))throw Error('sensor lost')")==0);
      review_capture(display,"ha-devices","设备");
      check_visible_icons(g_desktop.screen,0);
      panel_hide(NULL);
    }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("HA_NAVIGATION_NATIVE_OK: %u captures, %u icon checks\n",review_count,icon_checks);return 0;
}
