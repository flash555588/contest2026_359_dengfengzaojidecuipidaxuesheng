/* SPDX-License-Identifier: Apache-2.0 */
/* Native lyric-PV study inspired by JIZURA's layout/enter/hold/exit model.
 * Independent LVGL implementation; no Canvas2D, fonts or JS fetched at runtime.
 * Animation time always comes from decoded audio position, including pauses. */
#define MUSIC_PV_PARTS 3
#define MUSIC_PV_LAYOUTS 8
#include "glass_music_pv_assets.h"
struct music_pv_ui {
  struct music_pv_sd *sd;lv_obj_t *sd_image;
  unsigned sd_track,sd_style;bool sd_failed,sd_present,sd_layout;
  lv_obj_t *root,*title,*counter,*hint,*next,*rule,*marker,*shuffle;
  lv_obj_t *plate,*echo,*sweep,*style_button;
  lv_obj_t *groups[MUSIC_PV_PARTS],*text[MUSIC_PV_PARTS][3];
  lv_timer_t *timer;
  struct music_lyrics_window window;
  char source[MUSIC_LYRIC_TEXT_MAX],parts[MUSIC_PV_PARTS][MUSIC_LYRIC_TEXT_MAX];
  unsigned track,variation,layout,count,start,end,last_position,style_index;
  struct music_pv_assets assets;
  int x[MUSIC_PV_PARTS],y[MUSIC_PV_PARTS],width[MUSIC_PV_PARTS];
  unsigned glyphs[MUSIC_PV_PARTS],motion;
  uint32_t background,accent,foreground,secondary;
  int current;
  bool visible,valid,last_reduced;
};
#include "glass_music_pv_sd.inc"

static void music_pv_text(lv_obj_t *label,const char *text)
{if(strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);}

static const lv_font_t *music_pv_font(unsigned size)
{
  if(size==72&&g_glass.font72)return g_glass.font72;
  if(size>=48&&g_glass.font48)return g_glass.font48;
  return zh_font(size>=28?28:20);
}

/* The service supplies validated UTF-8. Split only at code-point boundaries. */
static bool music_pv_punctuation(const char *s)
{
  static const char *const marks[]={"，","。","、","！","？","；","：","）","】","》","”"};
  if(*s&&strchr(",.!?;:)]",*s))return true;
  for(unsigned i=0;i<sizeof(marks)/sizeof(marks[0]);i++)if(!strncmp(s,marks[i],3))return true;
  return false;
}

static void music_pv_split(struct music_pv_ui *p,const char *text)
{
  unsigned glyphs=0;
  for(const unsigned char *s=(const unsigned char *)text;*s;s++)if((*s&0xc0)!=0x80)glyphs++;
  p->count=glyphs>24?3:glyphs>10?2:1;
  const char *s=text;
  for(unsigned i=0;i<p->count;i++){
    unsigned take=(glyphs+p->count-i-1)/(p->count-i),used=0;
    const char *begin=s;
    while(*s&&used<take){s++;while(((unsigned char)*s&0xc0)==0x80)s++;used++;}
    /* Closing punctuation belongs to the preceding segment. */
    while(*s&&music_pv_punctuation(s)){s++;while(((unsigned char)*s&0xc0)==0x80)s++;used++;}
    size_t bytes=(size_t)(s-begin);
    memcpy(p->parts[i],begin,bytes);p->parts[i][bytes]=0;glyphs-=used;
    if(!*s){p->count=i+1;break;}
  }
}

static unsigned music_pv_glyphs(const char *text)
{
  unsigned count=0;
  for(const unsigned char *s=(const unsigned char *)text;*s;s++)if((*s&0xc0)!=0x80)count++;
  return count;
}

static const char *music_pv_advance(const char *s,unsigned count)
{
  while(*s&&count--){s++;while(((unsigned char)*s&0xc0)==0x80)s++;}
  return s;
}

/* Short CJK phrases become actual columns; long/Latin phrases stay readable. */
static bool music_pv_vertical_ok(const char *text)
{
  unsigned count=music_pv_glyphs(text);if(count<3||count>15)return false;
  for(const unsigned char *s=(const unsigned char *)text;*s;s++)
    if((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9'))return false;
  return true;
}

static void music_pv_columns(struct music_pv_ui *p,const char *text)
{
  unsigned left=music_pv_glyphs(text);p->count=left>5?3:2;
  const char *s=text;
  for(unsigned i=0;i<p->count;i++){
    unsigned take=(left+p->count-i-1)/(p->count-i),used=0;
    char *dest=p->parts[i];
    while(*s&&used<take){
      const char *next=music_pv_advance(s,1);size_t bytes=(size_t)(next-s);
      memcpy(dest,s,bytes);dest+=bytes;s=next;used++;
      if(used<take)*dest++='\n';
    }
    *dest=0;left-=used;
  }
}

static void music_pv_emphasis(struct music_pv_ui *p,const char *text)
{
  unsigned glyphs=music_pv_glyphs(text);
  if(glyphs<6||glyphs>24){music_pv_split(p,text);return;}
  /* Visual emphasis of the final phrase, without pretending to infer meaning. */
  const char *end=music_pv_advance(text,glyphs-4);
  /* Prefer a nearby separator to splitting a phrase across a function word. */
  unsigned best=3;
  for(unsigned n=2;n+2<glyphs;n++){
    const char *s=music_pv_advance(text,n-1),*candidate=music_pv_advance(s,1);
    bool boundary=music_pv_punctuation(s)||*s==' '||!strncmp(s,"给",3)||
      !strncmp(s,"向",3)||!strncmp(s,"与",3)||!strncmp(s,"和",3);
    unsigned distance=n>glyphs-4?n-(glyphs-4):(glyphs-4)-n;
    if(boundary&&distance<best){end=candidate;best=distance;}
  }
  while(*end&&music_pv_punctuation(end))end=music_pv_advance(end,1);
  if(!*end){music_pv_split(p,text);return;}
  size_t bytes=(size_t)(end-text);memcpy(p->parts[0],text,bytes);p->parts[0][bytes]=0;
  snprintf(p->parts[1],sizeof(p->parts[1]),"%s",end);p->count=2;
}

static unsigned music_pv_base_layout(int current,const char *text)
{
  /* Quiet shots anchor each phrase; emphasis is punctuation, not a slideshow. */
  static const unsigned sequence[16]={0,1,0,5,0,2,3,0,1,0,5,0,4,0,7,0};
  unsigned layout=sequence[(unsigned)current%16];
  if(strchr(text,'!')||strstr(text,"！"))layout=5;
  if(layout==4&&!music_pv_vertical_ok(text))layout=0;
  return layout;
}

static void music_pv_plan(struct music_pv_ui *p,const char *text,unsigned track,int current)
{
  static const uint32_t light_accent[3]={0xa44853,0x426b64,0x876632};
  static const uint32_t dark_accent[3]={0xe2a2a8,0x9bc5b6,0xd9bd88};
  p->layout=(music_pv_base_layout(current,text)+p->variation)%MUSIC_PV_LAYOUTS;
  if(p->layout==4&&!music_pv_vertical_ok(text))p->layout=6;
  p->motion=((unsigned)current/4+p->variation/MUSIC_PV_LAYOUTS)%4;
  music_pv_split(p,text);
  if(p->layout==4)music_pv_columns(p,text);
  if(p->layout==5)music_pv_emphasis(p,text);
  p->foreground=g_desktop.light_theme?0x292b2b:0xf1ece3;
  p->secondary=g_desktop.light_theme?0x74736f:0x999a97;
  p->background=g_desktop.light_theme?0xf5f1e8:0x121518;
  p->accent=g_desktop.light_theme?light_accent[track%3]:dark_accent[track%3];
  if(p->assets.count){
    const struct music_pv_style *style=&p->assets.styles[p->style_index%p->assets.count];
    const struct music_pv_colors *colors=g_desktop.light_theme?&style->light:&style->dark;
    p->foreground=colors->foreground;p->secondary=colors->secondary;p->background=colors->background;p->accent=colors->accent;
    music_pv_text(lv_obj_get_child(p->style_button,0),style->name);
  }
  uint32_t foreground=p->foreground,accent=p->accent;
  lv_obj_set_style_text_color(p->title,lv_color_hex(p->secondary),0);
  lv_obj_set_style_text_color(p->counter,lv_color_hex(p->secondary),0);
  lv_obj_set_style_text_color(p->next,lv_color_hex(p->secondary),0);
  lv_obj_set_style_text_color(lv_obj_get_child(p->shuffle,0),lv_color_hex(p->secondary),0);
  lv_obj_set_style_text_color(lv_obj_get_child(p->style_button,0),lv_color_hex(p->secondary),0);
  lv_obj_set_style_bg_color(p->rule,lv_color_hex(p->secondary),0);
  lv_obj_set_style_bg_color(p->marker,lv_color_hex(accent),0);
  lv_obj_set_pos(p->marker,p->layout==2?932:24,104);
  lv_obj_set_size(p->marker,p->layout==3?216:2,p->layout==3?1:24);
  if(p->layout==3)lv_obj_set_pos(p->marker,380,304);
  if(p->layout==4){lv_obj_set_pos(p->marker,916,94);lv_obj_set_size(p->marker,1,216);}
  if(p->layout==5||p->layout==6){lv_obj_set_pos(p->marker,48,80);lv_obj_set_size(p->marker,40,1);}
  if(p->layout==7){lv_obj_set_pos(p->marker,48,94);lv_obj_set_size(p->marker,2,24);}
  lv_obj_add_flag(p->plate,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(p->echo,LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_color(p->plate,lv_color_hex(accent),0);
  lv_obj_set_style_bg_opa(p->plate,p->layout==6?LV_OPA_TRANSP:LV_OPA_10,0);
  lv_obj_set_style_border_color(p->plate,lv_color_hex(accent),0);
  lv_obj_set_style_border_width(p->plate,p->layout==6?1:0,0);
  lv_obj_set_style_border_opa(p->plate,LV_OPA_20,0);
  if(p->layout==5||p->layout==6){
    lv_obj_remove_flag(p->plate,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(p->plate,48,p->layout==5?190:88);
    lv_obj_set_size(p->plate,880,p->layout==5?124:240);
  }
  lv_obj_set_style_bg_color(p->sweep,lv_color_hex(accent),0);
  unsigned vertical_font=48;
  if(p->layout==4)for(unsigned i=0;i<p->count;i++){
    lv_point_t size;lv_text_get_size(&size,p->parts[i],music_pv_font(48),0,-12,200,LV_TEXT_FLAG_NONE);
    if(size.y>248)vertical_font=28;
  }
  for(unsigned i=0;i<MUSIC_PV_PARTS;i++){
    if(i>=p->count){lv_obj_add_flag(p->groups[i],LV_OBJ_FLAG_HIDDEN);continue;}
    lv_obj_remove_flag(p->groups[i],LV_OBJ_FLAG_HIDDEN);
    unsigned height=p->layout==4?248:p->count<3?108:84;
    int width=p->layout==4?120:808;
    p->x[i]=p->layout==4?588-(int)i*144:p->layout==1?48+(int)i*30:84;
    p->y[i]=p->layout==4?82:p->count==1?140:p->count==2?88+(int)i*120:76+(int)i*84;
    if(p->layout==5&&p->count==2)p->y[i]=i?198:88;
    if(p->layout==6&&p->count==3)p->y[i]=80+(int)i*80;
    p->width[i]=width;p->glyphs[i]=music_pv_glyphs(p->parts[i]);
    lv_obj_set_size(p->groups[i],width,height);
    const lv_font_t *font=music_pv_font(p->layout==4?vertical_font:p->layout==5&&i+1<p->count?28:p->layout==3?48:72);
    int line_space=p->layout==4?-12:0;
    lv_point_t measure;
    lv_text_get_size(&measure,p->parts[i],font,0,line_space,width,LV_TEXT_FLAG_NONE);
    if(measure.y>(int)height){font=music_pv_font(48);lv_text_get_size(&measure,p->parts[i],font,0,line_space,width,LV_TEXT_FLAG_NONE);}
    if(measure.y>(int)height){font=music_pv_font(28);lv_text_get_size(&measure,p->parts[i],font,0,line_space,width,LV_TEXT_FLAG_NONE);}
    if(measure.y>(int)height)font=music_pv_font(20);
    lv_text_get_size(&measure,p->parts[i],font,0,line_space,width,LV_TEXT_FLAG_NONE);
    for(unsigned layer=0;layer<3;layer++){
      music_pv_text(p->text[i][layer],p->parts[i]);
      lv_obj_set_width(p->text[i][layer],width);
      lv_obj_set_height(p->text[i][layer],measure.y>(int)height?(int)height:measure.y);
      lv_obj_set_style_text_font(p->text[i][layer],font,0);
      lv_obj_set_style_text_line_space(p->text[i][layer],line_space,0);
      lv_obj_set_style_text_color(p->text[i][layer],lv_color_hex(layer==2?
        (p->layout==3||(p->layout==5&&i+1==p->count)?accent:p->layout==5?p->secondary:foreground):accent),0);
      lv_obj_set_style_text_opa(p->text[i][layer],layer==2?LV_OPA_COVER:LV_OPA_TRANSP,0);
      lv_obj_set_style_text_align(p->text[i][layer],p->layout==1||p->layout==7||p->layout==5?LV_TEXT_ALIGN_LEFT:
        p->layout==2?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_CENTER,0);
      lv_obj_align(p->text[i][layer],LV_ALIGN_LEFT_MID,layer==0?-1:layer==1?1:0,0);
    }
  }
  if(p->layout==4){
    const lv_font_t *shared=lv_obj_get_style_text_font(p->text[0][2],0);
    for(unsigned i=1;i<p->count;i++){
      const lv_font_t *candidate=lv_obj_get_style_text_font(p->text[i][2],0);
      if(candidate->line_height<shared->line_height)shared=candidate;
    }
    for(unsigned i=0;i<p->count;i++)for(unsigned layer=0;layer<3;layer++){
      lv_point_t size;lv_text_get_size(&size,p->parts[i],shared,0,-12,p->width[i],LV_TEXT_FLAG_NONE);
      lv_obj_set_style_text_font(p->text[i][layer],shared,0);
      lv_obj_set_height(p->text[i][layer],size.y);
      lv_obj_align(p->text[i][layer],LV_ALIGN_TOP_MID,layer==0?-1:layer==1?1:0,16);
    }
  }
  if(p->layout==5){
    unsigned last=p->count-1;lv_obj_t *label=p->text[last][2];
    int height=lv_obj_get_style_height(label,0);
    lv_point_t ink;lv_text_get_size(&ink,p->parts[last],lv_obj_get_style_text_font(label,0),0,0,p->width[last],LV_TEXT_FLAG_NONE);
    lv_obj_set_pos(p->plate,p->x[last]-12,p->y[last]+(108-height)/2+height-10);
    lv_obj_set_size(p->plate,ink.x+24,12);
  }
  snprintf(p->source,sizeof(p->source),"%s",text);
}

static unsigned music_pv_ease(unsigned position,unsigned delay,unsigned duration)
{
  if(position<=delay)return 0;
  unsigned t=position-delay;if(t>=duration)return 1000;
  unsigned remaining=1000-t*1000/duration;
  return 1000-(uint64_t)remaining*remaining*remaining/1000000;
}

static void music_pv_refresh(struct music_pv_ui *p)
{
  if(!p->visible)return;
  struct music_now_playing now;glass_music_now_playing(&now);
  bool sd=music_pv_sd_refresh(p,&now);
  if(sd||p->sd){
    bool changed=!p->sd_layout||!p->valid;
    if(!p->sd_layout){
      for(unsigned i=0;i<lv_obj_get_child_count(p->root);i++){
        lv_obj_t *child=lv_obj_get_child(p->root,i);
        if(child!=p->sd_image&&child!=p->style_button)lv_obj_add_flag(child,LV_OBJ_FLAG_HIDDEN);
      }
    }
    p->sd_layout=true;p->sd_present=sd;p->valid=true;
    if(changed&&p->assets.count){
      const struct music_pv_style *style=&p->assets.styles[p->style_index%p->assets.count];
      const struct music_pv_colors *c=g_desktop.light_theme?&style->light:&style->dark;
      p->background=c->background;p->foreground=c->foreground;p->accent=c->accent;p->secondary=c->secondary;
      music_pv_text(lv_obj_get_child(p->style_button,0),style->name);
      lv_obj_set_style_text_color(lv_obj_get_child(p->style_button,0),lv_color_hex(c->foreground),0);
    }
    return;
  }
  if(p->sd_layout){
    p->sd_layout=false;p->sd_present=false;p->valid=false;
    lv_obj_t *restore[]={p->title,p->counter,p->next,p->rule};
    for(unsigned i=0;i<4;i++)lv_obj_remove_flag(restore[i],LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(p->shuffle,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(p->shuffle,336);
  }
  music_pv_text(p->title,now.name[0]?now.name:"音乐 · 文字 PV");
  glass_music_lyrics_read(p->current<0?0:(unsigned)p->current,&p->window);
  if(p->window.track_id==now.track_id&&p->window.current>=0&&
     ((unsigned)p->window.current<p->window.first||(unsigned)p->window.current>=p->window.first+p->window.count))
    glass_music_lyrics_read((unsigned)p->window.current,&p->window);
  int index=p->window.current;
  bool ready=p->window.track_id==now.track_id&&p->window.state==MUSIC_LYRICS_READY&&index>=0&&
    (unsigned)index>=p->window.first&&(unsigned)index<p->window.first+p->window.count&&
    (now.state==MUSIC_PLAYING||now.state==MUSIC_PAUSED);
  if(!ready){
    const char *hint=now.state==MUSIC_ERROR?"播放失败":now.state==MUSIC_STOPPED?"已停止":
      now.state==MUSIC_ENDED?"播放结束":now.state==MUSIC_IDLE?"选择一首歌开始":
      now.state==MUSIC_LOADING?"正在加载音乐…":now.lyrics_state==MUSIC_LYRICS_LOADING?"正在获取歌词…":
      now.lyrics_state==MUSIC_LYRICS_READY?"前奏 · 等待歌词":"暂无歌词 · 仍可继续听歌";
    music_pv_text(p->hint,hint);lv_obj_remove_flag(p->hint,LV_OBJ_FLAG_HIDDEN);
    for(unsigned i=0;i<MUSIC_PV_PARTS;i++)lv_obj_add_flag(p->groups[i],LV_OBJ_FLAG_HIDDEN);
    music_pv_text(p->counter,"");music_pv_text(p->next,"");
    lv_obj_add_flag(p->plate,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(p->echo,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(p->sweep,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(p->marker,LV_OBJ_FLAG_HIDDEN);p->valid=false;return;
  }
  unsigned slot=(unsigned)index-p->window.first;
  const char *text=p->window.lines[slot][0]?p->window.lines[slot]:"· · ·";
  if(!p->valid||p->track!=now.track_id||p->current!=index||strcmp(p->source,text)){
    music_pv_plan(p,text,now.track_id,index);
    p->track=now.track_id;p->current=index;p->start=p->window.start_ms[slot];p->end=p->window.end_ms[slot];
    p->valid=true;p->last_position=UINT32_MAX;
    char counter[32];snprintf(counter,sizeof(counter),"%03u / %03u",(unsigned)index+1,p->window.total);
    music_pv_text(p->counter,counter);
    music_pv_text(p->next,slot+1<p->window.count?p->window.lines[slot+1]:"");
    if(now.lyrics[0][0]&&(p->layout==0||p->layout==1||p->layout==2)){
      music_pv_text(p->echo,now.lyrics[0]);lv_obj_remove_flag(p->echo,LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_text_color(p->echo,lv_color_hex(p->secondary),0);
    }
    lv_obj_add_flag(p->hint,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(p->marker,LV_OBJ_FLAG_HIDDEN);
    if(p->layout==0||p->layout==2||p->layout==5||p->layout==6)lv_obj_add_flag(p->marker,LV_OBJ_FLAG_HIDDEN);
    if(p->layout==3||p->layout==7)lv_obj_remove_flag(p->sweep,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(p->sweep,LV_OBJ_FLAG_HIDDEN);
  }
  p->start=p->window.start_ms[slot];p->end=p->window.end_ms[slot];
  if(p->last_position==now.position_ms&&p->last_reduced==g_glass.reduced_motion)return;
  p->last_position=now.position_ms;p->last_reduced=g_glass.reduced_motion;
  unsigned local=now.position_ms>p->start?now.position_ms-p->start:0;
  unsigned span=p->end>p->start?p->end-p->start:0;
  /* Short cuts settle faster. Unknown final durations have no fabricated exit. */
  unsigned entrance=span&&span<1800?span/3:620;if(!entrance)entrance=1;
  unsigned cut_opacity=255;
  if(!g_glass.reduced_motion&&span>800&&local>span-160)
    cut_opacity=local>=span?0:255*(span-local)/160;
  unsigned progress=span?(unsigned)((uint64_t)(local>span?span:local)*1000/span):1000;
  if(g_glass.reduced_motion)progress=1000;
  lv_obj_set_width(p->sweep,1+progress*879/1000);
  lv_obj_set_style_opa(p->sweep,cut_opacity*30/100,0);
  lv_obj_set_style_opa(p->plate,g_glass.reduced_motion?255:music_pv_ease(local,0,entrance)*cut_opacity/1000,0);
  lv_obj_set_style_opa(p->marker,cut_opacity,0);
  for(unsigned i=0;i<p->count;i++){
    unsigned delay=span&&span<900?i*span/18:i*80;
    unsigned e=g_glass.reduced_motion?1000:music_pv_ease(local,delay,entrance);
    unsigned opacity=e*cut_opacity/1000;
    int x=p->x[i],y=p->y[i],offset=(1000-e)*24/1000;
    if(p->layout==4)y-=offset;
    else if(p->layout==5&&i+1==p->count)x+=offset;
    else if(p->motion==1)x+=(i%2?1:-1)*offset;
    else if(p->motion==2)y-=offset;
    else if(p->motion!=3)y+=offset;
    unsigned width=p->width[i];
    if(p->motion==3&&p->layout!=4&&p->layout!=7)width=1+(p->width[i]-1)*e/1000;
    lv_obj_set_width(p->groups[i],width);
    lv_obj_set_pos(p->groups[i],x,y);
    lv_obj_set_style_opa(p->groups[i],opacity,0);
    /* The settled lyric is sharp. A restrained same-hue trail exists only on entry. */
    for(unsigned layer=0;layer<2;layer++)lv_obj_set_style_text_opa(p->text[i][layer],
      g_glass.reduced_motion||p->layout==4||p->layout==6||p->layout==7?0:(1000-e)*18/1000,0);
    if(p->layout==7){
      unsigned duration=span&&span<2400?span/2:1200;if(!duration)duration=1;
      unsigned typed=local>delay?local-delay:0;
      unsigned count=g_glass.reduced_motion||typed>=duration?p->glyphs[i]:
        (uint64_t)p->glyphs[i]*typed/duration;
      const char *end=music_pv_advance(p->parts[i],count);size_t bytes=(size_t)(end-p->parts[i]);
      char visible[MUSIC_LYRIC_TEXT_MAX];memcpy(visible,p->parts[i],bytes);visible[bytes]=0;
      for(unsigned layer=0;layer<3;layer++)music_pv_text(p->text[i][layer],visible);
    }
  }
}

static void music_pv_tick(lv_timer_t *timer)
{music_pv_refresh(lv_timer_get_user_data(timer));}

static void music_pv_shuffle(lv_event_t *event)
{
  struct music_pv_ui *p=lv_event_get_user_data(event);
  p->variation++;p->valid=false;music_pv_refresh(p);
}

static void music_pv_style_next(lv_event_t *event)
{
  struct music_pv_ui *p=lv_event_get_user_data(event);
  if(!p->assets.count)return;
  p->style_index=(p->style_index+1)%p->assets.count;p->valid=false;
  music_ui_refresh(NULL);
}

static void music_pv_create(struct music_pv_ui *p,lv_obj_t *parent)
{
  p->current=-1;
  music_pv_assets_load(&p->assets,MUSIC_PV_STYLE_FILE);
  p->root=glass_box(parent,24,78,976,394,g_music_ui.background,0);
  lv_obj_set_style_bg_opa(p->root,LV_OPA_TRANSP,0);
  lv_obj_remove_flag(p->root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
  uint32_t sub=g_desktop.light_theme?0x6b6074:0xc5bacd;
  p->title=music_ui_text(p->root,"",44,20,680,16,sub);
  p->counter=music_ui_text(p->root,"",806,20,140,16,sub);
  p->rule=glass_box(p->root,44,60,888,1,sub,0);lv_obj_set_style_bg_opa(p->rule,LV_OPA_10,0);
  p->marker=glass_box(p->root,24,104,4,162,g_music_ui.accent,2);
  p->plate=glass_box(p->root,48,88,880,240,g_music_ui.accent,16);
  lv_obj_remove_flag(p->plate,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  p->echo=music_ui_text(p->root,"",84,84,808,20,g_music_ui.secondary);
  lv_obj_set_height(p->echo,32);lv_label_set_long_mode(p->echo,LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(p->echo,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_opa(p->echo,LV_OPA_50,0);
  p->sweep=glass_box(p->root,48,330,1,1,g_music_ui.accent,0);
  for(unsigned i=0;i<MUSIC_PV_PARTS;i++){
    p->groups[i]=glass_box(p->root,84,100+i*84,808,84,g_music_ui.background,0);
    lv_obj_set_style_bg_opa(p->groups[i],LV_OPA_TRANSP,0);
    lv_obj_remove_flag(p->groups[i],LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    for(unsigned layer=0;layer<3;layer++){
      p->text[i][layer]=music_ui_text(p->groups[i],"",0,0,808,28,g_music_ui.primary);
      lv_obj_set_height(p->text[i][layer],LV_SIZE_CONTENT);
      lv_label_set_long_mode(p->text[i][layer],LV_LABEL_LONG_WRAP);
    }
  }
  p->hint=music_ui_text(p->root,"",44,178,888,28,sub);
  lv_obj_set_style_text_align(p->hint,LV_TEXT_ALIGN_CENTER,0);
  p->next=music_ui_text(p->root,"",44,352,600,16,sub);
  p->style_button=music_ui_button(parent,"SD 风格",832,16,168,44,g_music_ui.surface,music_pv_style_next,p,false);
  lv_obj_set_style_bg_opa(p->style_button,LV_OPA_TRANSP,0);
  lv_obj_add_flag(p->style_button,LV_OBJ_FLAG_HIDDEN);
  p->shuffle=music_ui_button(p->root,"构图",842,336,110,44,g_music_ui.surface,music_pv_shuffle,p,false);
  lv_obj_set_style_bg_opa(p->shuffle,LV_OPA_TRANSP,0);
  p->sd_image=lv_image_create(p->root);lv_obj_set_pos(p->sd_image,(976-JPV_WIDTH)/2,0);
  lv_obj_remove_flag(p->sd_image,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_flag(p->sd_image,LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(p->sd_image);
  p->timer=lv_timer_create(music_pv_tick,40,p);lv_timer_pause(p->timer);
  lv_obj_add_flag(p->root,LV_OBJ_FLAG_HIDDEN);
}
