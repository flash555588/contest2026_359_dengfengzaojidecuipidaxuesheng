/* SPDX-License-Identifier: Apache-2.0 */
#define main unused_all_pages_main
#include "render_all_ui.c"
#undef main
#include <time.h>
extern double qpk_review_number(const char *source);
static lv_indev_state_t motion_pointer_state;
static lv_point_t motion_pointer_point;
static void motion_pointer_read(lv_indev_t *input,lv_indev_data_t *data)
{LV_UNUSED(input);data->state=motion_pointer_state;data->point=motion_pointer_point;}
static void motion_pointer(lv_indev_t *input,int x,int y,bool pressed)
{motion_pointer_point=(lv_point_t){x,y};motion_pointer_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_obj_update_layout(lv_layer_top());lv_indev_read(input);}
static void fluid_frames(lv_display_t *display,const char *name,unsigned count)
{
  char path[1024];
  for(unsigned i=0;i<count;i++){
    for(unsigned ms=0;ms<GLASS_MOTION_TICK;ms++){lv_tick_inc(1);lv_timer_handler();}
    if(qpk_runtime_running())assert(qpk_review_eval("for(let i=0;i<4;i++)__reviewTick()")==0);
    lv_refr_now(display);
    snprintf(path,sizeof(path),"%s/%s-%s-%03u.rgb565",review_out,g_desktop.light_theme?"light":"dark",name,i);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(review_fb,1,sizeof(review_fb),f)==sizeof(review_fb));fclose(f);
  }
}
static void fluid_page(unsigned page)
{
  char js[256];snprintf(js,sizeof(js),"globalThis.__fluidDot=ESPHomeHA.snapshot().pageIndicator.slots.find(s=>s.page===%u)",page);
  assert(qpk_review_eval(js)==0);
  assert(qpk_review_click_at((int)qpk_review_number("__fluidDot.x+12"),(int)qpk_review_number("__fluidDot.y+20"))==0);
  assert(qpk_review_eval("for(let i=0;i<12;i++)__reviewTick()")==0);
}
static void motion_frames(lv_display_t *display,const char *name,unsigned count)
{
  char path[1024];
  for(unsigned i=0;i<count;i++){
    lv_tick_inc(20);lv_timer_handler();lv_refr_now(display);
    snprintf(path,sizeof(path),"%s/%s-%s-%03u.rgb565",review_out,
             g_desktop.light_theme?"light":"dark",name,i);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(review_fb,1,sizeof(review_fb),f)==sizeof(review_fb));fclose(f);
  }
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];FILE *f=fopen(argv[1],"rb");assert(f);
  fseek(f,0,SEEK_END);size_t bytes=ftell(f);rewind(f);void *data=malloc(bytes);
  assert(data&&fread(data,1,bytes,f)==bytes);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,review_flush);
  glass_ui_metrics_attach(display);
  g_desktop.font16=lv_tiny_ttf_create_data(data,bytes,16);
  g_desktop.font20=lv_tiny_ttf_create_data(data,bytes,20);
  g_desktop.font28=lv_tiny_ttf_create_data(data,bytes,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);
  g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(data,bytes,48);glass_home_fonts_init(data,bytes);
  g_desktop.screen=lv_screen_active();lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0,0);
  lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);
  review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  extern lv_obj_t *qpk_review_widget(int);
  extern void qpk_review_root_area(lv_area_t *);
  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme==0;g_glass.reduced_motion=false;glass_rebuild();
    qpk_review_hass_fixture(true);glass_homeassistant(NULL);
    assert(qpk_review_eval(
      "system.homeAssistantService.callService=system.homeAssistantService.control=()=>{throw Error('scroll cannot send device commands')};"
      "__haStates=Array.from({length:512},(_,i)=>i%4===0?{entity_id:'light.fall_'+i,state:'on',attributes:{friendly_name:'客厅灯 '+i,brightness:128,supported_color_modes:['brightness']}}:i%4===1?{entity_id:'sensor.fall_'+i,state:String(20+i/100),attributes:{friendly_name:'温度 '+i,device_class:'temperature',unit_of_measurement:'C'}}:i%4===2?{entity_id:'switch.fall_'+i,state:'off',attributes:{friendly_name:'插座 '+i}}:{entity_id:'camera.fall_'+i,state:'idle',attributes:{friendly_name:'摄像机 '+i,device_type:'camera'}});"
      "ESPHomeHA.refresh();for(let i=0;i<150;i++)__reviewTick();"
      "var __fall=ESPHomeHA.snapshot().waterfall;if(!__fall||__fall.columns!==3||__fall.contentHeight<10000)throw Error('waterfall missing');"
      "if(ESPHomeHA.snapshot().pageIndicator.visible)throw Error('paged list retained')")==0);
    fluid_frames(display,"waterfall-top",18);review_capture(display,"waterfall-top","米家瀑布流 · 顶部");
    lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,motion_pointer_read);
    lv_area_t root_area;qpk_review_root_area(&root_area);
    motion_pointer(pointer,310,root_area.y1+226,true);
    for(unsigned step=1;step<=8;step++){lv_tick_inc(20);motion_pointer(pointer,310,root_area.y1+226-step*12,true);assert(qpk_review_eval("__reviewTick()")==0);}
    motion_pointer(pointer,310,root_area.y1+130,false);
    fluid_frames(display,"waterfall-drag",18);assert(qpk_review_eval("for(let i=0;i<60;i++)__reviewTick()")==0);
    assert(qpk_review_number("ESPHomeHA.snapshot().waterfall.scrollY")>10);
    assert(qpk_review_eval("ui.scrollTo(__fall.viewport,0,false);for(let i=0;i<30;i++)__reviewTick()")==0);
    fluid_frames(display,"waterfall-slider-scroll-start",8);
    motion_pointer(pointer,310,root_area.y1+180,true);
    assert(qpk_review_number("!!ESPHomeHA.snapshot().brightnessInput")>0);
    for(unsigned step=1;step<=8;step++){
      lv_tick_inc(20);motion_pointer(pointer,310,root_area.y1+180-step*10,true);
      assert(qpk_review_eval("__reviewTick()")==0);
    }
    motion_pointer(pointer,310,root_area.y1+100,false);
    fluid_frames(display,"waterfall-slider-scroll",18);
    assert(qpk_review_number("ESPHomeHA.snapshot().waterfall.scrollY")>10);
    assert(qpk_review_number("ESPHomeHA.snapshot().brightnessInput===null")>0);
    int handle=(int)qpk_review_number("ESPHomeHA.snapshot().waterfall.viewport");
    lv_obj_t *viewport=qpk_review_widget(handle);assert(viewport&&lv_obj_get_width(viewport)==860);
    assert(qpk_review_eval("ui.scrollTo(__fall.viewport,ui.scrollInfo(__fall.viewport).max/2,false);for(let i=0;i<60;i++)__reviewTick()")==0);
    fluid_frames(display,"waterfall-middle",18);review_capture(display,"waterfall-middle","米家瀑布流 · 中部");
    assert(qpk_review_number("ESPHomeHA.snapshot().waterfall.scrollY")>1000);
    assert(qpk_review_eval("ui.scrollTo(__fall.viewport,ui.scrollInfo(__fall.viewport).max,false);for(let i=0;i<60;i++)__reviewTick()")==0);
    fluid_frames(display,"waterfall-bottom",18);review_capture(display,"waterfall-bottom","米家瀑布流 · 底部");
    assert(qpk_review_number("Math.abs(ESPHomeHA.snapshot().waterfall.scrollY-ui.scrollInfo(__fall.viewport).max)")<=2);
    struct timespec perf_start,perf_end;clock_gettime(CLOCK_MONOTONIC,&perf_start);
    for(unsigned pass=0;pass<2;pass++){
      for(unsigned position=0;position<12;position++){
        char js[256];snprintf(js,sizeof(js),"ui.scrollTo(__fall.viewport,ui.scrollInfo(__fall.viewport).max*%u/11,false);for(let i=0;i<50;i++)__reviewTick()",position);
        assert(qpk_review_eval(js)==0);lv_tick_inc(40);lv_timer_handler();assert(qpk_review_eval("for(let i=0;i<50;i++)__reviewTick()")==0);
      }
      if(pass==0)assert(qpk_review_eval("var __fallWidgetCount=0")==0);
      assert(qpk_runtime_widget_count()<500);
    }
    clock_gettime(CLOCK_MONOTONIC,&perf_end);
    double elapsed=(perf_end.tv_sec-perf_start.tv_sec)*1000.0+(perf_end.tv_nsec-perf_start.tv_nsec)/1000000.0;
    printf("WATERFALL_HOST_PERF theme=%u windows=24 ms=%.3f widgets=%u motion_slot_bytes=%zu\n",theme,elapsed,qpk_runtime_widget_count(),sizeof(g_motion_jobs));
    assert(qpk_review_eval("ui.scrollTo(__fall.viewport,430,false);for(let i=0;i<60;i++)__reviewTick()")==0);
    fluid_frames(display,"waterfall-return",18);
    assert(qpk_review_eval("globalThis.__fallSavedScroll=ESPHomeHA.snapshot().waterfall.scrollY;globalThis.__fallRow=ESPHomeHA.snapshot().waterfall.visibleCards.find(r=>r.y>=64&&r.y+r.h<=64+ESPHomeHA.snapshot().waterfall.h)")==0);
    int x=(int)qpk_review_number("__fallRow.x+__fallRow.w/2"),y=(int)qpk_review_number("__fallRow.y+__fallRow.h-20");
    motion_pointer(pointer,x+root_area.x1,y+root_area.y1,true);
    motion_pointer(pointer,x+root_area.x1,y+root_area.y1,false);
    lv_indev_delete(pointer);
    assert(qpk_review_eval("for(let i=0;i<60;i++)__reviewTick();if(!ESPHomeHA.snapshot().detailOpen)throw Error('wrong card target')")==0);
    fluid_frames(display,"waterfall-detail",18);review_capture(display,"waterfall-detail","瀑布流 · 设备详情");
    assert(lv_obj_has_flag(viewport,LV_OBJ_FLAG_HIDDEN));
    assert(qpk_review_click_at(192,30)==0);assert(qpk_review_eval("for(let i=0;i<60;i++)__reviewTick()")==0);
    fluid_frames(display,"waterfall-back",18);assert(!lv_obj_has_flag(viewport,LV_OBJ_FLAG_HIDDEN));
    assert(qpk_review_number("Math.abs(ESPHomeHA.snapshot().waterfall.scrollY-__fallSavedScroll)")<=2);
    /* New bindings reject conversion-time deletion before touching LVGL. */
    assert(qpk_review_eval(
      "var __v=ui.viewport(20,20,100,100,()=>{}),__p=ui.panel(20,20,20,20,0);var __caught=false;"
      "try{ui.attach(__p,{valueOf(){ui.remove(__p);return __v}})}catch(e){__caught=true}if(!__caught)throw Error('stale child accepted');"
      "__caught=false;try{ui.scrollExtent(__v,{valueOf(){ui.remove(__v);return 1000}})}catch(e){__caught=true}if(!__caught)throw Error('stale view accepted')")==0);
    assert(qpk_review_eval(
      "globalThis.__viewportRemoved=false;globalThis.__ownedView=ui.viewport(20,20,100,100,y=>{if(y>0){ui.remove(__ownedView);__viewportRemoved=true}});ui.scrollExtent(__ownedView,500);ui.scrollTo(__ownedView,100,false)")==0);
    lv_tick_inc(40);lv_timer_handler();assert(qpk_review_eval("if(!__viewportRemoved)throw Error('scroll callback did not run')")==0);
    assert(qpk_review_eval(
      "globalThis.__cancelledCalls=0;var __cancelView=ui.viewport(20,20,100,100,()=>__cancelledCalls++);ui.scrollExtent(__cancelView,500);ui.scrollTo(__cancelView,100,false);ui.remove(__cancelView)")==0);
    lv_tick_inc(40);lv_timer_handler();assert(qpk_review_eval("if(__cancelledCalls)throw Error('deleted viewport callback survived')")==0);
    assert(qpk_review_eval(
      "var __lr=ui.panel(20,20,234,48,0),__lt=ui.panel(20,20,6,28,0xffffff),__fresh=0,__rejected=false;"
      "try{ui.rangeLinked(__lr,{valueOf(){ui.remove(__lt);__fresh=ui.panel(20,20,6,28,0xffffff);return 128}},0xffa64e,0x303640,true,__lt)}catch(e){__rejected=true}"
      "if(!__rejected)throw Error('reused linked thumb accepted');ui.remove(__fresh);ui.remove(__lr)")==0);
    printf("WATERFALL_POOL theme=%u widgets=%u children=%u\n",theme,qpk_runtime_widget_count(),(unsigned)lv_obj_get_child_count(viewport));
    assert(qpk_review_eval(
      "__haStates=["
      "{entity_id:'light.living',state:'on',attributes:{friendly_name:'客厅吊灯',brightness:180,supported_color_modes:['brightness']}},"
      "{entity_id:'switch.desk',state:'on',attributes:{friendly_name:'书桌插座'}},"
      "{entity_id:'fan.purifier',state:'on',attributes:{friendly_name:'空气净化器',device_type:'air-purifier',percentage:40,pm25:12}},"
      "{entity_id:'light.bed',state:'on',attributes:{friendly_name:'床头灯',brightness:80,supported_color_modes:['brightness']}},"
      "{entity_id:'switch.lamp',state:'off',attributes:{friendly_name:'台灯插座'}},"
      "{entity_id:'sensor.temperature',state:'24.6',attributes:{friendly_name:'室内温度',device_class:'temperature',unit_of_measurement:'C'}},"
      "{entity_id:'sensor.humidity',state:'48',attributes:{friendly_name:'室内湿度',device_class:'humidity',unit_of_measurement:'%'}},"
      "{entity_id:'climate.room',state:'cool',attributes:{friendly_name:'卧室空调',temperature:24,current_temperature:26}},"
      "{entity_id:'cover.curtain',state:'open',attributes:{friendly_name:'阳台窗帘',current_position:65}},"
      "{entity_id:'camera.door',state:'idle',attributes:{friendly_name:'入户摄像机',device_type:'camera'}},"
      "{entity_id:'media_player.speaker',state:'paused',attributes:{friendly_name:'客厅音箱',volume_level:0.4}},"
      "{entity_id:'binary_sensor.door',state:'off',attributes:{friendly_name:'入户门磁',device_class:'door'}}];"
      "ESPHomeHA.refresh();for(let i=0;i<90;i++)__reviewTick();ui.scrollTo(__fall.viewport,0,false)")==0);
    fluid_frames(display,"waterfall-preview",18);review_capture(display,"waterfall-preview","米家瀑布流 · 混合设备");
    unsigned visible_thumbs=0;
    for(unsigned i=0;i<lv_obj_get_child_count(viewport);i++){
      lv_obj_t *child=lv_obj_get_child(viewport,i);
      if(lv_obj_get_width(child)!=6||lv_obj_get_height(child)!=28||!lv_obj_is_visible(child))continue;
      lv_area_t a;lv_obj_get_coords(child,&a);
      int px=(a.x1+a.x2)/2,py=(a.y1+a.y2)/2;
      if(py<root_area.y1+64||py>=root_area.y1+64+(int)qpk_review_number("__fall.h"))continue;
      assert(px>=0&&px<1024&&py>=0&&py<600);
      unsigned pixel=((unsigned)review_fb[(py*1024+px)*2+1]<<8)|review_fb[(py*1024+px)*2];
      assert(pixel==0xffff); /* The white handle must be painted above its track. */
      visible_thumbs++;
    }
    assert(visible_thumbs>=2);
    assert(qpk_review_eval("ui.scrollTo(__fall.viewport,160,true)")==0);fluid_frames(display,"waterfall-scroll-one",24);
    assert(qpk_review_eval("ui.scrollTo(__fall.viewport,320,true)")==0);fluid_frames(display,"waterfall-scroll-two",24);
    assert(qpk_review_eval("ui.scrollTo(__fall.viewport,0,true)")==0);fluid_frames(display,"waterfall-scroll-back",24);
    assert(qpk_review_eval("__haStates[0].attributes.brightness=235;ESPHomeHA.refresh();for(let i=0;i<60;i++)__reviewTick()")==0);
    fluid_frames(display,"waterfall-slider-up",24);
    assert(qpk_review_eval("__haStates[0].attributes.brightness=70;ESPHomeHA.refresh();for(let i=0;i<60;i++)__reviewTick()")==0);
    fluid_frames(display,"waterfall-slider-down",24);
    assert(qpk_review_click_at(910,32)==0);assert(qpk_review_eval("for(let i=0;i<50;i++)__reviewTick();if(!ESPHomeHA.snapshot().discovery.open)throw Error('filter did not open')")==0);
    assert(lv_obj_has_flag(viewport,LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(viewport,LV_OBJ_FLAG_SCROLLABLE));
    fluid_frames(display,"waterfall-discovery",12);review_capture(display,"waterfall-discovery","设备筛选");
    assert(qpk_review_click_at(420,166)==0);assert(qpk_review_eval("for(let i=0;i<60;i++)__reviewTick();if(ESPHomeHA.snapshot().discovery.count!==0)throw Error('favorites filter did not apply')")==0);
    fluid_frames(display,"waterfall-empty",12);review_capture(display,"waterfall-empty","无匹配设备");
    assert(qpk_review_click_at(278,262)==0);assert(qpk_review_eval("for(let i=0;i<60;i++)__reviewTick();if(ESPHomeHA.snapshot().discovery.count!==12)throw Error('clear filter failed')")==0);
    assert(qpk_review_eval("globalThis.__searchCallback=null;prompt.input=(options,callback)=>__searchCallback=callback")==0);
    assert(qpk_review_click_at(866,32)==0);assert(qpk_review_eval("for(let i=0;i<20;i++)__reviewTick();if(!__searchCallback)throw Error('search input did not open');__searchCallback('light.living');for(let i=0;i<60;i++)__reviewTick();if(ESPHomeHA.snapshot().discovery.count!==1)throw Error('search failed')")==0);
    fluid_frames(display,"waterfall-search",12);review_capture(display,"waterfall-search","搜索设备");
    assert(qpk_review_click_at(910,32)==0);assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick()")==0);
    assert(qpk_review_click_at(400,278)==0);assert(qpk_review_eval("for(let i=0;i<60;i++)__reviewTick();if(ESPHomeHA.snapshot().search)throw Error('clear search failed')")==0);
    pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,motion_pointer_read);
    motion_pointer(pointer,310,root_area.y1+180,true);
    assert(!lv_obj_has_flag(viewport,LV_OBJ_FLAG_SCROLLABLE));
    assert(qpk_review_eval("ESPHomeHA.settings();for(let i=0;i<40;i++)__reviewTick()")==0);
    assert(lv_obj_has_flag(viewport,LV_OBJ_FLAG_SCROLLABLE));
    motion_pointer(pointer,310,root_area.y1+180,false);
    assert(qpk_review_click_at(192,30)==0);assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick()")==0);
    motion_pointer(pointer,310,root_area.y1+180,true);
    assert(!lv_obj_has_flag(viewport,LV_OBJ_FLAG_SCROLLABLE));
    assert(qpk_review_eval("ESPHomeHA.disconnect()")==0);
    assert(lv_obj_has_flag(viewport,LV_OBJ_FLAG_SCROLLABLE));
    motion_pointer(pointer,310,root_area.y1+180,false);
    assert(qpk_review_eval("if(ESPHomeHA.snapshot().brightnessInput!==null)throw Error('late release survived disconnect')")==0);
    lv_indev_delete(pointer);
    panel_hide(NULL);
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  lv_obj_delete(g_desktop.screen);if(g_motion_timer)lv_timer_delete(g_motion_timer);g_motion_timer=NULL;
  lv_tiny_ttf_destroy(g_desktop.font16);lv_tiny_ttf_destroy(g_desktop.font20);lv_tiny_ttf_destroy(g_desktop.font28);
  lv_tiny_ttf_destroy(g_glass.font48);free(data);lv_display_delete(display);lv_deinit();
  assert(g_ui_metrics.frames>0&&g_ui_metrics.refreshes>0);
  assert(g_ui_metrics.render_us>=g_ui_metrics.render_max_us);
  glass_ui_metrics_reset();assert(!g_ui_metrics.frames&&!g_ui_metrics.refreshes);
  puts("WATERFALL_UI_OK: light/dark; 512 cards; native clipping; bounded pool; scroll middle/bottom/return; detail target; generation safety; discovery/empty/search; render metrics");return 0;
}
