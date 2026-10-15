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

  FILE *small_regions=NULL;snprintf(path,sizeof(path),"%s/small-card-regions.json",review_out);small_regions=fopen(path,"w");assert(small_regions);fputs("[\n",small_regions);unsigned small_count=0;

  for(unsigned theme=0;theme<2;theme++)
    {
      qpk_review_hass_fixture(true);panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();glass_homeassistant(NULL);
      assert(qpk_review_eval(
        "system.homeAssistantService.callService=system.homeAssistantService.control=()=>{throw Error('offline render must not control a device')};"
        "globalThis.__get=system.homeAssistantService.get;system.homeAssistantService.get=r=>{if(r!=='services')return __get(r);const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:JSON.stringify([{domain:'light',services:{turn_on:{},turn_off:{}}}])});return id;};"
        "__haStates=[{entity_id:'light.review',state:'on',attributes:{friendly_name:'床头灯',brightness:200,supported_features:4,supported_color_modes:['color_temp','rgb'],color_mode:'color_temp',color_temp_kelvin:2700,min_color_temp_kelvin:2700,max_color_temp_kelvin:6500,rgb_color:[255,150,46],effect_list:['none','Day','Night','Relax'],effect:'Relax'}}];"
        "ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();globalThis.__row=ESPHomeHA.snapshot().layoutPages[0][0];")==0);
      /* Small cards use the same production gradient painter as detail sliders. */
      const unsigned small_levels[]={0,1,50,100};
      for(unsigned level=0;level<4;level++){
        char js[800];snprintf(js,sizeof(js),"__haStates[0].state='%s';__haStates[0].attributes.brightness=%u;ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();globalThis.__small=ESPHomeHA.snapshot().brightnessControls.find(r=>r.entity==='light.review');if(!__small||__small.value!==%u)throw Error('small brightness mismatch');globalThis.__view=ui.getSize()",small_levels[level]?"on":"off",small_levels[level]?(unsigned)((small_levels[level]*255+50)/100):200,small_levels[level]);
        assert(qpk_review_eval(js)==0);
        for(unsigned drag=0;drag<2;drag++){
          if(drag){snprintf(js,sizeof(js),"__reviewTouch('down',(__small.x+__small.w*%u/100)/__view.width,(__small.y+__small.h/2)/__view.height)",small_levels[level]);assert(qpk_review_eval(js)==0);}
          snprintf(path,sizeof(path),"lamp-small-%s-%u",drag?"drag":"set",small_levels[level]);review_capture(display,path,"小卡片亮度端点");
          lv_area_t root;qpk_review_root_area(&root);
          fprintf(small_regions,"%s{\"file\":\"%s-%s.png\",\"percent\":%u,\"drag\":%s,\"x\":%.0f,\"y\":%.0f,\"w\":%.0f,\"h\":%.0f}",small_count++?",\n":"",theme?"dark":"light",path,small_levels[level],drag?"true":"false",root.x1+qpk_review_number("__small.x-15"),root.y1+qpk_review_number("__small.y"),qpk_review_number("__small.w+30"),qpk_review_number("__small.h"));
          if(drag)assert(qpk_review_eval("__reviewTouch('cancel',0,0);for(let i=0;i<20;i++)__reviewTick()")==0);
        }
      }
      assert(qpk_review_eval("__haStates[0].state='on';__haStates[0].attributes.brightness=200;ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();__row=ESPHomeHA.snapshot().layoutPages[0][0]")==0);
      assert(qpk_review_eval("__reviewTouch('down',(__row.x+__row.w/2)/__view.width,(__row.y+__row.h-24)/__view.height);__reviewTouch('up',(__row.x+__row.w/2)/__view.width,(__row.y+__row.h-24)/__view.height)")==0);
      assert(qpk_review_click_at((int)qpk_review_number("__row.x+__row.w/2"),(int)qpk_review_number("__row.y+__row.h-24"))==0);
      assert(qpk_review_eval("for(let i=0;i<30;i++)__reviewTick();globalThis.__rail=ESPHomeHA.snapshot().parameterControls.find(r=>r.key==='brightness_pct');globalThis.__view=ui.getSize()")==0);
      assert(review_label_find(g_desktop.screen,"常用色温 · 颜色"));assert(review_label_find(g_desktop.screen,"颜色选择"));
      review_capture(display,"lamp-detail","灯具统一详情");check_visible_icons(g_desktop.screen,0xf0eb);
      for(unsigned frame=0;frame<3;frame++){
        char js[512];snprintf(js,sizeof(js),"__reviewTouch('%s',(__rail.x+__rail.w*%u/100)/__view.width,(__rail.y+__rail.h/2)/__view.height)",frame?"move":"down",20+frame*30);
        assert(qpk_review_eval(js)==0);snprintf(path,sizeof(path),"lamp-drag-%u",20+frame*30);review_capture(display,path,"原生拖动");
      }
      assert(qpk_review_eval("__reviewTouch('cancel',0,0);for(let i=0;i<15;i++)__reviewTick()")==0);
      assert(qpk_review_eval("globalThis.__wheel=ESPHomeHA.snapshot().colorWheel;if(!__wheel)throw Error('disc missing');__reviewTouch('down',(__wheel.x+__wheel.w/2)/__view.width,(__wheel.y+2)/__view.height);__reviewTouch('move',(__wheel.x+__wheel.w-3)/__view.width,(__wheel.y+__wheel.h/2)/__view.height)")==0);
      review_capture(display,"lamp-color-disc","色盘拖动");check_visible_icons(g_desktop.screen,0xf0eb);
      assert(qpk_review_eval("__reviewTouch('cancel',0,0)")==0);
      assert(qpk_review_eval(
        "globalThis.__effectCalls=[];system.homeAssistantService.callService=(entity,service,data)=>{const body=JSON.parse(data);if(entity!=='light.review'||service!=='turn_on'||Object.keys(body).sort().join(',')!=='effect,entity_id'||!__haStates[0].attributes.effect_list.includes(body.effect))throw Error('invalid offline effect');__effectCalls.push(body.effect);__haStates[0].attributes.effect=body.effect;const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:'[]'});return id;};"
        "{const id=ui.colorWheel(0,0,96);ui.setHidden(id,true);ui.setSize(id,128,128);for(const size of [[63,63],[96,97],[257,257]]){let bad=false;try{ui.setSize(id,...size)}catch(e){bad=true}if(!bad)throw Error('wheel size not bounded')}ui.setSize(id,96,96)}")==0);
      const char *effects[]={"夜间","日间","放松","无"};
      for(unsigned effect=0;effect<4;effect++){
        lv_obj_t *label=review_label_find(g_desktop.screen,effects[effect]);assert(label);
        lv_obj_update_layout(g_desktop.screen);lv_area_t a,root;lv_obj_get_coords(label,&a);qpk_review_root_area(&root);
        assert(a.y2<root.y2&&a.x1>=root.x1&&a.x2<=root.x2);
        assert(qpk_review_click_at((a.x1+a.x2)/2-root.x1,(a.y1+a.y2)/2-root.y1)==0);
        assert(qpk_review_eval("for(let i=0;i<50;i++)__reviewTick();if(ESPHomeHA.snapshot().optionPicker||ESPHomeHA.snapshot().phase!=='ready')throw Error('effect not switched directly')")==0);
        assert(qpk_review_number("__effectCalls.length")==effect+1);
        if(effect==0||effect==3){snprintf(path,sizeof(path),"lamp-effect-%s",effect?"none":"night");review_capture(display,path,"灯光效果单击切换");}
      }
      assert(qpk_review_eval("if(__effectCalls.join(',')!=='Night,Day,Relax,none')throw Error('effect values changed');__haStates[0].attributes.effect_list=Array.from({length:9},(_,i)=>'effect-'+i);__haStates[0].attributes.effect='effect-0';ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick()")==0);
      lv_obj_t *effect_title=review_label_find(g_desktop.screen,"灯光效果");assert(effect_title);
      lv_obj_update_layout(g_desktop.screen);lv_area_t a,root;lv_obj_get_coords(effect_title,&a);qpk_review_root_area(&root);
      assert(qpk_review_click_at((a.x1+a.x2)/2-root.x1,(a.y1+a.y2)/2-root.y1)==0);
      assert(qpk_review_eval("for(let i=0;i<20;i++)__reviewTick();if(!ESPHomeHA.snapshot().optionPicker)throw Error('long effect picker absent')")==0);
      review_capture(display,"lamp-effect-picker","长效果列表");
      assert(qpk_review_eval("__haStates[0].attributes.effect_list=['none','Day','Night','Relax'];__haStates[0].attributes.effect='Night';ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick()")==0);
      /* Close the still-valid picker without sending a command. */
      lv_obj_t *back=review_label_find(g_desktop.screen,"返回");assert(back);lv_obj_update_layout(g_desktop.screen);lv_obj_get_coords(back,&a);qpk_review_root_area(&root);
      assert(qpk_review_click_at((a.x1+a.x2)/2-root.x1,(a.y1+a.y2)/2-root.y1)==0);
      assert(qpk_review_eval("for(let i=0;i<20;i++)__reviewTick();if(__effectCalls.length!==4)throw Error('return applied an effect')")==0);
      assert(review_label_find(g_desktop.screen,"夜间"));review_capture(display,"lamp-effect-direct-restored","直接效果按钮恢复");
      /* Exercise production touch handlers against an in-memory HA fixture only. */
      assert(qpk_review_eval(
        "globalThis.__brightnessCalls=[];system.homeAssistantService.callService=(entity,service,data)=>{const body=JSON.parse(data);if(entity!=='light.review'||service!==(body.brightness_pct===undefined?'turn_off':'turn_on'))throw Error('brightness service mismatch');if(body.brightness_pct!==undefined&&(!Number.isInteger(body.brightness_pct)||body.brightness_pct<1||body.brightness_pct>100))throw Error('invalid brightness');__brightnessCalls.push({service,body});__haStates[0].state=service==='turn_on'?'on':'off';if(body.brightness_pct!==undefined)__haStates[0].attributes.brightness=Math.round(body.brightness_pct*255/100);const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:'[]'});return id;};")==0);
      const unsigned levels[]={0,1,50,100};
      for(unsigned level=0;level<4;level++){
        char js[640];snprintf(js,sizeof(js),"globalThis.__rail=ESPHomeHA.snapshot().parameterControls.find(r=>r.key==='brightness_pct');if(__rail.min!==0||__rail.max!==100)throw Error('range mismatch');__reviewTouch('down',(__rail.x+__rail.w*%u/100)/__view.width,(__rail.y+__rail.h/2)/__view.height)",levels[level]);
        assert(qpk_review_eval(js)==0);snprintf(path,sizeof(path),"lamp-brightness-drag-%u",levels[level]);review_capture(display,path,"亮度端点拖动");
        snprintf(js,sizeof(js),"__reviewTouch('up',(__rail.x+__rail.w*%u/100)/__view.width,(__rail.y+__rail.h/2)/__view.height);for(let i=0;i<70;i++)__reviewTick();if(ESPHomeHA.snapshot().phase!=='ready')throw Error('brightness not verified')",levels[level]);
        assert(qpk_review_eval(js)==0);assert(qpk_review_number("__brightnessCalls.length")==level+1);
        snprintf(js,sizeof(js),"{const c=__brightnessCalls.at(-1);if(c.service!=='%s'||%s)throw Error('wrong brightness payload')}",levels[level]?"turn_on":"turn_off",levels[level]?"c.body.brightness_pct!==ESPHomeHA.snapshot().entities[0].brightness":"Object.keys(c.body).join(',')!=='entity_id'");assert(qpk_review_eval(js)==0);
        snprintf(path,sizeof(path),"lamp-brightness-set-%u",levels[level]);review_capture(display,path,"亮度端点确认");
      }
      panel_hide(NULL);
    }
  fputs("\n]\n",small_regions);fclose(small_regions);
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("LAMP_DETAIL_NATIVE_OK: %u captures, direct effect taps, long list, resized disc, 0/1/50/100 brightness commands and drag frames\n",review_count);return 0;
}
