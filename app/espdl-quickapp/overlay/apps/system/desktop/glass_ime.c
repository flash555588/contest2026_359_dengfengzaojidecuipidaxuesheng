/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_ime.h"
#include "glass_pinyin.h"
#ifdef CONFIG_SYSTEM_DESKTOP
#include "glass_voice.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ime {
  lv_obj_t *keyboard, *bar, *mode, *composition, *candidates[5], *previous, *next, *target;
  char input[33], labels[5][100];
  unsigned page, count, slots;
  bool chinese;
#ifdef CONFIG_SYSTEM_DESKTOP
  lv_obj_t *voice;
  lv_timer_t *voice_timer;
  char *voice_base;
  uint32_t voice_revision, voice_cursor;
  bool voice_owns;
#endif
};
static void ime_key(lv_event_t *e);

static struct ime *get_ime(lv_obj_t *kb)
{
  for (uint32_t i=0; i<lv_obj_get_event_count(kb); i++) {
    lv_event_dsc_t *d = lv_obj_get_event_dsc(kb, i);
    if (lv_event_dsc_get_cb(d) == ime_key) return lv_event_dsc_get_user_data(d);
  }
  return NULL;
}

static void ime_render(struct ime *ime)
{
  lv_label_set_text(lv_obj_get_child(ime->mode,0), ime->chinese ? "中" : "EN");
  lv_label_set_text(ime->composition, ime->input[0] ? ime->input : ime->chinese ? "拼音" : "英文");
  ime->count = ime->input[0] ? glass_pinyin_search(ime->input) : 0;
  int width=lv_obj_get_width(ime->bar), start=width>700?200:128;
#ifdef CONFIG_SYSTEM_DESKTOP
  int end=122;
#else
  int end=76;
#endif
  char first[100]; glass_pinyin_candidate(0,first,sizeof(first));
  ime->slots=width>700 && strlen(first)<=12 ? 5 : 3;
  int size=(width-start-end)/(int)ime->slots;
  if (ime->page * ime->slots >= ime->count) ime->page = 0;
  for (unsigned i=0; i<5; i++) {
    if(i>=ime->slots) { lv_obj_add_flag(ime->candidates[i],LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_remove_flag(ime->candidates[i],LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ime->candidates[i],start+i*size,2); lv_obj_set_width(ime->candidates[i],size-2);
    lv_obj_set_width(lv_obj_get_child(ime->candidates[i],0),size-6);
    unsigned index=ime->page*ime->slots+i;
    ime->labels[i][0] = 0;
    if (index < ime->count) glass_pinyin_candidate(index,ime->labels[i],sizeof(ime->labels[i]));
    lv_label_set_text(lv_obj_get_child(ime->candidates[i],0), ime->labels[i]);
    if (index < ime->count) lv_obj_remove_state(ime->candidates[i],LV_STATE_DISABLED);
    else lv_obj_add_state(ime->candidates[i],LV_STATE_DISABLED);
  }
  if (!ime->page) lv_obj_add_state(ime->previous,LV_STATE_DISABLED);
  else lv_obj_remove_state(ime->previous,LV_STATE_DISABLED);
  if ((ime->page+1)*ime->slots >= ime->count) lv_obj_add_state(ime->next,LV_STATE_DISABLED);
  else lv_obj_remove_state(ime->next,LV_STATE_DISABLED);
}

static void reset_ime(struct ime *ime)
{ glass_pinyin_reset(); ime->input[0]=0; ime->page=0; ime_render(ime); }

static void commit_ime(struct ime *ime, const char *text)
{
  lv_obj_t *target = lv_keyboard_get_textarea(ime->keyboard);
  if (target && text && text[0]) lv_textarea_add_text(target,text);
  reset_ime(ime);
}

static void choose_ime(struct ime *ime,unsigned index)
{
  char text[100]; unsigned consumed=0;
  if(!glass_pinyin_choose(index,text,sizeof(text),&consumed)) return;
  if(consumed) {
    lv_obj_t *target=lv_keyboard_get_textarea(ime->keyboard);
    if(target) lv_textarea_add_text(target,text);
    size_t length=strlen(ime->input);
    if(consumed>length) consumed=length;
    memmove(ime->input,ime->input+consumed,length-consumed+1);
    glass_pinyin_reset();
  }
  ime->page=0; ime_render(ime);
}

void glass_ime_commit(lv_obj_t *keyboard)
{
  if (!keyboard) return;
  struct ime *ime = get_ime(keyboard);
  if (ime && ime->target != lv_keyboard_get_textarea(keyboard)) {
    ime->target = lv_keyboard_get_textarea(keyboard);
    reset_ime(ime);
    return;
  }
  if (ime && ime->input[0]) {
    /* An external submit accepts the best complete candidate, then preserves
     * an undecodable suffix literally instead of silently discarding it. */
    if(ime->count) choose_ime(ime,0);
    if(ime->input[0]) commit_ime(ime,ime->input);
  }
}

void glass_ime_commit_tree(lv_obj_t *parent)
{
  if (!parent) return;
  if (lv_obj_check_type(parent,&lv_keyboard_class)) { glass_ime_commit(parent); return; }
  for (uint32_t i=0;i<lv_obj_get_child_count(parent);i++) glass_ime_commit_tree(lv_obj_get_child(parent,i));
}

static void candidate_clicked(lv_event_t *e)
{
  struct ime *ime = lv_event_get_user_data(e);
  lv_obj_t *button = lv_event_get_current_target(e);
#ifdef CONFIG_SYSTEM_DESKTOP
  if(button==ime->voice) {
    if(ime->voice_owns) {
      glass_voice_stop();
      lv_label_set_text(ime->composition,"正在识别");
    }
    return;
  }
#endif
  if (button == ime->mode) { glass_ime_commit(ime->keyboard); ime->chinese=!ime->chinese; ime_render(ime); }
  else if (button == ime->previous) { if (ime->page) ime->page--; ime_render(ime); }
  else if (button == ime->next) { if ((ime->page+1)*ime->slots < ime->count) ime->page++; ime_render(ime); }
  else for (unsigned i=0; i<ime->slots; i++) if (button==ime->candidates[i]) { choose_ime(ime,ime->page*ime->slots+i); break; }
}

static void ime_single_click(lv_obj_t *keyboard)
{
  /* Commit on release and never auto-repeat a held key. Mode changes reset
   * the matrix control map, so reapply this after the default mode handler. */
  lv_buttonmatrix_set_button_ctrl_all(keyboard,
    LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
}

static void ime_default_key(lv_event_t *e)
{
  lv_obj_t *keyboard=lv_event_get_current_target(e);
  const char *text=lv_buttonmatrix_get_button_text(keyboard,lv_buttonmatrix_get_selected_button(keyboard));
  bool mode=text && (!strcmp(text,"ABC") || !strcmp(text,"abc") || !strcmp(text,"1#"));
  lv_keyboard_def_event_cb(e);
  /* READY / CANCEL can delete the keyboard synchronously. */
  if (mode) ime_single_click(keyboard);
}

static void ime_key(lv_event_t *e)
{
  struct ime *ime = lv_event_get_user_data(e);
#ifdef CONFIG_SYSTEM_DESKTOP
  if(ime->voice_owns) return;
#endif
  lv_obj_t *target = lv_keyboard_get_textarea(ime->keyboard);
  if (target != ime->target) { ime->target=target; reset_ime(ime); }
  uint32_t id=lv_buttonmatrix_get_selected_button(ime->keyboard);
  const char *text=lv_buttonmatrix_get_button_text(ime->keyboard,id);
  if (!text || !target) return;
  if (!ime->chinese || lv_textarea_get_password_mode(target)) { ime_default_key(e); return; }
  if (!strcmp(text,"ABC") || !strcmp(text,"abc") || !strcmp(text,"1#")) { glass_ime_commit(ime->keyboard); ime_default_key(e); return; }
  if (text[1]==0 && ((text[0]>='a' && text[0]<='z') || (text[0]>='A' && text[0]<='Z'))) {
    size_t n=strlen(ime->input);
    if (n < sizeof(ime->input)-1) { ime->input[n]=text[0] | 32; ime->input[n+1]=0; ime->page=0; ime_render(ime); }
    return;
  }
  if (!strcmp(text,"'") && ime->input[0]) {
    size_t n=strlen(ime->input);
    if(n<sizeof(ime->input)-1 && ime->input[n-1]!='\'') { ime->input[n]='\''; ime->input[n+1]=0; ime->page=0; ime_render(ime); }
    return;
  }
  if (!strcmp(text,LV_SYMBOL_BACKSPACE) && ime->input[0]) { ime->input[strlen(ime->input)-1]=0; ime->page=0; ime_render(ime); return; }
  if ((!strcmp(text," ") || !strcmp(text,"Enter") || !strcmp(text,LV_SYMBOL_NEW_LINE) || !strcmp(text,LV_SYMBOL_OK)) && ime->input[0]) {
    if(ime->count) choose_ime(ime,ime->page*ime->slots);
    else glass_ime_commit(ime->keyboard);
    /* First confirmation commits composition; second submits the form. */
    return;
  }
  if (!strcmp(text,LV_SYMBOL_CLOSE) || !strcmp(text,LV_SYMBOL_KEYBOARD)) reset_ime(ime);
  else glass_ime_commit(ime->keyboard);
  ime_default_key(e);
}

static void ime_bar_deleted(lv_event_t *e)
{
  struct ime *ime=lv_event_get_user_data(e);
  ime->bar=NULL;
}

static void ime_deleted(lv_event_t *e)
{
  struct ime *ime=lv_event_get_user_data(e);
#ifdef CONFIG_SYSTEM_DESKTOP
  if(ime->voice_owns) glass_voice_stop();
  if(ime->voice_timer) lv_timer_delete(ime->voice_timer);
  free(ime->voice_base);
#endif
  if (ime->bar) lv_obj_delete(ime->bar);
  glass_pinyin_flush();
  free(ime);
}

static lv_obj_t *ime_button(struct ime *ime, int x, int width, const lv_font_t *font, uint32_t foreground, uint32_t surface)
{
  lv_obj_t *button=lv_button_create(ime->bar);
  lv_obj_set_pos(button,x,2); lv_obj_set_size(button,width,40);
  lv_obj_remove_flag(button,LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_set_style_bg_color(button,lv_color_hex(surface),0);
  lv_obj_set_style_radius(button,8,0); lv_obj_set_style_shadow_width(button,0,0); lv_obj_set_style_border_width(button,0,0);
  lv_obj_t *label=lv_label_create(button);
  lv_obj_set_style_text_font(label,font,0); lv_obj_set_style_text_color(label,lv_color_hex(foreground),0);
  lv_obj_set_width(label,width-4); lv_label_set_long_mode(label,LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(label);
  lv_obj_add_event_cb(button,candidate_clicked,LV_EVENT_CLICKED,ime);
  return button;
}

#ifdef CONFIG_SYSTEM_DESKTOP
static size_t utf8_offset(const char *text,uint32_t characters)
{
  size_t offset=0;
  while(text[offset]&&characters--) {
    unsigned char c=(unsigned char)text[offset];
    offset+=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:(c&0xf8)==0xf0?4:1;
  }
  return offset;
}

static uint32_t utf8_characters(const char *text)
{
  uint32_t count=0;
  for(size_t i=0;text[i];) {
    unsigned char c=(unsigned char)text[i];
    i+=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:(c&0xf8)==0xf0?4:1;
    count++;
  }
  return count;
}

static const char *ime_voice_error_text(enum glass_voice_error error)
{
  switch(error) {
    case GLASS_VOICE_ERROR_CONFIG: return "需配置";
    case GLASS_VOICE_ERROR_CLOCK: return "等校时";
    case GLASS_VOICE_ERROR_TLS: return "TLS失败";
    case GLASS_VOICE_ERROR_AUTH: return "凭据错";
    case GLASS_VOICE_ERROR_AUDIO: return "录音错";
    case GLASS_VOICE_ERROR_EMPTY: return "没听清";
    case GLASS_VOICE_ERROR_MEMORY: return "内存低";
    case GLASS_VOICE_ERROR_RESPONSE: return "服务错";
    default: return "连接错";
  }
}

static void ime_voice_text(struct ime *ime,const char *text)
{
  if(!ime->voice_base||!ime->target||!lv_obj_is_valid(ime->target)) return;
  size_t base_size=strlen(ime->voice_base),offset=utf8_offset(ime->voice_base,ime->voice_cursor);
  size_t add=strlen(text);
  char *combined=malloc(base_size+add+1);
  if(!combined) return;
  memcpy(combined,ime->voice_base,offset);
  memcpy(combined+offset,text,add);
  memcpy(combined+offset+add,ime->voice_base+offset,base_size-offset+1);
  lv_textarea_set_text(ime->target,combined);
  lv_textarea_set_cursor_pos(ime->target,(int32_t)(ime->voice_cursor+utf8_characters(text)));
  free(combined);
}

static void ime_voice_release(struct ime *ime,bool restore)
{
  if(restore) ime_voice_text(ime,"");
  ime->voice_owns=false;
  lv_obj_remove_state(ime->keyboard,LV_STATE_DISABLED);
  lv_label_set_text(lv_obj_get_child(ime->voice,0),LV_SYMBOL_AUDIO);
  free(ime->voice_base);
  ime->voice_base=NULL;
  if(ime->voice_timer) {
    lv_timer_delete(ime->voice_timer);
    ime->voice_timer=NULL;
  }
}

static void ime_voice_poll(lv_timer_t *timer)
{
  struct ime *ime=lv_timer_get_user_data(timer);
  struct glass_voice_snapshot state;
  glass_voice_get(&state);
  if(!ime->voice_owns||state.revision==ime->voice_revision) return;
  ime->voice_revision=state.revision;
  if(!ime->target||!lv_obj_is_valid(ime->target)) {
    glass_voice_stop();
    ime_voice_release(ime,false);
    return;
  }
  if(state.text[0]) ime_voice_text(ime,state.text);
  if(state.phase==GLASS_VOICE_CONNECTING) lv_label_set_text(ime->composition,"正在连接");
  else if(state.phase==GLASS_VOICE_LISTENING) {
    lv_label_set_text(ime->composition,state.text[0]?state.text:"请说话");
    lv_label_set_text(lv_obj_get_child(ime->voice,0),LV_SYMBOL_STOP);
  } else if(state.phase==GLASS_VOICE_FINISHING) {
    lv_label_set_text(ime->composition,"正在识别");
  } else if(state.phase==GLASS_VOICE_DONE) {
    ime_voice_release(ime,false);
    reset_ime(ime);
  } else if(state.phase==GLASS_VOICE_ERROR) {
    enum glass_voice_error error=state.error;
    ime_voice_release(ime,true);
    reset_ime(ime);
    lv_label_set_text(ime->composition,ime_voice_error_text(error));
  }
}

static void ime_voice_begin(struct ime *ime)
{
  lv_obj_t *target=lv_keyboard_get_textarea(ime->keyboard);
  if(!target||lv_textarea_get_password_mode(target)) {
    lv_label_set_text(ime->composition,"密码框不可使用语音");
    return;
  }
  glass_ime_commit(ime->keyboard);
  target=lv_keyboard_get_textarea(ime->keyboard);
  const char *text=lv_textarea_get_text(target);
  char *base=strdup(text?text:"");
  if(!base) {
    lv_label_set_text(ime->composition,"可用内存不足");
    return;
  }
  int ret=glass_voice_start();
  if(ret) {
    free(base);
    struct glass_voice_snapshot state;
    glass_voice_get(&state);
    lv_label_set_text(ime->composition,
      ime_voice_error_text(state.error?state.error:GLASS_VOICE_ERROR_NETWORK));
    return;
  }
  ime->target=target;
  ime->voice_base=base;
  ime->voice_cursor=(uint32_t)lv_textarea_get_cursor_pos(target);
  ime->voice_owns=true;
  struct glass_voice_snapshot state;
  glass_voice_get(&state);
  ime->voice_revision=state.revision-1;
  lv_obj_add_state(ime->keyboard,LV_STATE_DISABLED);
  lv_label_set_text(ime->composition,"正在连接");
  if(!ime->voice_timer) ime->voice_timer=lv_timer_create(ime_voice_poll,80,ime);
}

static void ime_voice_clicked(lv_event_t *e)
{
  struct ime *ime=lv_event_get_user_data(e);
  if(ime->voice_owns) {
    glass_voice_stop();
    lv_label_set_text(ime->composition,"正在识别");
  } else {
    ime_voice_begin(ime);
  }
}
#endif

void glass_ime_attach(lv_obj_t *keyboard, const lv_font_t *font, bool chinese, uint32_t foreground, uint32_t surface, uint32_t accent)
{
  if (!keyboard || get_ime(keyboard)) return;
  struct ime *ime=calloc(1,sizeof(*ime));
  if (!ime) return;
  ime->keyboard=keyboard; ime->chinese=chinese;
  glass_pinyin_init(); glass_pinyin_reset();
  lv_obj_update_layout(keyboard);
  int width=lv_obj_get_width(keyboard);
  /* A sibling remains visible and touchable above the button matrix. */
  ime->bar=lv_obj_create(lv_obj_get_parent(keyboard)); lv_obj_remove_style_all(ime->bar);
  lv_obj_set_pos(ime->bar,lv_obj_get_x(keyboard),lv_obj_get_y(keyboard)-48);
  lv_obj_add_event_cb(ime->bar,ime_bar_deleted,LV_EVENT_DELETE,ime);
  lv_obj_set_size(ime->bar,width,44); lv_obj_remove_flag(ime->bar,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ime->bar,lv_color_hex(surface),0); lv_obj_set_style_bg_opa(ime->bar,LV_OPA_COVER,0); lv_obj_set_style_radius(ime->bar,10,0);
  ime->mode=ime_button(ime,2,44,font,accent,surface);
  ime->composition=lv_label_create(ime->bar); lv_obj_set_pos(ime->composition,52,12); lv_obj_set_width(ime->composition,width>700?140:70);
  lv_obj_set_style_text_font(ime->composition,font,0); lv_obj_set_style_text_color(ime->composition,lv_color_hex(foreground),0); lv_label_set_long_mode(ime->composition,LV_LABEL_LONG_DOT);
  int start=width>700?232:148;
#ifdef CONFIG_SYSTEM_DESKTOP
  int end=122;
#else
  int end=76;
#endif
  int size=(width-start-end)/5;
  for (unsigned i=0;i<5;i++) ime->candidates[i]=ime_button(ime,start+i*size,size-2,font,foreground,surface);
#ifdef CONFIG_SYSTEM_DESKTOP
  ime->voice=ime_button(ime,width-118,40,&lv_font_montserrat_24,accent,surface);
  lv_label_set_text(lv_obj_get_child(ime->voice,0),LV_SYMBOL_AUDIO);
  lv_obj_remove_event_cb(ime->voice,candidate_clicked);
  lv_obj_add_event_cb(ime->voice,ime_voice_clicked,LV_EVENT_CLICKED,ime);
#endif
  ime->previous=ime_button(ime,width-74,34,&lv_font_montserrat_24,foreground,surface);
  ime->next=ime_button(ime,width-38,34,&lv_font_montserrat_24,foreground,surface);
  lv_label_set_text(lv_obj_get_child(ime->previous,0),LV_SYMBOL_LEFT);
  lv_label_set_text(lv_obj_get_child(ime->next,0),LV_SYMBOL_RIGHT);
  lv_obj_remove_event_cb(keyboard,lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(keyboard,ime_key,LV_EVENT_VALUE_CHANGED,ime);
  lv_obj_add_event_cb(keyboard,ime_deleted,LV_EVENT_DELETE,ime);
  ime_single_click(keyboard);
  ime_render(ime);
}
