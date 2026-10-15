/* SPDX-License-Identifier: Apache-2.0 */
/* Native desktop, archive, recorder and candidate pagination. No real I/O. */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_statusbar_devices.c"
static unsigned object_count(lv_obj_t *obj)
{unsigned n=1;for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)n+=object_count(lv_obj_get_child(obj,i));return n;}
static void selected_page(void *context,unsigned page){*(unsigned *)context=page;}
static void delete_pager(void *context,unsigned page){(void)page;lv_obj_delete(*(lv_obj_t **)context);}
static void pager_jump(lv_obj_t *root,unsigned page)
{
  assert(root);struct glass_pager *pager=lv_obj_get_user_data(root);
  for(unsigned i=0;i<GLASS_PAGER_SLOTS;i++)if(pager->slots[i].hit&&pager->slots[i].page==page&&!lv_obj_has_flag(pager->slots[i].hit,LV_OBJ_FLAG_HIDDEN)){
    lv_obj_send_event(pager->slots[i].hit,LV_EVENT_CLICKED,NULL);return;
  }
  assert(!"requested page is not visible");
}
static lv_obj_t *find_pager(lv_obj_t *root)
{
  if(!root||!lv_obj_is_visible(root))return NULL;
  if(lv_obj_check_type(root,&lv_obj_class)&&lv_obj_get_user_data(root)&&lv_obj_get_height(root)==40&&lv_obj_get_width(root)<=136)return root;
  for(unsigned i=0;i<lv_obj_get_child_count(root);i++){lv_obj_t *p=find_pager(lv_obj_get_child(root,i));if(p)return p;}
  return NULL;
}
static void shared_checks(void)
{
  unsigned selected=999;
  lv_obj_t *pager=glass_pager_create(g_desktop.screen,0xf3f2f7,0x292c38,0x808491,selected_page,&selected);
  glass_pager_update(pager,0,35,true);unsigned warm=object_count(pager);
  for(unsigned i=0;i<1000;i++){glass_pager_update(pager,i%35,35,true);assert(object_count(pager)==warm);}
  glass_pager_update(pager,0,3,true);pager_jump(pager,2);assert(selected==2);
  glass_pager_set_enabled(pager,false);pager_jump(pager,1);assert(selected==2);
  glass_pager_update(pager,0,1,true);assert(!lv_obj_is_visible(pager));lv_obj_delete(pager);
  pager=glass_pager_create(g_desktop.screen,0,0,0,delete_pager,&pager);
  glass_pager_update(pager,0,2,true);pager_jump(pager,1);
  puts("PAGER_LIFETIME_OK: 1000 bounded updates; single page; disabled input; callback deletes owner");
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
  shared_checks();glass_edge_back_create();glass_status_create();
  extern void review_chat_pages_fixture(unsigned),review_pinyin_fixture(unsigned);
  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme==0;glass_rebuild();
    glass_apps(NULL);review_capture(display,"apps-first","全部应用 · 首页");
    lv_obj_t *pager=review_callback_find(g_desktop.panel,glass_pager_deleted);assert(pager);pager_jump(pager,1);
    assert(g_apps_page_index==1);review_capture(display,"apps-last","全部应用 · 末页");panel_hide(NULL);
    review_chat_fixture(1);review_chat_pages_fixture(3);glass_chat_open(NULL);chat_ui_refresh(NULL);
    review_click("阅读完整回复");chat_ui_refresh(NULL);review_capture(display,"archive-first","完整回复 · 首页");
    pager_jump(g_chat_ui.archive_pager,2);assert(g_chat_ui.archive_page->busy);
    chat_ui_refresh(NULL);assert(g_chat_ui.archive_page->page==2);review_capture(display,"archive-last","完整回复 · 末页");
    review_click("返回对话");assert(!g_chat_ui.archive_pager);panel_hide(NULL);
    launch_builtin_recorder_qapp(NULL);
    assert(qpk_review_eval("__clips=Array.from({length:12},(_,i)=>({clip:'rec_'+String(i+1).padStart(6,'0'),durationMs:12000,fileBytes:48044,valid:true}));refresh('','准备录音');for(let i=0;i<10;i++)__reviewTick()")==0);
    review_capture(display,"recorder-first","录音列表 · 首页");
    assert(qpk_review_eval("globalThis.__dot=RecorderApp.state().pager.find(s=>s.page===2)")==0);
    assert(qpk_review_click_at((int)qpk_review_number("right+rightWidth/2+24"),(int)qpk_review_number("H-62"))==0);
    assert(qpk_review_eval("if(RecorderApp.state().page!==2)throw Error('recorder jump failed')")==0);
    review_capture(display,"recorder-last","录音列表 · 末页");panel_hide(NULL);
    lv_obj_t *card=panel_card("输入法候选");
    lv_obj_t *target=lv_textarea_create(card);lv_obj_set_pos(target,32,24);lv_obj_set_size(target,880,90);
    lv_obj_t *keyboard=lv_keyboard_create(card);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,24,200);lv_obj_set_size(keyboard,880,240);lv_keyboard_set_textarea(keyboard,target);
    review_pinyin_fixture(30);glass_ime_attach(keyboard,zh_font(20),true,detail_ink(),detail_soft(),theme_accent());
    bool typed=false;for(unsigned i=0;i<100;i++){const char *text=lv_buttonmatrix_get_button_text(keyboard,i);if(!text)break;if(!strcmp(text,"n")){lv_buttonmatrix_set_selected_button(keyboard,i);lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,NULL);typed=true;break;}}
    assert(typed);lv_obj_update_layout(card);pager=find_pager(card);assert(pager);review_capture(display,"ime-first","候选词 · 首页");
    pager_jump(pager,2);review_capture(display,"ime-middle","候选词 · 第三页");
    assert(review_label_find(card,"候选11"));panel_hide(NULL);review_pinyin_fixture(0);
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  printf("ALL_PAGERS_NATIVE_OK: %u captures; apps/archive/recorder/IME selection\n",review_count);return 0;
}
