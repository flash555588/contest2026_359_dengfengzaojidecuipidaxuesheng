/* SPDX-License-Identifier: Apache-2.0 */
/* Full-screen presentation only. Playback and lyric ownership stay in music. */
#define MUSIC_FULL_PITCH 76
#define MUSIC_FULL_HEIGHT 372
#define MUSIC_FULL_PAD ((MUSIC_FULL_HEIGHT-MUSIC_FULL_PITCH)/2)
#include "glass_music_pv.inc"
struct music_full_ui {
  lv_obj_t *root,*viewport,*span,*title,*artist,*status,*hint,*follow_button;
  lv_obj_t *rows[MUSIC_LYRICS_WINDOW],*labels[MUSIC_LYRICS_WINDOW];
  lv_obj_t *translations[MUSIC_LYRICS_WINDOW],*bilingual;
  lv_obj_t *play,*favorite,*elapsed,*duration,*progress,*volume;
  lv_obj_t *controls,*previous,*next,*volume_icon;
  lv_obj_t *mode;
  struct music_pv_ui pv;
  struct music_cover_view cover;
  struct music_lyrics_window window;
  unsigned track,total;
  unsigned row_index[MUSIC_LYRICS_WINDOW];
  bool row_active[MUSIC_LYRICS_WINDOW];
  int current;
  bool follow,updating,status_hidden,playing;
  bool palette_valid,palette_pv,palette_favorite;
  uint32_t palette_accent,palette_background,palette_surface,palette_secondary,palette_foreground;
};

static void music_full_text(lv_obj_t *label,const char *text)
{if(strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);}

/* Only the newly current line fades in; recycled slots never inherit a fade. */
static void music_full_lyric_opacity(void *label,int32_t opacity)
{lv_obj_set_style_text_opa(label,(lv_opa_t)opacity,0);}

static void music_full_draw(void)
{
  struct music_full_ui *f=g_music_ui.full;if(!f||f->updating)return;
  f->updating=true;
  int y=lv_obj_get_scroll_y(f->viewport)-MUSIC_FULL_PAD;
  unsigned first=y>MUSIC_FULL_PITCH?(unsigned)y/MUSIC_FULL_PITCH-1:0;
  glass_music_lyrics_read(first,&f->window);
  if(f->window.track_id!=f->track){f->updating=false;return;}
  for(unsigned i=0;i<MUSIC_LYRICS_WINDOW;i++){
    if(i>=f->window.count){
      lv_anim_delete(f->labels[i],music_full_lyric_opacity);
      f->row_active[i]=false;
      lv_obj_add_flag(f->rows[i],LV_OBJ_FLAG_HIDDEN);continue;
    }
    lv_obj_remove_flag(f->rows[i],LV_OBJ_FLAG_HIDDEN);
    unsigned index=f->window.first+i;
    bool active=(int)index==f->window.current;
    bool entering=active&&(!f->row_active[i]||f->row_index[i]!=index);
    bool animate=f->follow&&f->playing&&!g_glass.reduced_motion;
    if(entering||!active||!animate){
      lv_anim_delete(f->labels[i],music_full_lyric_opacity);
      music_full_lyric_opacity(f->labels[i],LV_OPA_COVER);
    }
    lv_obj_set_y(f->rows[i],MUSIC_FULL_PAD+index*MUSIC_FULL_PITCH);
    music_full_text(f->labels[i],f->window.lines[i][0]?f->window.lines[i]:"· · ·");
    const char *translation=g_glass.music_original_only?"":f->window.translations[i];
    lv_label_set_long_mode(f->labels[i],translation[0]?LV_LABEL_LONG_DOT:LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_height(f->labels[i],translation[0]?40:70,0);
    lv_obj_set_style_text_font(f->labels[i],zh_font(active?28:20),0);
    lv_obj_set_style_text_color(f->labels[i],lv_color_hex(active?g_music_ui.accent:
      g_desktop.light_theme?0x625b6e:0xbdb5c9),0);
    if(translation[0]){
      lv_obj_align(f->labels[i],LV_ALIGN_TOP_LEFT,0,3);
      music_full_text(f->translations[i],translation);
      lv_obj_set_style_text_color(f->translations[i],lv_color_hex(active?g_music_ui.primary:g_music_ui.secondary),0);
      lv_obj_remove_flag(f->translations[i],LV_OBJ_FLAG_HIDDEN);
    }else{
      lv_obj_align(f->labels[i],LV_ALIGN_LEFT_MID,0,0);
      lv_obj_add_flag(f->translations[i],LV_OBJ_FLAG_HIDDEN);
    }
    if(entering&&animate){
      lv_anim_t fade;lv_anim_init(&fade);
      lv_anim_set_var(&fade,f->labels[i]);
      lv_anim_set_exec_cb(&fade,music_full_lyric_opacity);
      lv_anim_set_values(&fade,LV_OPA_40,LV_OPA_COVER);
      lv_anim_set_duration(&fade,320);
      lv_anim_set_path_cb(&fade,lv_anim_path_ease_out);
      lv_anim_start(&fade);
    }
    f->row_index[i]=index;f->row_active[i]=active;
  }
  f->updating=false;
}

static void music_full_scroll(lv_event_t *event)
{
  struct music_full_ui *f=g_music_ui.full;if(!f)return;
  if(lv_event_get_code(event)==LV_EVENT_SCROLL_BEGIN){
    lv_indev_t *input=lv_indev_active();
    if(input&&lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED){
      f->follow=false;
      music_full_text(lv_obj_get_child(f->follow_button,0),"恢复跟随");
    }
  }
  if(lv_event_get_code(event)==LV_EVENT_SCROLL)music_full_draw();
}

static void music_full_follow(lv_event_t *event)
{
  LV_UNUSED(event);struct music_full_ui *f=g_music_ui.full;if(!f)return;
  f->follow=true;f->current=-2;
  struct music_now_playing now;glass_music_now_playing(&now);music_full_refresh(&now);
}

static void music_full_deleted(lv_event_t *event)
{
  struct music_full_ui *f=lv_event_get_user_data(event);
  if(g_music_ui.full==f)g_music_ui.full=NULL;
  if(f->pv.timer)lv_timer_delete(f->pv.timer);
  music_cover_reset(&f->cover);
  if(!f->status_hidden)glass_status_visibility(false);
  free(f);
}

static void music_full_close(lv_event_t *event)
{
  LV_UNUSED(event);
  if(g_music_ui.full)lv_obj_delete(g_music_ui.full->root);
}

static void music_full_palette(struct music_full_ui *f,const struct music_now_playing *now)
{
  bool pv=f->pv.visible;
  uint32_t accent=pv&&f->pv.valid?f->pv.accent:g_music_ui.accent;
  uint32_t background=pv&&f->pv.valid?f->pv.background:pv?(g_desktop.light_theme?0xf5f1e8:0x121518):g_music_ui.background;
  uint32_t surface=pv?lv_color_to_u32(lv_color_mix(lv_color_hex(f->pv.valid?f->pv.foreground:0x888888),lv_color_hex(background),24))&0xffffff:g_music_ui.surface;
  uint32_t secondary=pv&&f->pv.valid?f->pv.secondary:pv?(g_desktop.light_theme?0x74736f:0x999a97):g_music_ui.secondary;
  uint32_t foreground=pv&&f->pv.valid?f->pv.foreground:g_music_ui.primary;
  if(f->palette_valid&&f->palette_pv==pv&&f->palette_favorite==now->favorite&&
     f->palette_accent==accent&&f->palette_background==background&&f->palette_surface==surface&&
     f->palette_secondary==secondary&&f->palette_foreground==foreground)return;
  f->palette_valid=true;f->palette_pv=pv;f->palette_favorite=now->favorite;
  f->palette_accent=accent;f->palette_background=background;f->palette_surface=surface;
  f->palette_secondary=secondary;f->palette_foreground=foreground;
  lv_obj_set_style_bg_color(f->root,lv_color_hex(background),0);
  lv_obj_set_style_bg_color(f->controls,lv_color_hex(surface),0);
  lv_obj_set_style_border_color(f->controls,lv_color_hex(secondary),0);
  lv_obj_set_style_text_color(f->volume_icon,lv_color_hex(secondary),0);
  lv_obj_t *play=lv_obj_get_parent(f->play);
  for(unsigned i=0;i<lv_obj_get_child_count(f->root);i++){
    lv_obj_t *child=lv_obj_get_child(f->root,i);
    if(lv_obj_check_type(child,&lv_button_class)){
      lv_obj_set_style_bg_color(child,lv_color_hex(child==play?accent:surface),0);
      bool header=lv_obj_get_style_y(child,0)==16;
      bool footer=child==play||child==f->previous||child==f->next||child==lv_obj_get_parent(f->favorite);
      lv_obj_set_style_bg_opa(child,footer?(child==play?LV_OPA_20:LV_OPA_TRANSP):header?(child==f->bilingual?LV_OPA_10:LV_OPA_TRANSP):LV_OPA_COVER,0);
      lv_obj_t *label=lv_obj_get_child(child,0);
      if(label){
        uint32_t color=foreground;
        if(footer)color=child==play?accent:secondary;
        lv_obj_set_style_text_color(label,lv_color_hex(color),0);
      }
    }
  }
  lv_obj_set_style_bg_color(f->progress,lv_color_hex(secondary),LV_PART_MAIN);
  lv_obj_set_style_bg_opa(f->progress,LV_OPA_20,LV_PART_MAIN);
  lv_obj_set_style_bg_color(f->progress,lv_color_hex(accent),LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(f->volume,lv_color_hex(secondary),LV_PART_MAIN);
  lv_obj_set_style_bg_opa(f->volume,LV_OPA_20,LV_PART_MAIN);
  lv_obj_set_style_bg_color(f->volume,lv_color_hex(accent),LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(f->volume,lv_color_hex(accent),LV_PART_KNOB);
  lv_obj_set_style_text_color(f->favorite,lv_color_hex(now->favorite?accent:secondary),0);
  lv_obj_set_style_text_color(f->status,lv_color_hex(secondary),0);
  lv_obj_set_style_text_color(f->elapsed,lv_color_hex(secondary),0);
  lv_obj_set_style_text_color(f->duration,lv_color_hex(secondary),0);
}

static void music_full_refresh(const struct music_now_playing *now)
{
  struct music_full_ui *f=g_music_ui.full;if(!f)return;
  f->playing=now->state==MUSIC_PLAYING&&!now->pause_requested;
  if(f->track!=now->track_id)for(unsigned i=0;i<MUSIC_LYRICS_WINDOW;i++){
    lv_anim_delete(f->labels[i],music_full_lyric_opacity);
    f->row_active[i]=false;
    lv_obj_add_flag(f->rows[i],LV_OBJ_FLAG_HIDDEN);
  }
  music_cover_tick(&f->cover,now);
  music_full_text(f->title,now->name[0]?now->name:"还没有播放音乐");
  music_full_text(f->artist,now->artist[0]?now->artist:"播放歌曲后，在这里跟随歌词");
  const char *status=now->state==MUSIC_PLAYING?"正在播放":now->state==MUSIC_PAUSED?"已暂停":
    now->state==MUSIC_LOADING?"正在加载音乐":now->state==MUSIC_ERROR?"播放失败":
    now->state==MUSIC_STOPPED?"已停止":now->state==MUSIC_ENDED?"播放结束":"音乐 · 全屏歌词";
  music_full_text(f->status,status);
  music_full_text(f->play,(now->state==MUSIC_PLAYING||now->state==MUSIC_LOADING)&&!now->pause_requested?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
  char time[24];snprintf(time,sizeof(time),"%02u:%02u",now->position_ms/60000,now->position_ms/1000%60);
  music_full_text(f->elapsed,time);
  if(now->duration_ms)snprintf(time,sizeof(time),"%02u:%02u",now->duration_ms/60000,now->duration_ms/1000%60);
  else snprintf(time,sizeof(time),"--:--");
  music_full_text(f->duration,time);
  unsigned progress=now->duration_ms?(uint64_t)now->position_ms*1000/now->duration_ms:0;
  lv_bar_set_value(f->progress,progress>1000?1000:progress,LV_ANIM_OFF);
  if(!lv_obj_has_state(f->volume,LV_STATE_PRESSED))lv_slider_set_value(f->volume,now->volume,LV_ANIM_OFF);
  if(f->pv.visible){music_pv_refresh(&f->pv);music_full_palette(f,now);return;}
  music_full_palette(f,now);
  glass_music_lyrics_read(f->window.first,&f->window);
  if(f->window.track_id!=now->track_id)return;
  bool changed=f->track!=now->track_id||f->total!=f->window.total;
  if(f->track!=now->track_id){f->follow=true;f->current=-2;}
  f->track=now->track_id;f->total=f->window.total;
  if(changed){
    for(unsigned i=0;i<MUSIC_LYRICS_WINDOW;i++)lv_obj_add_flag(f->rows[i],LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(f->span,f->total*MUSIC_FULL_PITCH+MUSIC_FULL_PAD*2+1);
    lv_obj_update_layout(f->viewport);
  }
  if(!f->total){
    const char *hint=now->lyrics_state==MUSIC_LYRICS_LOADING?"正在获取歌词…":
      now->name[0]?"暂无歌词 · 仍可继续听歌":"选择一首喜欢的歌开始播放";
    music_full_text(f->hint,hint);lv_obj_remove_flag(f->hint,LV_OBJ_FLAG_HIDDEN);
  }else lv_obj_add_flag(f->hint,LV_OBJ_FLAG_HIDDEN);
  if(f->follow&&(changed||f->current!=f->window.current)){
    int target=f->window.current<0?0:f->window.current*MUSIC_FULL_PITCH;
    lv_obj_scroll_to_y(f->viewport,target,changed||g_glass.reduced_motion?LV_ANIM_OFF:LV_ANIM_ON);
  }
  f->current=f->window.current;
  music_full_text(lv_obj_get_child(f->follow_button,0),f->follow?"自动跟随":"恢复跟随");
  music_full_draw();
}

static void music_full_mode(lv_event_t *event)
{
  LV_UNUSED(event);struct music_full_ui *f=g_music_ui.full;if(!f)return;
  f->pv.visible=!f->pv.visible;f->pv.valid=false;
  lv_obj_t *ordinary[]={f->cover.host,f->title,f->artist,f->viewport,f->follow_button};
  for(unsigned i=0;i<sizeof(ordinary)/sizeof(ordinary[0]);i++){
    if(f->pv.visible)lv_obj_add_flag(ordinary[i],LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(ordinary[i],LV_OBJ_FLAG_HIDDEN);
  }
  for(unsigned i=0;i<MUSIC_LYRICS_WINDOW;i++){
    lv_anim_delete(f->labels[i],music_full_lyric_opacity);f->row_active[i]=false;
  }
  if(f->pv.visible){
    lv_obj_remove_flag(f->pv.style_button,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(f->pv.root,LV_OBJ_FLAG_HIDDEN);lv_timer_resume(f->pv.timer);
  }else{
    lv_obj_add_flag(f->pv.style_button,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(f->pv.root,LV_OBJ_FLAG_HIDDEN);lv_timer_pause(f->pv.timer);
    f->follow=true;f->current=-2;
  }
  music_full_text(lv_obj_get_child(f->mode,0),f->pv.visible?"普通歌词":"文字 PV");
  struct music_now_playing now;glass_music_now_playing(&now);music_full_refresh(&now);
}

static void music_full_bilingual(lv_event_t *event)
{
  LV_UNUSED(event);struct music_full_ui *f=g_music_ui.full;if(!f)return;
  g_glass.music_original_only=!g_glass.music_original_only;f->pv.valid=false;f->palette_valid=false;
  music_full_text(lv_obj_get_child(f->bilingual,0),g_glass.music_original_only?"原文":"双语");
  struct music_now_playing now;glass_music_now_playing(&now);music_full_refresh(&now);
}

static void music_full_open(lv_event_t *event)
{
  LV_UNUSED(event);if(!g_music_ui.root||g_music_ui.full)return;
  struct music_full_ui *f=calloc(1,sizeof(*f));if(!f){show_toast("内存不足，稍后再试");return;}
  g_music_ui.full=f;f->follow=true;f->track=UINT32_MAX;f->current=-2;
  f->status_hidden=g_desktop.statusbar&&lv_obj_has_flag(g_desktop.statusbar,LV_OBJ_FLAG_HIDDEN);
  f->root=glass_box(lv_layer_top(),0,0,1024,600,g_music_ui.background,0);
  lv_obj_remove_flag(f->root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(f->root,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(f->root,music_full_deleted,LV_EVENT_DELETE,f);
  glass_status_visibility(true);
  music_ui_button(f->root,"返回音乐",24,16,136,44,g_music_ui.surface,music_full_close,NULL,false);
  f->status=music_ui_text(f->root,"音乐 · 全屏歌词",194,27,274,16,g_music_ui.secondary);
  f->bilingual=music_ui_button(f->root,g_glass.music_original_only?"原文":"双语",494,16,132,44,g_music_ui.surface,music_full_bilingual,NULL,false);
  f->mode=music_ui_button(f->root,"文字 PV",650,16,162,44,g_music_ui.surface,music_full_mode,NULL,false);
  f->follow_button=music_ui_button(f->root,"自动跟随",832,16,168,44,g_music_ui.surface,music_full_follow,NULL,false);
  f->cover.host=music_ui_sleeve(f->root,48,100,288,20);f->cover.owner=GLASS_COVER_LOCK;
  f->title=music_ui_text(f->root,"",48,404,304,28,g_music_ui.primary);
  f->artist=music_ui_text(f->root,"",48,448,304,16,g_music_ui.secondary);
  f->viewport=glass_box(f->root,386,86,590,MUSIC_FULL_HEIGHT,g_music_ui.background,0);
  lv_obj_add_flag(f->viewport,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(f->viewport,LV_DIR_VER);
  lv_obj_set_scrollbar_mode(f->viewport,LV_SCROLLBAR_MODE_AUTO);
  f->span=glass_box(f->viewport,0,0,1,1,g_music_ui.background,0);
  lv_obj_set_style_bg_opa(f->span,LV_OPA_TRANSP,0);
  lv_obj_remove_flag(f->span,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  for(unsigned i=0;i<MUSIC_LYRICS_WINDOW;i++){
    f->rows[i]=glass_box(f->viewport,22,0,538,MUSIC_FULL_PITCH,g_music_ui.background,0);
    lv_obj_remove_flag(f->rows[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    f->labels[i]=music_ui_text(f->rows[i],"",0,0,538,20,g_music_ui.secondary);
    lv_obj_set_height(f->labels[i],LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(f->labels[i],70,0);
    lv_label_set_long_mode(f->labels[i],LV_LABEL_LONG_WRAP);
    f->translations[i]=music_ui_text(f->rows[i],"",0,47,538,16,g_music_ui.secondary);
    lv_obj_set_height(f->translations[i],24);lv_label_set_long_mode(f->translations[i],LV_LABEL_LONG_DOT);
    lv_obj_add_flag(f->translations[i],LV_OBJ_FLAG_HIDDEN);
  }
  f->hint=music_ui_text(f->viewport,"",22,MUSIC_FULL_PAD,538,20,g_music_ui.secondary);
  lv_obj_add_flag(f->hint,LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_event_cb(f->viewport,music_full_scroll,LV_EVENT_SCROLL,NULL);
  lv_obj_add_event_cb(f->viewport,music_full_scroll,LV_EVENT_SCROLL_BEGIN,NULL);
  /* One quiet control island. All transport targets remain at least 44 px;
   * the slender volume track gets its touch area independently of its ink. */
  f->controls=glass_box(f->root,136,488,752,64,g_music_ui.surface,32);
  lv_obj_set_style_bg_opa(f->controls,LV_OPA_70,0);
  lv_obj_set_style_border_width(f->controls,1,0);lv_obj_set_style_border_opa(f->controls,LV_OPA_10,0);
  lv_obj_remove_flag(f->controls,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  f->elapsed=music_ui_text(f->root,"00:00",212,511,48,12,g_music_ui.secondary);
  f->duration=music_ui_text(f->root,"--:--",380,511,48,12,g_music_ui.secondary);
  f->progress=lv_bar_create(f->root);lv_obj_set_pos(f->progress,264,519);lv_obj_set_size(f->progress,108,3);
  lv_bar_set_range(f->progress,0,1000);
  lv_obj_set_style_bg_color(f->progress,lv_color_hex(g_music_ui.surface),LV_PART_MAIN);
  lv_obj_set_style_bg_color(f->progress,lv_color_hex(g_music_ui.accent),LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(f->progress,LV_OPA_COVER,LV_PART_MAIN);
  lv_obj_set_style_bg_opa(f->progress,LV_OPA_COVER,LV_PART_INDICATOR);
  lv_obj_set_style_radius(f->progress,LV_RADIUS_CIRCLE,LV_PART_MAIN);
  lv_obj_set_style_radius(f->progress,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);
  lv_obj_remove_flag(f->progress,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *favorite=music_ui_button(f->root,GLASS_ICON_HEART,152,498,44,44,g_music_ui.surface,music_ui_favorite,NULL,true);
  f->favorite=lv_obj_get_child(favorite,0);
  f->previous=music_ui_button(f->root,LV_SYMBOL_PREV,432,498,44,44,g_music_ui.surface,music_ui_skip,(void *)-1,true);
  lv_obj_t *play=music_ui_button(f->root,LV_SYMBOL_PLAY,488,496,48,48,g_music_ui.accent,music_ui_toggle,NULL,true);
  f->play=lv_obj_get_child(play,0);lv_obj_set_style_text_color(f->play,lv_color_white(),0);
  f->next=music_ui_button(f->root,LV_SYMBOL_NEXT,544,498,44,44,g_music_ui.surface,music_ui_skip,(void *)1,true);
  f->volume_icon=music_ui_text(f->root,LV_SYMBOL_VOLUME_MAX,636,510,28,16,g_music_ui.secondary);
  lv_obj_set_style_text_font(f->volume_icon,glass_icon_font(16),0);
  f->volume=lv_slider_create(f->root);lv_obj_set_pos(f->volume,676,519);lv_obj_set_size(f->volume,148,4);
  lv_obj_set_ext_click_area(f->volume,20);
  lv_obj_set_style_pad_all(f->volume,3,LV_PART_KNOB);
  lv_obj_set_style_bg_opa(f->volume,LV_OPA_TRANSP,LV_PART_KNOB);
  lv_obj_set_style_bg_opa(f->volume,LV_OPA_COVER,LV_PART_KNOB|LV_STATE_PRESSED);
  lv_slider_set_range(f->volume,0,100);
  lv_obj_set_style_bg_color(f->volume,lv_color_hex(g_music_ui.surface),LV_PART_MAIN);
  lv_obj_set_style_bg_color(f->volume,lv_color_hex(g_music_ui.accent),LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(f->volume,lv_color_hex(g_music_ui.accent),LV_PART_KNOB);
  lv_obj_add_event_cb(f->volume,music_ui_volume,LV_EVENT_VALUE_CHANGED,NULL);
  music_pv_create(&f->pv,f->root);
  struct music_now_playing now;glass_music_now_playing(&now);music_full_refresh(&now);
}
