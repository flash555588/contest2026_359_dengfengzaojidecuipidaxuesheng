/* Shared device controls: production QuickJS/LVGL, offline service, drag and picker frames. */
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


  for(unsigned theme=0;theme<2;theme++)for(unsigned i=0;i<4;i++)
    {
      const char *types[]={"fan","curtain","humidifier","speaker"};
      const char *keys[]={"percentage","position","humidity","volume_level"};
      const struct all_device_case_s *sample=NULL;
      for(unsigned j=0;j<sizeof(all_device_cases)/sizeof(all_device_cases[0]);j++)if(!strcmp(all_device_cases[j].type,types[i]))sample=&all_device_cases[j];
      assert(sample);qpk_review_hass_fixture(true);panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();glass_homeassistant(NULL);
      char js[14000];snprintf(js,sizeof(js),
        "system.homeAssistantService.callService=system.homeAssistantService.control=()=>{throw Error('offline render must not control a device')};"
        "globalThis.__get=system.homeAssistantService.get;globalThis.__services=%s;system.homeAssistantService.get=r=>{if(r!=='services')return __get(r);const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:JSON.stringify(__services)});return id;};"
        "__haStates=%s;ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();globalThis.__row=ESPHomeHA.snapshot().layoutPages[0][0];",all_device_services,sample->states);
      assert(qpk_review_eval(js)==0);
      assert(qpk_review_click_at((int)qpk_review_number("__row.x+__row.w/2"),(int)qpk_review_number("__row.y+__row.h-24"))==0);
      snprintf(js,sizeof(js),"for(let i=0;i<30;i++)__reviewTick();globalThis.__rail=ESPHomeHA.snapshot().parameterControls.find(r=>r.key==='%s');globalThis.__view=ui.getSize();if(__rail.h!==52)throw Error('slider geometry mismatch')",keys[i]);
      assert(qpk_review_eval(js)==0);
      assert(qpk_review_eval("{const id=ui.panel(0,0,1,1);ui.setHidden(id,true);for(const style of [{gradientMainStop:-1},{gradientEndStop:256},{rightAlign:2}]){let rejected=false;try{ui.setStyle(id,style)}catch(e){rejected=true}if(!rejected)throw Error('style bounds missing')}ui.setStyle(id,{gradientMainStop:0,gradientEndStop:255,rightAlign:1})}")==0);
      for(unsigned frame=0;frame<3;frame++){
        snprintf(js,sizeof(js),"__reviewTouch('%s',(__rail.x+__rail.w*%u/100)/__view.width,(__rail.y+__rail.h/2)/__view.height)",frame?"move":"down",20+frame*30);
        assert(qpk_review_eval(js)==0);snprintf(path,sizeof(path),"%s-drag-%u",types[i],20+frame*30);review_capture(display,path,"统一滑条原生拖动");
      }
      assert(qpk_review_eval("__reviewTouch('cancel',0,0);for(let i=0;i<20;i++)__reviewTick()")==0);
      if(i==0){
        assert(qpk_review_eval("__haStates[0].attributes.preset_modes=Array.from({length:9},(_,i)=>'mode-'+i);__haStates[0].attributes.preset_mode='mode-0';ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick()")==0);
        assert(qpk_review_click_at(400,340)==0);assert(qpk_review_eval("for(let i=0;i<20;i++)__reviewTick()")==0);
        assert(review_label_find(g_desktop.screen,"mode-3"));review_capture(display,"fan-mode-picker","模式选择原生渲染");
        assert(qpk_review_click_at(288,428)==0);assert(qpk_review_eval("for(let i=0;i<20;i++)__reviewTick()")==0);
        assert(review_label_find(g_desktop.screen,"mode-6"));review_capture(display,"fan-mode-picker-page-2","模式分页原生渲染");
      }
      panel_hide(NULL);
    }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("PROFILE_DETAIL_NATIVE_OK: %u captures, four domains, drag frames and long option pages\n",review_count);return 0;
}
