#define main all_ui_reference_main
#include "render_all_ui.c"
#undef main
extern double qpk_review_number(const char *source);
extern void qpk_review_root_area(lv_area_t *area);
extern unsigned qpk_review_gesture_count(void);
static lv_indev_data_t pointer_data;
static void pointer_read(lv_indev_t *indev,lv_indev_data_t *data){(void)indev;*data=pointer_data;}
static void pointer_move(lv_indev_t *indev,int x,int y,bool pressed)
{
  pointer_data.point=(lv_point_t){x,y};pointer_data.state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
  lv_tick_inc(16);lv_indev_read(indev);
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
  void *font=malloc(size);assert(font);fread(font,1,size,f);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(font,size,16);g_desktop.font20=lv_tiny_ttf_create_data(font,size,20);g_desktop.font28=lv_tiny_ttf_create_data(font,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);
  g_glass.reduced_motion=true;g_glass.temperature_c=-999;g_glass.humidity_pct=-1;g_glass.pm25=-1;
  g_desktop.screen=lv_screen_active();lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0x000000,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  lv_indev_t *pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_display(pointer,display);lv_indev_set_read_cb(pointer,pointer_read);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  for(unsigned theme=0;theme<2;theme++) {
    qpk_review_hass_fixture(true);panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();glass_homeassistant(NULL);
    assert(qpk_review_eval("globalThis.__haControlCount=0;system.homeAssistantService.control=(entity,on,brightness)=>{__haControlCount++;const s=__haStates.find(s=>s.entity_id===entity);s.state=on?'on':'off';if(brightness>=0)s.attributes.brightness=Math.round(brightness*255/100);const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:'[]'});return id;}")==0);
    assert(qpk_review_eval("__haStates=[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'台灯',brightness:194,color_temp_kelvin:2800,supported_color_modes:['color_temp']}},{entity_id:'light.bed',state:'on',attributes:{friendly_name:'床头灯',brightness:41,supported_color_modes:['brightness']}},{entity_id:'switch.plug',state:'off',attributes:{friendly_name:'智能插座'}},{entity_id:'sensor.temperature',state:'24.5',attributes:{friendly_name:'室内温度',device_class:'temperature',unit_of_measurement:'°C'}}];ESPHomeHA.refresh();for(let i=0;i<60;i++)__reviewTick();globalThis.__rail=ESPHomeHA.snapshot().brightnessControls.find(r=>r.entity==='light.desk');if(!__rail||__rail.h!==64)throw Error('small slider missing')")==0);
    review_capture(display,"hass-lamp-small","参考样式 · 灯具小卡片");
    double x=qpk_review_number("__rail.x"),y=qpk_review_number("__rail.y"),w=qpk_review_number("__rail.w"),h=qpk_review_number("__rail.h");
    lv_area_t area;qpk_review_root_area(&area);
    int global_y=area.y1+(int)round(y+h/2);
    pointer_move(pointer,area.x1+(int)round(x+w*.2),global_y,true);
    for(int i=0;i<60;i++) {
      double t=.2+.6*i/59;pointer_move(pointer,area.x1+(int)round(x+w*t),global_y,true);
      assert(qpk_review_number("ESPHomeHA.snapshot().brightnessInput?.dragging?1:0")==1);
      double percent=qpk_review_number("ESPHomeHA.snapshot().brightnessInput.percent");
      if(fabs(percent-t*100)>1)fprintf(stderr,"MOVE %d expected=%.2f actual=%.2f coords x=%.2f y=%d root=%d,%d\n",i,t*100,percent,x+w*t,global_y,area.x1,area.y1);
      assert(fabs(percent-t*100)<=1);
      if(i==30)assert(qpk_review_eval("for(let i=0;i<10;i++)__reviewTick()")==0);
    }
    assert(review_label_find(g_desktop.screen,"80%"));review_capture(display,"hass-lamp-small-drag","真实 LVGL 指针 · 60 次连续移动");
    pointer_move(pointer,area.x1+(int)round(x+w*.8),global_y,false);
    assert(qpk_review_eval("for(let i=0;i<25;i++)__reviewTick();if(ESPHomeHA.snapshot().detailOpen)throw Error('slider release opened details');if(__haControlCount!==1)throw Error('release must commit exactly once')")==0);
    printf("NATIVE_POINTER_SLIDER_OK %s: 60 moves, no card click\n",theme?"dark":"light");
    assert(qpk_review_click_at(900,30)==0);assert(qpk_review_eval("for(let i=0;i<25;i++)__reviewTick()")==0);
    assert(review_label_find(g_desktop.screen,"设为大卡"));review_capture(display,"hass-lamp-editor","固定网格 · 独立尺寸按钮");
    assert(qpk_review_click_at(300,233)==0);assert(qpk_review_eval("for(let i=0;i<45;i++)__reviewTick()")==0);
    assert(review_label_find(g_desktop.screen,"设为小卡"));
    assert(qpk_review_click_at(900,30)==0);assert(qpk_review_eval("for(let i=0;i<30;i++)__reviewTick()")==0);
    assert(qpk_review_eval("globalThis.__rail=ESPHomeHA.snapshot().brightnessControls.find(r=>r.entity==='light.bed');if(ESPHomeHA.snapshot().layoutPages.length<2)throw Error('gesture test needs another page')")==0);
    x=qpk_review_number("__rail.x");y=qpk_review_number("__rail.y");w=qpk_review_number("__rail.w");h=qpk_review_number("__rail.h");
    qpk_review_root_area(&area);global_y=area.y1+(int)round(y+h/2);
    unsigned gestures=qpk_review_gesture_count();
    pointer_move(pointer,area.x1+(int)round(x+w*.85),global_y,true);
    pointer_move(pointer,area.x1+(int)round(x+w*.15),global_y,true);
    pointer_move(pointer,area.x1+(int)round(x+w*.12),global_y,true);
    assert(qpk_review_gesture_count()>gestures);
    assert(qpk_review_eval("if(ESPHomeHA.snapshot().roomPage!==0)throw Error('native slider gesture paged the board');__reviewTouch('cancel',0,0)")==0);
    pointer_move(pointer,area.x1+(int)round(x+w*.12),global_y,false);
    assert(qpk_review_eval("for(let i=0;i<30;i++)__reviewTick();if(ESPHomeHA.snapshot().roomPage!==0||__haControlCount!==1)throw Error('cancelled gesture leaked a page or service')")==0);
    assert(review_label_find(g_desktop.screen,"16%"));
    printf("NATIVE_GESTURE_OWNERSHIP_OK %s\n",theme?"dark":"light");
    review_capture(display,"hass-lamp-mixed","原有大卡与参考小卡混排");
    assert(qpk_review_eval("globalThis.__holdHa=true;globalThis.__oldHaPoll=system.homeAssistantService.poll;system.homeAssistantService.poll=()=>__holdHa?{busy:true,done:false}:__oldHaPoll();globalThis.__rail=ESPHomeHA.snapshot().brightnessControls.find(r=>r.entity==='light.desk')")==0);
    x=qpk_review_number("__rail.x");y=qpk_review_number("__rail.y");w=qpk_review_number("__rail.w");h=qpk_review_number("__rail.h");
    qpk_review_root_area(&area);global_y=area.y1+(int)round(y+h/2);
    pointer_move(pointer,area.x1+(int)round(x+w*.45),global_y,true);pointer_move(pointer,area.x1+(int)round(x+w*.45),global_y,false);
    assert(qpk_review_eval("for(let i=0;i<25;i++)__reviewTick();if(__haControlCount!==2)throw Error('first control missing');globalThis.__rail=ESPHomeHA.snapshot().brightnessControls.find(r=>r.entity==='light.bed');if(!__rail)throw Error('another device slider disappeared while busy')")==0);
    x=qpk_review_number("__rail.x");y=qpk_review_number("__rail.y");w=qpk_review_number("__rail.w");h=qpk_review_number("__rail.h");
    global_y=area.y1+(int)round(y+h/2);
    pointer_move(pointer,area.x1+(int)round(x+w*.5),global_y,true);pointer_move(pointer,area.x1+(int)round(x+w*.5),global_y,false);
    assert(qpk_review_eval("for(let i=0;i<25;i++)__reviewTick();if(__haControlCount!==2||ESPHomeHA.snapshot().controlQueue.length!==1)throw Error('busy control was not serialized')")==0);
    assert(review_label_find(g_desktop.screen,"50%"));assert(review_label_find(g_desktop.screen,"待执行"));
    review_capture(display,"hass-control-waiting","A 等待回执 · B 滑条仍可操作");
    assert(qpk_review_eval("__holdHa=false;for(let i=0;i<60;i++)__reviewTick();if(__haControlCount!==3||ESPHomeHA.snapshot().controlQueue.length)throw Error('queued control did not finish exactly once');if(ESPHomeHA.snapshot().selectedId!=='light.bed')throw Error('verification stole the selected device')")==0);
    printf("NATIVE_CONTROL_QUEUE_OK %s\n",theme?"dark":"light");
    for(unsigned use=0;use<2;use++){
      assert(qpk_review_eval("globalThis.__tile=ESPHomeHA.snapshot().layoutPages[0].find(r=>r.entity_id==='light.bed')")==0);
      x=qpk_review_number("__tile.x+90");y=qpk_review_number("__tile.y+25");
      pointer_move(pointer,area.x1+(int)round(x),area.y1+(int)round(y),true);
      pointer_move(pointer,area.x1+(int)round(x),area.y1+(int)round(y),false);
      assert(qpk_review_eval("for(let i=0;i<30;i++)__reviewTick();if(!ESPHomeHA.snapshot().detailOpen)throw Error('frequent-device open missing')")==0);
      assert(qpk_review_click_at(60,89)==0);assert(qpk_review_eval("for(let i=0;i<30;i++)__reviewTick()")==0);
    }
    assert(qpk_review_eval("if(ESPHomeHA.snapshot().layoutPages[0][0].entity_id!=='light.bed')throw Error('common small card was pushed behind large cards')")==0);
    review_capture(display,"hass-common-home","常用小卡优先 · 保留大小卡组合");
    printf("NATIVE_COMMON_HOME_OK %s\n",theme?"dark":"light");
    assert(qpk_review_eval("ESPHomeHA.disconnect();globalThis.__actions=[];system.homeAssistantService.callService=(entity,action,data)=>{const b=JSON.parse(data);__actions.push(b);const s=__haStates.find(s=>s.entity_id===entity);if(action==='turn_on')s.state='on';if(b.brightness_pct!==undefined)s.attributes.brightness=Math.round(b.brightness_pct*255/100);if(b.color_temp_kelvin!==undefined)s.attributes.color_temp_kelvin=b.color_temp_kelvin;if(b.value!==undefined)s.state=String(b.value);if(b.option!==undefined)s.state=b.option;const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:'[]'});return id;};const oldGet=system.homeAssistantService.get;system.homeAssistantService.get=resource=>{if(resource!=='services')return oldGet(resource);const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:JSON.stringify([{domain:'light',services:{turn_on:{},turn_off:{}}},{domain:'number',services:{set_value:{}}},{domain:'select',services:{select_option:{}}},{domain:'button',services:{press:{}}}])});return id;};__haStates=[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'台灯',device_id:'lamp',device_name:'书房台灯',brightness:194,color_temp_kelvin:2800,min_color_temp_kelvin:2700,max_color_temp_kelvin:6500,supported_color_modes:['color_temp']}},{entity_id:'number.limit',state:'2',attributes:{friendly_name:'夜灯亮度',device_id:'lamp',min:0,max:10,step:0.5}},{entity_id:'select.mode',state:'自动',attributes:{friendly_name:'工作模式',device_id:'lamp',options:['自动','柔光','阅读']}},{entity_id:'button.test',state:'unknown',attributes:{friendly_name:'测试功能',device_id:'lamp'}},{entity_id:'sensor.temperature',state:'24.5',attributes:{friendly_name:'温度',device_id:'lamp'}}];ESPHomeHA.refresh();for(let i=0;i<60;i++)__reviewTick();globalThis.__tile=ESPHomeHA.snapshot().layoutPages[0][0]")==0);
    lv_obj_t *more_label=review_label_find(g_desktop.screen,"更多操作");assert(more_label);
    lv_area_t more_area;qpk_review_root_area(&area);lv_obj_get_coords(more_label,&more_area);
    assert(qpk_review_click_at((more_area.x1+more_area.x2)/2-area.x1,(more_area.y1+more_area.y2)/2-area.y1)==0);
    assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(!ESPHomeHA.snapshot().detailAdvanced)throw Error('More missing');globalThis.__ct=ESPHomeHA.snapshot().parameterControls.find(r=>r.key==='color_temp_kelvin');if(!__ct)throw Error('color control missing')")==0);
    review_capture(display,"hass-more-lamp","更多功能 · 亮度与色温控制");
    x=qpk_review_number("__ct.x");y=qpk_review_number("__ct.y");w=qpk_review_number("__ct.w");h=qpk_review_number("__ct.h");
    qpk_review_root_area(&area);global_y=area.y1+(int)round(y+h/2);
    snprintf(path,sizeof(path),"%s/%s-ct-geometry.json",review_out,theme?"dark":"light");
    FILE *geometry=fopen(path,"w");assert(geometry);
    fprintf(geometry,"{\"warm\":[%d,%d],\"cool\":[%d,%d]}",area.x1+(int)round(x+12),area.y1+(int)round(y+h/2),area.x1+(int)round(x+w-12),area.y1+(int)round(y+h/2));fclose(geometry);
    pointer_move(pointer,area.x1+(int)round(x+w*.5),global_y,true);
    for(int i=0;i<60;i++)pointer_move(pointer,area.x1+(int)round(x+w*.5),global_y,true);
    assert(qpk_review_number("__actions.length")==0);
    pointer_move(pointer,area.x1+(int)round(x+w*.5),global_y,false);
    assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(__actions.length!==1||__actions[0].color_temp_kelvin!==4600||ESPHomeHA.snapshot().phase!=='ready')throw Error('typed color control failed')")==0);
    review_capture(display,"hass-more-color","色温 4600K · 原生触摸提交一次");
    assert(qpk_review_eval("globalThis.__num=ESPHomeHA.snapshot().parameterControls.find(r=>r.entity==='number.limit');if(!__num)throw Error('integrated numeric control missing')")==0);
    x=qpk_review_number("__num.x");y=qpk_review_number("__num.y");w=qpk_review_number("__num.w");h=qpk_review_number("__num.h");
    pointer_move(pointer,area.x1+(int)round(x+w*.75),area.y1+(int)round(y+h/2),true);
    pointer_move(pointer,area.x1+(int)round(x+w*.75),area.y1+(int)round(y+h/2),false);
    assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(__actions.length!==2||__actions[1].value!==7.5||ESPHomeHA.snapshot().selectedId!=='light.desk')throw Error('integrated number changed device or value')")==0);
    review_capture(display,"hass-more-number","同一大卡片 · 数值控制 7.5");
    assert(qpk_review_click_at(870,312)==0);
    assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(__actions.length!==3||__actions[2].option!=='柔光'||ESPHomeHA.snapshot().phase!=='ready')throw Error('integrated mode control failed')")==0);
    review_capture(display,"hass-more-mode","同一大卡片 · 模式柔光");
    assert(qpk_review_click_at(870,442)==0);
    assert(qpk_review_eval("for(let i=0;i<35;i++)__reviewTick();if(__actions.length!==4||ESPHomeHA.snapshot().phase!=='ready')throw Error('integrated button failed')")==0);
    printf("NATIVE_TYPED_MORE_OK %s: color, function chooser, 60 moves, one release\n",theme?"dark":"light");
    panel_hide(NULL);
  }
  lv_indev_delete(pointer);fputs("\n]\n",review_manifest);fclose(review_manifest);puts("HASS_SLIDER_NATIVE_UI_OK");return 0;
}
