#include "glass_ime.h"
#include "glass_pinyin.h"
#include <lvgl/src/widgets/buttonmatrix/lv_buttonmatrix_private.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_indev_data_t pointer;
static void read_pointer(lv_indev_t *dev, lv_indev_data_t *data)
{ (void)dev; *data=pointer; }
static void key(lv_indev_t *dev, lv_obj_t *kb, const char *text, unsigned hold)
{
  lv_obj_update_layout(kb);
  const char *const *map=lv_buttonmatrix_get_map(kb);
  unsigned id=0, i;
  for (i=0;map[i][0];i++) {
    if (!strcmp(map[i],"\n")) continue;
    if (!strcmp(map[i],text)) break;
    id++;
  }
  assert(map[i][0]);
  lv_area_t area; lv_obj_get_coords(kb,&area);
  lv_area_t b=((lv_buttonmatrix_t *)kb)->button_areas[id];
  pointer.point.x=area.x1+(b.x1+b.x2)/2;
  pointer.point.y=area.y1+(b.y1+b.y2)/2;
  pointer.state=LV_INDEV_STATE_PRESSED;
  for (unsigned t=0;t<hold;t+=20) { lv_tick_inc(20); lv_indev_read(dev); }
  pointer.state=LV_INDEV_STATE_RELEASED;
  lv_tick_inc(20); lv_indev_read(dev);
}
static lv_obj_t *find_button(lv_obj_t *obj,const char *text)
{
  if (lv_obj_check_type(obj,&lv_button_class) && lv_obj_get_child_count(obj) &&
      !strcmp(lv_label_get_text(lv_obj_get_child(obj,0)),text)) return obj;
  for (unsigned i=0;i<lv_obj_get_child_count(obj);i++) {
    lv_obj_t *found=find_button(lv_obj_get_child(obj,i),text); if(found) return found;
  }
  return NULL;
}
static bool has_label(lv_obj_t *obj,const char *text)
{
  if(lv_obj_check_type(obj,&lv_label_class) && !strcmp(lv_label_get_text(obj),text)) return true;
  for(unsigned i=0;i<lv_obj_get_child_count(obj);i++) if(has_label(lv_obj_get_child(obj,i),text)) return true;
  return false;
}
int main(void)
{
  lv_init();
  lv_display_t *disp=lv_display_create(1024,600);
  lv_indev_t *dev=lv_indev_create();
  lv_indev_set_type(dev,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(dev,read_pointer);
  lv_obj_t *root=lv_screen_active();
  lv_obj_t *ta=lv_textarea_create(root); lv_obj_set_size(ta,800,80);
  lv_obj_t *kb=lv_keyboard_create(root); lv_obj_set_size(kb,800,240); lv_obj_align(kb,LV_ALIGN_TOP_LEFT,10,200);
  lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_LOWER); lv_keyboard_set_textarea(kb,ta);
  glass_ime_attach(kb,&lv_font_montserrat_24,true,0x222222,0xeeeeee,0xcc2255);
  key(dev,kb,"q",2000); assert(has_label(root,"q")); glass_ime_commit(kb);
  lv_textarea_set_text(ta,"");
  key(dev,kb,"q",40); key(dev,kb,"q",40); assert(has_label(root,"qq")); glass_ime_commit(kb);
  lv_textarea_set_text(ta,"");
  for (const char *p="qingtian";*p;p++) { char ch[]={*p,0}; key(dev,kb,ch,40); }
  glass_ime_commit(kb); assert(!strcmp(lv_textarea_get_text(ta),"晴天"));
  lv_obj_t *mode=find_button(root,"中"); assert(mode); lv_obj_send_event(mode,LV_EVENT_CLICKED,NULL);
  lv_textarea_set_text(ta,""); key(dev,kb,"a",2000);
  assert(!strcmp(lv_textarea_get_text(ta),"a"));
  key(dev,kb,"1#",40); key(dev,kb,"1",2000);
  assert(!strcmp(lv_textarea_get_text(ta),"a1"));
  key(dev,kb,"abc",40); key(dev,kb,"b",2000);
  assert(!strcmp(lv_textarea_get_text(ta),"a1b"));
  lv_obj_clean(root);
  for(unsigned i=0;i<20;i++) {
    lv_obj_t *panel=lv_obj_create(root);
    ta=lv_textarea_create(panel); kb=lv_keyboard_create(panel);
    lv_keyboard_set_textarea(kb,ta);
    glass_ime_attach(kb,&lv_font_montserrat_24,true,0,0,0);
    lv_obj_delete(panel);
  }
  const char *pinyin[]={"de","wo","shi","nihao","zhongguo","yinyue","woxihuanyinyue"};
  const char *expected[]={"的","我","是","你好","中国","音乐","我喜欢音乐"};
  for(unsigned i=0;i<7;i++) {
    glass_pinyin_reset(); unsigned count=glass_pinyin_search(pinyin[i]); char result[100];
    assert(count && glass_pinyin_candidate(0,result,sizeof(result)));
    printf("%s -> %s (%u candidates)\n",pinyin[i],result,count); fflush(stdout);
    assert(!strcmp(result,expected[i]));
  }
  glass_pinyin_reset(); unsigned count=glass_pinyin_search("nihao"), prefix=count;
  char result[100];
  for(unsigned i=1;i<count;i++) {
    assert(glass_pinyin_candidate(i,result,sizeof(result)));
    if(!strcmp(result,"你")) { prefix=i; break; }
  }
  assert(prefix<count); unsigned consumed;
  assert(glass_pinyin_choose(prefix,result,sizeof(result),&consumed)); assert(!consumed);
  assert(glass_pinyin_choose(0,result,sizeof(result),&consumed));
  assert(consumed==5 && !strcmp(result,"你好"));
  puts("PASS: frequency-ranked characters, words, sentence and partial candidate selection");
  lv_indev_delete(dev); lv_display_delete(disp); lv_deinit(); glass_pinyin_close();
  puts("PASS: real LVGL pointer taps, 2s holds, repeated intentional taps, Chinese phrase, EN/numeric mode changes and 20 teardown cycles");
}
