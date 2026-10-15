/* Production QuickJS/LVGL pagination, with offline state and no device I/O. */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_statusbar_devices.c"

static void pager_click(unsigned page)
{
  char js[320];
  snprintf(js,sizeof(js),"globalThis.__dot=ESPHomeHA.snapshot().pageIndicator.slots.find(s=>s.page===%u);if(!__dot)throw Error('page outside indicator')",page);
  assert(qpk_review_eval(js)==0);
  assert(qpk_review_click_at((int)qpk_review_number("__dot.x+12"),(int)qpk_review_number("__dot.y+20"))==0);
  snprintf(js,sizeof(js),"for(let i=0;i<30;i++)__reviewTick();if(ESPHomeHA.snapshot().roomPage!==%u)throw Error('wrong selected page')",page);
  assert(qpk_review_eval(js)==0);
}

static void ha_text_click(const char *text)
{
  lv_obj_t *label=review_label_find(g_desktop.screen,text);assert(label);
  lv_area_t a,root;qpk_review_root_area(&root);lv_obj_get_coords(label,&a);
  assert(qpk_review_click_at((a.x1+a.x2)/2-root.x1,(a.y1+a.y2)/2-root.y1)==0);
  assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick()")==0);
}
static void ha_indicator_click(const char *kind,unsigned page)
{
  char js[320];snprintf(js,sizeof(js),"globalThis.__dot=ESPHomeHA.snapshot().%s.slots.find(s=>s.page===%u);if(!__dot)throw Error('missing indicator page')",kind,page);
  assert(qpk_review_eval(js)==0);assert(qpk_review_click_at((int)qpk_review_number("__dot.x+12"),(int)qpk_review_number("__dot.y+20"))==0);
  assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick()")==0);
}
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
  g_desktop.screen=glass_box(lv_screen_active(),0,GLASS_STATUS_HEIGHT,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  for(unsigned theme=0;theme<2;theme++)
    {
      qpk_review_hass_fixture(true);panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();glass_homeassistant(NULL);
      assert(qpk_review_eval(
        "system.homeAssistantService.callService=system.homeAssistantService.control=()=>{throw Error('no device commands in pager review')};"
        "__haStates=Array.from({length:18},(_,i)=>({entity_id:'light.pager_'+i,state:'on',attributes:{friendly_name:'灯具 '+(i+1),brightness:200,supported_color_modes:['brightness']}}));"
        "ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();"
        "if(ESPHomeHA.snapshot().pageIndicator.total!==3)throw Error('three pages expected');")==0);
      review_capture(display,"ha-pager-first","三页 · 首页");
      pager_click(1);review_capture(display,"ha-pager-middle","三页 · 中间页");
      pager_click(2);review_capture(display,"ha-pager-last","三页 · 末页");
      assert(qpk_review_eval("globalThis.__row=ESPHomeHA.snapshot().layoutPages[2][0]")==0);
      assert(qpk_review_click_at((int)qpk_review_number("__row.x+__row.w/2"),(int)qpk_review_number("__row.y+__row.h-24"))==0);
      assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick();if(ESPHomeHA.snapshot().pageIndicator.visible)throw Error('detail pager visible')")==0);
      review_capture(display,"ha-pager-detail","详情 · 隐藏分页");
      assert(qpk_review_click_at(190,31)==0);
      assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick();if(!ESPHomeHA.snapshot().pageIndicator.visible)throw Error('pager lost after detail')")==0);
      review_capture(display,"ha-pager-return","返回 · 保留末页");
      assert(qpk_review_eval("__haStates=Array.from({length:59},(_,i)=>({entity_id:'light.pager_'+i,state:'on',attributes:{friendly_name:'灯具 '+(i+1),brightness:200,supported_color_modes:['brightness']}}));ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick()")==0);
      for(unsigned page=3;page<10;page++)pager_click(page);
      review_capture(display,"ha-pager-many-last","十页 · 末页");
      for(unsigned page=8;page>3;page--)pager_click(page);
      review_capture(display,"ha-pager-many-middle","十页 · 中间页");
      assert(qpk_review_eval("__haStates=__haStates.slice(0,1);ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();if(ESPHomeHA.snapshot().roomPage!==0||ESPHomeHA.snapshot().pageIndicator.visible)throw Error('single page clamp failed')")==0);
      review_capture(display,"ha-pager-single","单页 · 隐藏分页");
      check_visible_icons(g_desktop.screen,0);panel_hide(NULL);
      qpk_review_hass_fixture(true);glass_homeassistant(NULL);
      assert(qpk_review_eval(
        "system.homeAssistantService.callService=system.homeAssistantService.control=()=>{throw Error('no commands during option review')};"
        "__haStates=[{entity_id:'light.effect',state:'on',attributes:{friendly_name:'床头灯',brightness:200,supported_features:4,supported_color_modes:['color_temp','rgb'],color_temp_kelvin:2700,min_color_temp_kelvin:2700,max_color_temp_kelvin:6500,effect:'effect-0',effect_list:Array.from({length:9},(_,i)=>'effect-'+i)}}];"
        "ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();globalThis.__row=ESPHomeHA.snapshot().layoutPages[0][0];")==0);
      assert(qpk_review_click_at((int)qpk_review_number("__row.x+__row.w/2"),(int)qpk_review_number("__row.y+__row.h-24"))==0);
      assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick()")==0);
      ha_text_click("灯光效果");assert(qpk_review_number("ESPHomeHA.snapshot().optionPageIndicator.visible")==1);
      review_capture(display,"ha-options-first","米家选项 · 首页");
      ha_indicator_click("optionPageIndicator",2);assert(qpk_review_number("ESPHomeHA.snapshot().optionPicker.page")==2);
      review_capture(display,"ha-options-last","米家选项 · 末页");ha_text_click("返回");
      panel_hide(NULL);qpk_review_hass_fixture(true);glass_homeassistant(NULL);
      assert(qpk_review_eval(
        "globalThis.__originalStatus=system.homeAssistantService.status;system.homeAssistantService.status=()=>Object.assign({},__originalStatus(),{deviceGroups:true});"
        "globalThis.__originalGet=system.homeAssistantService.get;system.homeAssistantService.get=r=>{if(r!=='devices')return __originalGet(r);const id=__haId++;__haPending.push({requestId:id,busy:false,done:true,status:200,error:0,body:JSON.stringify([['dev','多功能设备','',__haStates.map(s=>s.entity_id)]])});return id;};"
        "__haStates=[{entity_id:'light.main',state:'on',attributes:{friendly_name:'主灯',brightness:128,supported_color_modes:['brightness']}}].concat(Array.from({length:12},(_,i)=>({entity_id:'sensor.reading_'+i,state:String(i),attributes:{friendly_name:'读数 '+(i+1)}})));"
        "ESPHomeHA.refresh();for(let i=0;i<70;i++)__reviewTick();globalThis.__row=ESPHomeHA.snapshot().layoutPages[0][0];")==0);
      assert(qpk_review_click_at((int)qpk_review_number("__row.x+__row.w/2"),(int)qpk_review_number("__row.y+__row.h-24"))==0);
      assert(qpk_review_eval("for(let i=0;i<40;i++)__reviewTick();if(!ESPHomeHA.snapshot().detailPageIndicator.visible)throw Error('auxiliary pager missing')")==0);
      review_capture(display,"ha-detail-first","设备功能 · 首页");ha_indicator_click("detailPageIndicator",1);
      review_capture(display,"ha-detail-next","设备功能 · 下一页");panel_hide(NULL);
    }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("HA_PAGER_NATIVE_OK: %u captures, direct jumps and detail return\n",review_count);return 0;
}
