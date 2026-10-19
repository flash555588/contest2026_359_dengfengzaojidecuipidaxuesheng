/* SPDX-License-Identifier: Apache-2.0 */
/* Actual QuickJS setters and LVGL; no microphone, network or board access. */
#define main all_ui_reference_main
#include "render_all_ui.c"
#undef main
extern unsigned qpk_review_text_writes(void);
extern double qpk_review_number(const char *);
extern lv_obj_t *qpk_review_widget(int);
static const lv_font_t *performance_font(int size){(void)size;return LV_FONT_DEFAULT;}
int main(int argc,char **argv)
{
  assert(argc==3);lv_init();
  lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,review_flush);
  lv_obj_t *root=lv_obj_create(lv_screen_active());lv_obj_set_size(root,1024,600);
  assert(!qpk_runtime_launch(root,"Performance","org.test.performance","1",
    "builtin:/performance.js","",0,performance_font,NULL,NULL,NULL));
  assert(!qpk_review_eval("var text=ui.text('steady',0,0,200,20),"
    "button=ui.button('steady',0,40,200,40,()=>{}),"
    "number=ui.number('42',0,90,200,40);"));
  lv_obj_update_layout(root);
  unsigned writes=qpk_review_text_writes();
  assert(!qpk_review_eval("for(let i=0;i<1000;i++){ui.setText(text,'steady');"
    "ui.setText(button,'steady');ui.setText(number,'42')}"));
  unsigned unchanged=qpk_review_text_writes()-writes;
  bool baseline=getenv("FRONTEND_BASELINE")!=NULL;
  fprintf(stderr,"FRONTEND_COUNTER unchanged=%u baseline=%u\n",unchanged,baseline);
  assert(unchanged==(baseline?3000:0));
  writes=qpk_review_text_writes();
  assert(!qpk_review_eval("ui.setText(text,'changed');ui.setText(button,'changed');"
    "ui.setText(number,'43');"));
  assert(qpk_review_text_writes()-writes==3);
  int handle=(int)qpk_review_number("text");
  assert(!strcmp(lv_label_get_text(qpk_review_widget(handle)),"changed"));
  if(!baseline){
    assert(!qpk_review_eval("let converted=0;ui.setText(text,{toString(){converted++;return 'changed'}});"
      "if(converted!==1)throw Error('conversion skipped');"
      "let old=ui.text('old',0,150,100,20),fresh=0,rejected=false;"
      "try{ui.setText(old,{toString(){ui.remove(old);fresh=ui.text('new',0,150,100,20);return 'stale'}})}"
      "catch(e){rejected=true}if(!rejected||old!==fresh)throw Error('stale handle accepted');"));
    int fresh=(int)qpk_review_number("fresh");
    assert(!strcmp(lv_label_get_text(qpk_review_widget(fresh)),"new"));
  }
  printf("FRONTEND_TEXT_PERF unchanged_calls=3000 label_writes=%u changed_writes=3 baseline=%u\n",unchanged,baseline);
  qpk_runtime_stop();lv_obj_delete(root);lv_display_delete(display);lv_deinit();
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",argv[2]);
  FILE *file=fopen(path,"w");assert(file);fputs("[]\n",file);fclose(file);
  return 0;
}
