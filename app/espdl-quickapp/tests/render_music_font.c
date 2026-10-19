/* SPDX-License-Identifier: Apache-2.0 */
#define MUSIC_FULL_NO_MAIN
#include "render_music_full.c"
#include <unistd.h>
extern void review_lyrics_translation_fixture(const char *);
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];
  FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);
  void *base=malloc(n);assert(base&&fread(base,1,n,f)==n);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,review_flush);
  const unsigned sizes[]={16,20,28,48,72};lv_font_t *faces[5];
  for(unsigned i=0;i<5;i++){faces[i]=lv_tiny_ttf_create_data(base,n,sizes[i]);assert(faces[i]);}
  g_desktop.font16=faces[0];g_desktop.font20=faces[1];g_desktop.font28=faces[2];
  g_glass.font48=faces[3];g_glass.music_font72=faces[4];
  const uint32_t missing[]={0x524e,0x537b,0x5f9e,0x611b,0x61f7,0x6703,0x68c4,
    0x6c92,0x7aa9,0x7e31,0x7f88,0x8207,0x88e1,0x898a,0x89ba,0x8ab0,0x8ad2,
    0x8b8a,0x8d95,0x8e64,0x9019,0x904e,0x9060,0x95ca,0x9727,0x98a8,0x98c4};
  unsigned before=0;
  for(unsigned j=0;j<27;j++){lv_font_glyph_dsc_t d={0};
    if(!lv_font_get_glyph_dsc(faces[2],&d,missing[j],0)||d.is_placeholder)before++;}
  assert(before==27);
  glass_sd_font_start();int changed=0;
  bool expected_failure=getenv("MUSIC_FONT_EXPECT_FAILURE")!=NULL;
  for(unsigned i=0;i<2000&&!changed;i++){
    changed=glass_sd_font_poll();int status,error;glass_sd_font_status(&status,&error);
    if(expected_failure&&status==3)break;
    usleep(10000);
  }
  int state,error;glass_sd_font_status(&state,&error);
  if(expected_failure){
    assert(state==3&&error<0&&!changed&&!glass_sd_font_get(28));
    puts("MUSIC_FONT_REJECTED: missing or corrupt SD font; embedded font retained");return 0;
  }
  assert(state==2&&error==0&&changed==1);
  for(unsigned i=0;i<5;i++){
    lv_font_t *fallback=glass_sd_font_get(sizes[i]);assert(fallback);
    fallback->fallback=glass_icon_font(i==2?24:sizes[i]);faces[i]->fallback=fallback;
    for(unsigned j=0;j<27;j++){lv_font_glyph_dsc_t d={0};
      assert(lv_font_get_glyph_dsc(faces[i],&d,missing[j],0)&&!d.is_placeholder);
      assert(d.box_w&&d.box_h&&d.resolved_font==fallback);
    }
  }
  static const char *const languages[]={"日本語の歌 カタカナ","안녕하세요 세상 음악",
    "Français Español Português","Tiếng Việt mùa yêu thương",
    "Ελληνικά μουσική","Привет мир Українська пісня","Türkçe şarkı İstanbul"};
  for(unsigned i=0;i<5;i++)for(unsigned sample=0;sample<7;sample++){
    const unsigned char *s=(const unsigned char *)languages[sample];
    while(*s){uint32_t cp=*s++;
      if(cp>=0xe0){cp=(cp&15)<<12;cp|=(*s++&63)<<6;cp|=*s++&63;}
      else if(cp>=0xc0){cp=(cp&31)<<6;cp|=*s++&63;}
      lv_font_glyph_dsc_t d={0};
      assert(lv_font_get_glyph_dsc(faces[i],&d,cp,0)&&!d.is_placeholder);
    }
  }
  lv_font_glyph_dsc_t icon={0};
  assert(lv_font_get_glyph_dsc(faces[1],&icon,0xe00d,0)&&!icon.is_placeholder);
  assert(music_pv_font(72)==faces[4]);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);
  review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  g_desktop.light_theme=false;g_glass.reduced_motion=true;
  g_desktop.screen=glass_box(lv_screen_active(),0,32,1024,568,0,0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,568,0,0);
  lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  review_music_fixture(2);
  struct music_now_playing now={.revision=40,.track_id=100,.position_ms=192500,
    .duration_ms=600000,.volume=40,.state=MUSIC_PLAYING,.lyrics_state=MUSIC_LYRICS_READY,
    .can_skip=true,.lyrics_started=true};
  strcpy(now.name,"晚风里的歌");strcpy(now.artist,"声动 · 原创演示");
  strcpy(now.lyrics[1],"風の中で歌を聴こう");strcpy(now.translations[1],"在晚风里，听一首歌");
  review_music_now_fixture(&now);review_lyrics_fixture(192,48);
  review_lyrics_text_fixture("誰會與我遠飄");
  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme!=0;glass_music(NULL);lv_obj_update_layout(g_desktop.screen);
    assert(lv_obj_get_y(g_music_ui.lyric)+lv_obj_get_height(g_music_ui.lyric)<=lv_obj_get_height(lv_obj_get_parent(g_music_ui.lyric)));
    assert(lv_obj_get_height(lv_obj_get_parent(g_music_ui.progress))==68);
    review_capture(display,"music-polished","音乐 · 灰紫粉色、分组侧栏和悬浮播放器");
    panel_hide(NULL);
  }
  g_desktop.light_theme=false;
  glass_music(NULL);music_full_open(NULL);assert(g_music_ui.full);
  review_capture(display,"font-traditional-lyrics","SD 简繁体歌词");
  lv_obj_send_event(g_music_ui.full->mode,LV_EVENT_CLICKED,NULL);
  for(unsigned style=0;style<3;style++){
    struct music_pv_ui *pv=&g_music_ui.full->pv;
    music_pv_refresh(pv);assert(pv->valid);
    char name[64];snprintf(name,sizeof(name),"font-traditional-style-%u",style);
    review_capture(display,name,"简繁体 PV 字形回退");
    if(style<2)lv_obj_send_event(pv->style_button,LV_EVENT_CLICKED,NULL);
  }
  for(unsigned language=0;language<7;language++) {
    struct music_pv_ui *pv=&g_music_ui.full->pv;
    review_lyrics_text_fixture(languages[language]);pv->valid=false;music_pv_refresh(pv);
    char name[64];snprintf(name,sizeof(name),"font-language-%u",language);
    review_capture(display,name,languages[language]);
    assert(pv->valid);
  }
  review_lyrics_text_fixture("日本語の歌と韓国語の音楽");
  review_lyrics_translation_fixture("日语的歌与韩语的音乐");
  lv_obj_send_event(g_music_ui.full->mode,LV_EVENT_CLICKED,NULL);
  music_full_refresh(&now);
  unsigned slot=g_music_ui.full->window.current-g_music_ui.full->window.first;
  assert(!strcmp(lv_label_get_text(g_music_ui.full->translations[slot]),"日语的歌与韩语的音乐"));
  assert(!lv_obj_has_flag(g_music_ui.full->translations[slot],LV_OBJ_FLAG_HIDDEN));
  review_capture(display,"font-bilingual-ordinary","双语歌词 · 原文与简体译文");
  lv_obj_send_event(g_music_ui.full->bilingual,LV_EVENT_CLICKED,NULL);
  assert(g_glass.music_original_only&&lv_obj_has_flag(g_music_ui.full->translations[slot],LV_OBJ_FLAG_HIDDEN));
  lv_obj_send_event(g_music_ui.full->bilingual,LV_EVENT_CLICKED,NULL);
  assert(!g_glass.music_original_only);
  lv_obj_send_event(g_music_ui.full->mode,LV_EVENT_CLICKED,NULL);
  for(unsigned style=0;style<3;style++){
    struct music_pv_ui *pv=&g_music_ui.full->pv;pv->style_index=style;pv->valid=false;music_pv_refresh(pv);
    assert(!lv_obj_has_flag(pv->translation,LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(pv->translation),"日语的歌与韩语的音乐"));
    char name[64];snprintf(name,sizeof(name),"font-bilingual-style-%u",style);
    review_capture(display,name,"原文与简体译文 · 三套效果");
    for(unsigned i=0;i<pv->count;i++)assert(lv_obj_get_y(pv->groups[i])+lv_obj_get_height(pv->groups[i])<=284);
  }
  review_lyrics_translation_fixture(NULL);music_pv_refresh(&g_music_ui.full->pv);
  assert(lv_obj_has_flag(g_music_ui.full->pv.translation,LV_OBJ_FLAG_HIDDEN));
  music_full_close(NULL);
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  puts("MUSIC_FONT_OK: 27 missing glyphs fixed at 5 sizes; Japanese Korean Latin Vietnamese Greek Cyrillic Turkish; icons retained; 3 PV styles");
  return 0;
}
