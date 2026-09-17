/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_music.h"
#include "glass_ime.h"

static struct {
  lv_obj_t *root, *list, *heading, *subtitle, *search, *status, *title, *artist;
  lv_obj_t *album, *bottom_title, *bottom_artist, *play_label, *favorite_label, *elapsed;
  lv_obj_t *volume, *count, *nav[2], *disc, *input_shade, *input, *keyboard;
  lv_timer_t *timer;
  struct music_snapshot snapshot;
  unsigned search_revision, favorite_count;
  bool favorites, valid;
  uint32_t primary, secondary, surface, accent, background;
} g_music_ui;

static void music_ui_refresh(lv_timer_t *timer);
static void music_ui_list(void);

static lv_obj_t *music_ui_text(lv_obj_t *parent, const char *text, int x, int y, int width, int size, uint32_t color)
{ return glass_text(parent,text,x,y,width,size,color); }

static lv_obj_t *music_ui_button(lv_obj_t *parent, const char *text, int x, int y, int width, int height,
                                uint32_t color, lv_event_cb_t cb, void *data, bool symbol)
{
  lv_obj_t *button=lv_button_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button,x,y); lv_obj_set_size(button,width,height);
  lv_obj_set_style_bg_color(button,lv_color_hex(color),0);
  lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);
  lv_obj_set_style_radius(button,height/2,0);
  glass_clickable(button,cb,data);
  lv_obj_set_style_bg_opa(button,LV_OPA_80,LV_STATE_PRESSED);
  lv_obj_t *label=music_ui_text(button,text,0,0,width-12,16,g_music_ui.primary);
  if (symbol) lv_obj_set_style_text_font(label,&lv_font_montserrat_24,0);
  lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_center(label);
  return button;
}

static void music_ui_play(lv_event_t *e)
{
  uintptr_t index=(uintptr_t)lv_event_get_user_data(e);
  const struct music_song *song=index ? &g_music_ui.snapshot.favorites[index-1] : &g_music_ui.snapshot.result;
  if (song->name[0]) glass_music_play(song);
  music_ui_refresh(NULL);
}

static void music_ui_toggle(lv_event_t *e)
{ LV_UNUSED(e); glass_music_toggle(); music_ui_refresh(NULL); }

static void music_ui_favorite(lv_event_t *e)
{
  const struct music_song *song=lv_event_get_user_data(e) ? &g_music_ui.snapshot.result : &g_music_ui.snapshot.current;
  if (!song->name[0]) { show_toast("先选择一首歌曲"); return; }
  int ret=glass_music_favorite(song);
  if (ret) show_toast("正在保存，请稍后再试");
}

static void music_ui_skip(lv_event_t *e)
{
  int direction=(intptr_t)lv_event_get_user_data(e);
  struct music_snapshot *s=&g_music_ui.snapshot;
  if (!s->favorite_count) { show_toast("收藏歌曲后可切换播放"); return; }
  int index=-1;
  for (unsigned i=0;i<s->favorite_count;i++)
    if (!strcmp(s->current.name,s->favorites[i].name) && !strcmp(s->current.artist,s->favorites[i].artist)) { index=i; break; }
  index=index<0?0:(index+direction+(int)s->favorite_count)%s->favorite_count;
  glass_music_play(&s->favorites[index]);
  music_ui_refresh(NULL);
}

static void music_ui_volume(lv_event_t *e)
{ glass_music_volume(lv_slider_get_value(lv_event_get_target(e))); }

static void music_ui_nav(lv_event_t *e)
{
  g_music_ui.favorites=lv_event_get_user_data(e)!=NULL;
  music_ui_list();
}

static void music_ui_suggestion(lv_event_t *e)
{
  const char *query=lv_event_get_user_data(e);
  lv_label_set_text(lv_obj_get_child(g_music_ui.search,0),query);
  g_music_ui.favorites=false;
  glass_music_search(query);
  music_ui_list(); music_ui_refresh(NULL);
}

static void music_ui_input_close(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_music_ui.input_shade) lv_obj_delete(g_music_ui.input_shade);
  g_music_ui.input_shade=g_music_ui.input=g_music_ui.keyboard=NULL;
}

static void music_ui_input_submit(lv_event_t *e)
{
  LV_UNUSED(e);
  glass_ime_commit(g_music_ui.keyboard);
  char query[128];
  const char *text=lv_textarea_get_text(g_music_ui.input);
  if (!text[0]) { show_toast("输入歌名或歌手"); return; }
  if (strlen(text)>=sizeof(query)) { show_toast("搜索词太长，请缩短一些"); return; }
  strcpy(query,text);
  lv_label_set_text(lv_obj_get_child(g_music_ui.search,0),query);
  music_ui_input_close(NULL);
  g_music_ui.favorites=false;
  glass_music_search(query);
  music_ui_list(); music_ui_refresh(NULL);
}

static void music_ui_input_open(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_music_ui.input_shade) return;
  lv_obj_t *shade=glass_box(g_music_ui.root,188,8,818,482,g_music_ui.background,20);
  g_music_ui.input_shade=shade;
  music_ui_text(shade,"搜索音乐",24,20,420,28,g_music_ui.primary);
  music_ui_button(shade,"取消",590,18,84,44,g_music_ui.surface,music_ui_input_close,NULL,false);
  lv_obj_t *submit=music_ui_button(shade,"搜索",688,18,102,44,g_music_ui.accent,music_ui_input_submit,NULL,false);
  lv_obj_set_style_text_color(lv_obj_get_child(submit,0),lv_color_white(),0);
  music_ui_text(shade,"连续输入拼音，按词频选词 · 空格确认 · 中 / EN 切换",24,72,756,16,g_music_ui.secondary);
  g_music_ui.input=lv_textarea_create(shade);
  glass_input_style(g_music_ui.input);
  lv_obj_set_pos(g_music_ui.input,24,108); lv_obj_set_size(g_music_ui.input,766,54);
  lv_textarea_set_one_line(g_music_ui.input,true); lv_textarea_set_max_length(g_music_ui.input,40);
  lv_textarea_set_placeholder_text(g_music_ui.input,"歌名、歌手，如：晴天");
  g_music_ui.keyboard=lv_keyboard_create(shade);
  glass_keyboard_style(g_music_ui.keyboard);
  lv_obj_set_size(g_music_ui.keyboard,782,240);
  lv_obj_align(g_music_ui.keyboard,LV_ALIGN_TOP_LEFT,16,226);
  lv_keyboard_set_mode(g_music_ui.keyboard,LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(g_music_ui.keyboard,g_music_ui.input);
  glass_ime_attach(g_music_ui.keyboard,zh_font(20),true,g_music_ui.primary,g_music_ui.surface,g_music_ui.accent);
  lv_obj_add_event_cb(g_music_ui.keyboard,music_ui_input_submit,LV_EVENT_READY,NULL);
  lv_obj_add_event_cb(g_music_ui.keyboard,music_ui_input_close,LV_EVENT_CANCEL,NULL);
}

static void music_ui_row(lv_obj_t *list, const struct music_song *song, unsigned index, int y)
{
  lv_obj_t *row=glass_box(list,0,y,462,82,g_music_ui.surface,14);
  glass_clickable(row,music_ui_play,(void *)(uintptr_t)index);
  lv_obj_t *art=glass_box(row,12,12,58,58,0x925d78,12);
  lv_obj_remove_flag(art,LV_OBJ_FLAG_CLICKABLE);
  glass_gradient(art,0xe49378,LV_GRAD_DIR_VER);
  glass_symbol(art,LV_SYMBOL_AUDIO,17,17,0xffffff);
  music_ui_text(row,song->name,84,16,320,20,g_music_ui.primary);
  music_ui_text(row,song->artist[0]?song->artist:"未知歌手",84,47,316,16,g_music_ui.secondary);
  glass_symbol(row,LV_SYMBOL_PLAY,425,28,g_music_ui.accent);
}

static void music_ui_list(void)
{
  if (!g_music_ui.list) return;
  lv_obj_clean(g_music_ui.list);
  lv_obj_scroll_to_y(g_music_ui.list,0,LV_ANIM_OFF);
  for (unsigned i=0;i<2;i++) {
    bool selected=(i==1)==g_music_ui.favorites;
    lv_obj_set_style_bg_color(g_music_ui.nav[i],lv_color_hex(selected?g_music_ui.surface:g_music_ui.background),0);
    lv_obj_set_style_text_color(lv_obj_get_child(g_music_ui.nav[i],0),lv_color_hex(selected?g_music_ui.accent:g_music_ui.secondary),0);
  }
  struct music_snapshot *s=&g_music_ui.snapshot;
  lv_label_set_text(g_music_ui.heading,g_music_ui.favorites?"我喜欢的音乐":"搜索与发现");
  lv_label_set_text(g_music_ui.subtitle,g_music_ui.favorites?"每一首喜欢，都值得再听一遍":"找到此刻想听的声音");
  if (g_music_ui.favorites) {
    if (!s->favorite_count) {
      music_ui_text(g_music_ui.list,"让喜欢的歌，留在这里",22,52,418,28,g_music_ui.primary);
      music_ui_text(g_music_ui.list,"播放时点一下收藏，下次打开还能听见。",22,102,418,16,g_music_ui.secondary);
    }
    for (unsigned i=0;i<s->favorite_count;i++) music_ui_row(g_music_ui.list,&s->favorites[i],i+1,i*92);
  } else if (s->searching) {
    music_ui_text(g_music_ui.list,"正在寻找好音乐…",22,52,418,28,g_music_ui.primary);
    music_ui_text(g_music_ui.list,"搜索完成后，点选歌曲开始播放",22,102,418,16,g_music_ui.secondary);
  } else if (s->has_result) {
    music_ui_text(g_music_ui.list,"歌曲",0,4,420,16,g_music_ui.secondary);
    music_ui_row(g_music_ui.list,&s->result,0,36);
    music_ui_text(g_music_ui.list,s->result.album,12,132,430,16,g_music_ui.secondary);
    bool favorite=glass_music_is_favorite(s,&s->result);
    music_ui_button(g_music_ui.list,favorite?"已收藏":"收藏这首歌",12,176,140,40,g_music_ui.surface,music_ui_favorite,(void *)1,false);
  } else {
    music_ui_text(g_music_ui.list,s->search_error?"暂时没有找到":"从一首喜欢的歌开始",0,14,452,28,g_music_ui.primary);
    music_ui_text(g_music_ui.list,s->search_error?"试试只输入歌名，或检查网络后重试。":"试着搜索",0,61,452,16,g_music_ui.secondary);
    const char *queries[]={"晴天","稻香","夜曲","青花瓷","海阔天空","红豆"};
    for (unsigned i=0;i<6;i++) music_ui_button(g_music_ui.list,queries[i],(i%2)*232,108+(i/2)*56,218,44,g_music_ui.surface,music_ui_suggestion,(void *)queries[i],false);
  }
}

static void music_ui_refresh(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_music_ui.root) return;
  unsigned previous_revision=g_music_ui.snapshot.revision;
  unsigned previous_search=g_music_ui.snapshot.search_revision;
  unsigned previous_count=g_music_ui.snapshot.favorite_count;
  bool searching=g_music_ui.snapshot.searching;
  int storage_error=g_music_ui.snapshot.storage_error;
  glass_music_get(&g_music_ui.snapshot);
  struct music_snapshot *s=&g_music_ui.snapshot;
  if (g_music_ui.valid && s->revision==previous_revision) return;
  if (!g_music_ui.valid || s->search_revision!=previous_search || s->favorite_count!=previous_count || s->searching!=searching) music_ui_list();
  if (s->storage_error && s->storage_error!=storage_error) show_toast(s->storage_error==-ENOSPC?"收藏空间已满":"收藏未保存，请检查存储");
  g_music_ui.valid=true;
  const char *title=s->current.name[0]?s->current.name:"还没有播放音乐";
  const char *artist=s->current.artist[0]?s->current.artist:"让好音乐，陪伴此刻";
  lv_label_set_text(g_music_ui.title,title); lv_label_set_text(g_music_ui.bottom_title,title);
  lv_label_set_text(g_music_ui.artist,artist); lv_label_set_text(g_music_ui.bottom_artist,artist);
  lv_label_set_text(g_music_ui.album,s->current.album);
  const char *status=s->state==MUSIC_LOADING?"正在加载音乐":s->state==MUSIC_PLAYING?"正在播放":s->state==MUSIC_PAUSED?"已暂停":
    s->state==MUSIC_ERROR?"播放失败 · 点击播放重试":s->state==MUSIC_ENDED?"播放结束":s->state==MUSIC_STOPPED?"已停止":"现在播放";
  lv_label_set_text(g_music_ui.status,status);
  lv_label_set_text(g_music_ui.play_label,s->state==MUSIC_PLAYING?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
  lv_label_set_text(g_music_ui.favorite_label,glass_music_is_favorite(s,&s->current)?"已收藏":"收藏");
  lv_label_set_text_fmt(g_music_ui.count,"%u 首珍藏",s->favorite_count);
  lv_label_set_text_fmt(g_music_ui.elapsed,"%02u:%02u",s->position_ms/60000,(s->position_ms/1000)%60);
  if (!lv_obj_has_state(g_music_ui.volume,LV_STATE_PRESSED)) lv_slider_set_value(g_music_ui.volume,s->volume,LV_ANIM_OFF);
}

static void music_ui_deleted(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_music_ui.timer) lv_timer_delete(g_music_ui.timer);
  memset(&g_music_ui,0,sizeof(g_music_ui));
}

static void glass_music_app(lv_obj_t *card)
{
  lv_obj_clean(card);
  memset(&g_music_ui,0,sizeof(g_music_ui));
  g_music_ui.root=card;
  bool light=g_desktop.light_theme;
  g_music_ui.primary=light?0x242333:0xf7f5fa;
  g_music_ui.secondary=light?0x777484:0xa7a2b5;
  g_music_ui.background=light?0xf8f7fa:0x15131d;
  g_music_ui.surface=light?0xeeecf3:0x272330;
  g_music_ui.accent=light?0xd82f62:0xff7297;
  lv_obj_set_size(card,1024,600); lv_obj_set_pos(card,0,0);
  lv_obj_set_style_radius(card,0,0); lv_obj_set_style_border_width(card,0,0);
  lv_obj_set_style_bg_color(card,lv_color_hex(g_music_ui.background),0);
  lv_obj_add_event_cb(card,music_ui_deleted,LV_EVENT_DELETE,NULL);
  lv_obj_t *sidebar=glass_box(card,0,0,178,500,light?0xf0edf4:0x1c1924,0);
  music_ui_text(sidebar,"声动",24,28,130,28,g_music_ui.accent);
  music_ui_text(sidebar,"MUSIC",26,70,128,16,g_music_ui.secondary);
  g_music_ui.nav[0]=music_ui_button(sidebar,"搜索与发现",12,130,154,48,g_music_ui.surface,music_ui_nav,NULL,false);
  g_music_ui.nav[1]=music_ui_button(sidebar,"我的收藏",12,190,154,48,g_music_ui.background,music_ui_nav,(void *)1,false);
  g_music_ui.count=music_ui_text(sidebar,"0 首珍藏",26,255,132,16,g_music_ui.secondary);
  music_ui_button(sidebar,"返回桌面",16,434,146,44,g_music_ui.surface,panel_hide,NULL,false);
  g_music_ui.search=music_ui_button(card,"搜索歌名、歌手",202,24,476,50,g_music_ui.surface,music_ui_input_open,NULL,false);
  lv_obj_set_width(lv_obj_get_child(g_music_ui.search,0),414);
  g_music_ui.heading=music_ui_text(card,"搜索与发现",202,104,480,28,g_music_ui.primary);
  g_music_ui.subtitle=music_ui_text(card,"找到此刻想听的声音",202,144,480,16,g_music_ui.secondary);
  g_music_ui.list=glass_box(card,202,188,482,294,g_music_ui.background,0);
  lv_obj_add_flag(g_music_ui.list,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(g_music_ui.list,LV_DIR_VER);
  lv_obj_t *now=glass_box(card,706,24,294,458,light?0xf0e8ee:0x292030,22);
  g_music_ui.status=music_ui_text(now,"现在播放",22,18,250,16,g_music_ui.secondary);
  g_music_ui.disc=glass_box(now,38,60,218,218,0x6f5b8c,20);
  glass_gradient(g_music_ui.disc,0xe19a83,LV_GRAD_DIR_VER);
  lv_obj_set_style_shadow_width(g_music_ui.disc,24,0); lv_obj_set_style_shadow_opa(g_music_ui.disc,36,0);
  lv_obj_t *circle=glass_box(g_music_ui.disc,32,32,154,154,0x252031,77);
  lv_obj_set_style_bg_opa(circle,190,0);
  lv_obj_t *ring=glass_box(circle,28,28,98,98,0xd69c9c,49);
  lv_obj_set_style_bg_opa(ring,70,0);
  glass_symbol(ring,LV_SYMBOL_AUDIO,37,37,0xffffff);
  g_music_ui.title=music_ui_text(now,"还没有播放音乐",22,306,250,28,g_music_ui.primary);
  g_music_ui.artist=music_ui_text(now,"让好音乐，陪伴此刻",22,350,250,20,g_music_ui.secondary);
  g_music_ui.album=music_ui_text(now,"",22,385,250,16,g_music_ui.secondary);
  music_ui_text(now,"把这一刻，交给音乐",22,426,250,16,g_music_ui.secondary);
  lv_obj_t *bar=glass_box(card,0,500,1024,100,light?0xffffff:0x211d29,0);
  lv_obj_set_style_border_width(bar,1,0); lv_obj_set_style_border_color(bar,lv_color_hex(g_music_ui.surface),0);
  lv_obj_t *mini=glass_box(bar,22,20,60,60,0x946680,12);
  glass_gradient(mini,0xe29a82,LV_GRAD_DIR_VER); glass_symbol(mini,LV_SYMBOL_AUDIO,18,18,0xffffff);
  g_music_ui.bottom_title=music_ui_text(bar,"还没有播放音乐",98,23,250,20,g_music_ui.primary);
  g_music_ui.bottom_artist=music_ui_text(bar,"让好音乐，陪伴此刻",98,56,250,16,g_music_ui.secondary);
  music_ui_button(bar,LV_SYMBOL_PREV,372,24,52,52,g_music_ui.surface,music_ui_skip,(void *)-1,true);
  lv_obj_t *play=music_ui_button(bar,LV_SYMBOL_PLAY,440,18,64,64,g_music_ui.accent,music_ui_toggle,NULL,true);
  g_music_ui.play_label=lv_obj_get_child(play,0); lv_obj_set_style_text_color(g_music_ui.play_label,lv_color_white(),0);
  music_ui_button(bar,LV_SYMBOL_NEXT,520,24,52,52,g_music_ui.surface,music_ui_skip,(void *)1,true);
  g_music_ui.elapsed=music_ui_text(bar,"00:00",594,40,72,16,g_music_ui.secondary);
  lv_obj_t *favorite=music_ui_button(bar,"收藏",680,28,88,44,g_music_ui.surface,music_ui_favorite,NULL,false);
  g_music_ui.favorite_label=lv_obj_get_child(favorite,0);
  glass_symbol(bar,LV_SYMBOL_VOLUME_MAX,798,38,g_music_ui.secondary);
  g_music_ui.volume=lv_slider_create(bar); lv_obj_set_pos(g_music_ui.volume,842,45); lv_obj_set_size(g_music_ui.volume,146,8);
  lv_slider_set_range(g_music_ui.volume,0,100);
  lv_obj_set_style_bg_color(g_music_ui.volume,lv_color_hex(g_music_ui.accent),LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_music_ui.volume,lv_color_hex(g_music_ui.accent),LV_PART_KNOB);
  lv_obj_add_event_cb(g_music_ui.volume,music_ui_volume,LV_EVENT_VALUE_CHANGED,NULL);
  glass_music_start();
  g_music_ui.timer=lv_timer_create(music_ui_refresh,250,NULL);
  music_ui_refresh(NULL);
}
