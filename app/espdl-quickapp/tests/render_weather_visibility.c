/* SPDX-License-Identifier: Apache-2.0 */
/* Real production page lifetime and gesture-translation coverage, offline I/O. */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_unified_devices.c"
extern void review_weather_snapshot_fixture(const struct glass_weather_snapshot *);
static unsigned resource_draws;
static void resource_count_draw(lv_event_t *e){LV_UNUSED(e);resource_draws++;}

static void visibility_update(lv_display_t *display,struct glass_weather_snapshot *w,
                              const char *condition,int temperature)
{
  strcpy(w->data.condition,condition);w->data.temperature=temperature;w->revision++;
  review_weather_snapshot_fixture(w);glass_update(NULL);lv_refr_now(display);
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
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);
  g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);
  g_glass.font72=lv_tiny_ttf_create_data_ex(font,size,128,LV_FONT_KERNING_NORMAL,24);
  g_glass.reduced_motion=true;
  g_desktop.screen=glass_box(lv_screen_active(),0,GLASS_STATUS_HEIGHT,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0,0);
  lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
  struct glass_weather_snapshot w={.valid=true,.state=WEATHER_READY,.revision=10,
    .received_ms=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000,
    .data={.temperature=24,.humidity=48,.aqi=32,.level=1,.pm25=12}};
  strcpy(w.data.location,"北京市");strcpy(w.data.condition,"多云");strcpy(w.data.category,"优");
  for(unsigned theme=0;theme<2;theme++){
    panel_hide(NULL);g_desktop.light_theme=theme!=0;
    review_weather_snapshot_fixture(&w);glass_rebuild();lv_refr_now(display);
    char previous[16];snprintf(previous,sizeof(previous),"%s",lv_label_get_text(g_glass.temperature));
    unsigned home_decodes=g_weather_home.scene.decodes;
    const void *home_pixels=g_weather_home.scene.pixels;
    glass_weather_open(NULL);lv_refr_now(display);assert(glass_page_covers_home());
    visibility_update(display,&w,"暴雨",17);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),previous));
    assert(g_weather_home.scene.decodes==home_decodes&&g_weather_home.scene.pixels==home_pixels);
    assert(!strcmp(lv_label_get_text(g_weather_detail.temperature),"17"));
    assert(g_weather_detail.scene.kind==SKY_RAIN_HEAVY);
    /* The real back gesture uses this style; the exposed home must refresh. */
    lv_obj_set_style_translate_x(g_weather_detail.root,16,0);
    assert(!glass_page_covers_home());glass_update(NULL);lv_refr_now(display);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"17°"));
    assert(g_weather_home.scene.kind==SKY_RAIN_HEAVY);
    /* Cancellation covers home again; the latest data appears on dismissal. */
    lv_obj_set_style_translate_x(g_weather_detail.root,0,0);
    visibility_update(display,&w,"小雪",-3);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"17°"));
    panel_hide(NULL);lv_refr_now(display);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"-3°"));
    assert(g_weather_home.scene.kind==SKY_SNOW&&!g_weather_home.deferred);
    assert(!g_weather_detail.root&&!g_weather_detail.frost_pixels&&!g_weather_detail.data);
    /* Transparency also exposes the background, so it must not be deferred. */
    glass_weather_open(NULL);lv_refr_now(display);
    lv_obj_set_style_opa(g_weather_detail.root,180,0);
    visibility_update(display,&w,"晴",26);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"26°"));
    assert(!g_weather_home.deferred);
    lv_obj_set_style_opa(g_weather_detail.root,LV_OPA_COVER,0);
    visibility_update(display,&w,"阴",20);assert(g_weather_home.deferred);
    /* Replacing the weather page must not leave the next app's home frozen. */
    assert(panel_card("系统页面"));assert(!g_weather_detail.root);
    glass_update(NULL);lv_refr_now(display);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"20°"));
    assert(g_weather_home.scene.kind==SKY_OVERCAST&&!g_weather_home.deferred);
    panel_hide(NULL);
    lv_obj_t *page=panel_card("系统页面");assert(page);detail_page(page);
    lv_refr_now(display);
    assert(glass_page_covers_home());
    visibility_update(display,&w,"多云",23);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"20°"));
    lv_obj_set_style_translate_y(page,8,0);assert(!glass_page_covers_home());
    glass_update(NULL);assert(!strcmp(lv_label_get_text(g_glass.temperature),"23°"));
    lv_obj_set_style_translate_y(page,0,0);
    visibility_update(display,&w,"晴",25);
    panel_hide(NULL);assert(!strcmp(lv_label_get_text(g_glass.temperature),"25°"));
    /* A failed optional snapshot allocation must only disable its cache. */
    free(g_weather_home.data);g_weather_home.data=NULL;
    visibility_update(display,&w,"多云",22);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"22°"));
    glass_weather_open(NULL);lv_refr_now(display);
    free(g_weather_detail.data);g_weather_detail.data=NULL;
    visibility_update(display,&w,"晴",25);
    assert(!strcmp(lv_label_get_text(g_weather_detail.temperature),"25"));
    panel_hide(NULL);lv_refr_now(display);
    assert(!strcmp(lv_label_get_text(g_glass.temperature),"25°"));
    glass_resources_page(NULL);lv_refr_now(display);assert(glass_page_covers_home());
    struct glass_system_sample sample={.cpu_tenths=350,.heap_total=32*1024*1024,
      .heap_free=28*1024*1024,.heap_largest=27*1024*1024,.heap_peak=5*1024*1024};
    for(unsigned i=0;i<60;i++){lv_tick_inc(1000);glass_resources_show(&sample);}
    lv_refr_now(display);
    lv_obj_add_event_cb(g_resources.chart,resource_count_draw,LV_EVENT_DRAW_MAIN,NULL);
    resource_draws=0;
    for(unsigned i=0;i<20;i++){lv_tick_inc(1000);glass_resources_show(&sample);lv_refr_now(display);}
    assert(resource_draws==0);
    sample.cpu_tenths=420;lv_tick_inc(1000);glass_resources_show(&sample);lv_refr_now(display);
    assert(resource_draws==1);
    assert(g_resources.history[0][59]==420);
    assert(!strcmp(lv_label_get_text(g_resources.labels[6]),"42.0%"));
    g_resources.selected=1;resources_present();lv_refr_now(display);
    assert(!strcmp(lv_label_get_text(g_resources.heading),"内存"));
    assert(!strcmp(lv_label_get_text(g_resources.labels[6]),"12.5%"));
    panel_hide(NULL);
    printf("WEATHER_VISIBILITY_OK theme=%u covered, retained cache, gesture reveal/cancel, transparency, return, replacement\n",theme);
  }
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);
  f=fopen(path,"w");assert(f);fputs("[]\n",f);fclose(f);
  return 0;
}
