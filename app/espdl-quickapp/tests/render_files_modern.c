/* SPDX-License-Identifier: Apache-2.0 */
#define UNIFIED_DEVICE_NO_MAIN
#include "render_unified_devices.c"
extern void review_music_now_fixture(const struct music_now_playing *);
extern void review_lock_photo_fixture(int);
extern void review_cover_fixture(const char *);
static lv_point_t touch_point;
static lv_indev_state_t touch_state;
static unsigned leaked_touches;
static void touch_read(lv_indev_t *input,lv_indev_data_t *data)
{(void)input;data->point=touch_point;data->state=touch_state;}
static void touch(lv_indev_t *input,int x,int y,bool pressed)
{touch_point=(lv_point_t){x,y};touch_state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(input);}
static void underlying_click(lv_event_t *e){(void)e;leaked_touches++;}
static unsigned lock_object_count(lv_obj_t *root)
{unsigned count=1;for(unsigned i=0;i<lv_obj_get_child_count(root);i++)count+=lock_object_count(lv_obj_get_child(root,i));return count;}
static unsigned visible_page_buttons(void)
{
  unsigned count=0;struct glass_pager *pager=lv_obj_get_user_data(g_files_browser.pager);
  for(unsigned i=0;i<GLASS_PAGER_SLOTS;i++)
    if(pager->slots[i].hit&&!lv_obj_has_flag(pager->slots[i].hit,LV_OBJ_FLAG_HIDDEN))count++;
  return count;
}
static unsigned file_tile_deletes;
static void file_tile_deleted(lv_event_t *event){(void)event;file_tile_deletes++;}
static void files_boundary_pump(unsigned ticks)
{for(unsigned i=0;i<ticks;i++){lv_tick_inc(101);lv_timer_handler();}}
static void files_boundary_checks(void)
{
  extern void review_files_fault_fixture(unsigned,unsigned,int);
  extern unsigned review_files_live_jobs(void),review_files_cancel_count(void);
  extern void review_files_pages_fixture(unsigned);
  review_files_pages_fixture(3);g_files_page=0;g_files_root=GLASS_FILES_ROOT;strcpy(g_files_path,GLASS_FILES_ROOT);
  glass_files_render(NULL);files_boundary_pump(4);
  lv_obj_t *tile=review_find(g_desktop.panel,"透明图.png");assert(tile);
  lv_obj_send_event(tile,LV_EVENT_SHORT_CLICKED,NULL);assert(g_files_pending);
  panel_hide(NULL);files_boundary_pump(4);
  assert(!g_files_shell&&!g_files_job&&!g_files_list_job&&!review_files_live_jobs());

  review_files_fault_fixture(3,5,0);
  glass_files_render(NULL);files_boundary_pump(2);
  assert(g_files_pending&&g_files_retry&&!g_files_job);
  unsigned page=g_files_page;glass_files_up(NULL);assert(page==g_files_page);
  files_boundary_pump(3);assert(g_files_job&&g_files_pending);
  unsigned cancelled=review_files_cancel_count();panel_hide(NULL);files_boundary_pump(8);
  assert(!g_files_shell&&!review_files_live_jobs());assert(review_files_cancel_count()==cancelled+1);

  review_files_fault_fixture(0,0,ENODEV);glass_files_render(NULL);files_boundary_pump(4);
  assert(g_files_error[0]&&!g_files_pending&&!g_files_list_job&&!review_files_live_jobs());
  glass_files_refresh(NULL);files_boundary_pump(4);
  assert(!g_files_error[0]&&g_files_list_job&&review_files_live_jobs()==1);
  panel_hide(NULL);assert(!review_files_live_jobs());

  glass_files_render(NULL);files_boundary_pump(4);
  review_files_fault_fixture(0,0,EACCES);
  glass_files_begin(GLASS_FILE_RENAME,"/data","/data/notes.txt","new.txt");
  files_boundary_pump(6);
  assert(g_files_error[0]&&g_files_list_job); /* Mutation failure stays visible. */
  panel_hide(NULL);assert(!review_files_live_jobs()&&!g_files_error[0]);

  extern unsigned review_album_scan_calls(void);
  extern const char *review_album_decode_path(void);
  glass_files_render(NULL);files_boundary_pump(4);
  char media[ALBUM_PATH];strcpy(media,"/data/");memset(media+6,'a',200);strcpy(media+206,".png");
  strcpy(g_files_selected,media);unsigned scans=review_album_scan_calls();
  glass_files_image(NULL);files_boundary_pump(4);
  assert(review_find(g_desktop.panel,"返回文件"));
  assert(review_album_scan_calls()==scans&&!strcmp(review_album_decode_path(),media));
  review_click("返回文件");files_boundary_pump(4);panel_hide(NULL);
  assert(!review_files_live_jobs()&&!g_files_shell);
  puts("FILES_BOUNDARY_OK: queued image close; EBUSY retry; pending close/cancel; directory error recovery; mutation error retained; 204-byte media filename; direct viewer does not scan directories");
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
  g_desktop.screen=glass_box(lv_screen_active(),0,GLASS_STATUS_HEIGHT,1024,GLASS_CONTENT_HEIGHT,0,0);lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0x000000,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);




  g_glass.font72=lv_tiny_ttf_create_data(font,size,72);
  lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,touch_read);lv_indev_set_display(input,display);



  extern void review_album_fixture(int);
  extern unsigned review_file_action_count(unsigned);
  extern void review_files_pages_fixture(unsigned);
  glass_edge_back_create();glass_status_create();
  for(unsigned theme=0;theme<2;theme++) {
    g_desktop.light_theme=theme!=0;glass_rebuild();review_album_fixture(1);review_files_pages_fixture(3);
    g_files_root=GLASS_FILES_ROOT;strcpy(g_files_path,GLASS_FILES_ROOT);g_files_clipboard[0]=0;
    glass_files_render(NULL);for(unsigned i=0;i<3;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(!review_find(g_desktop.panel,"需转换后打开"));
    glass_status_tick();review_capture(display,"files-grid","文件管理 · 多格式网格");
    assert(visible_page_buttons()==2);
    lv_obj_t *retained[GLASS_FILE_PAGE_SIZE];
    for(unsigned i=0;i<GLASS_FILE_PAGE_SIZE;i++){
      retained[i]=g_files_browser.tiles[i].button;assert(retained[i]);
      lv_obj_add_event_cb(retained[i],file_tile_deleted,LV_EVENT_DELETE,NULL);
    }
    unsigned deletes_before=file_tile_deletes;
    for(unsigned repeat=0;repeat<100;repeat++){
      glass_files_refresh(NULL);for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
      for(unsigned i=0;i<GLASS_FILE_PAGE_SIZE;i++)assert(g_files_browser.tiles[i].button==retained[i]);
    }
    assert(file_tile_deletes==deletes_before);

    lv_obj_send_event(((struct glass_pager *)lv_obj_get_user_data(g_files_browser.pager))->slots[1].hit,LV_EVENT_CLICKED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(g_files_page==1&&visible_page_buttons()==3);
    for(unsigned i=0;i<GLASS_FILE_PAGE_SIZE;i++)assert(g_files_browser.tiles[i].button==retained[i]);
    assert(file_tile_deletes==deletes_before);
    review_capture(display,"files-page2","居中胶囊 · 当前第二页");
    lv_obj_t *pdf=review_find(g_desktop.panel,"使用说明.PDF");assert(pdf);
    lv_obj_send_event(pdf,LV_EVENT_SHORT_CLICKED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(review_find(g_desktop.panel,"此文件不提供文本预览"));
    assert(file_tile_deletes==deletes_before+GLASS_FILE_PAGE_SIZE);
    for(unsigned i=0;i<GLASS_FILE_PAGE_SIZE;i++)assert(!g_files_browser.tiles[i].button);

    review_capture(display,"files-document","文档类型 · 文件详情");
    review_click("返回");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(g_files_page==1);

    lv_obj_send_event(((struct glass_pager *)lv_obj_get_user_data(g_files_browser.pager))->slots[2].hit,LV_EVENT_CLICKED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(g_files_page==2&&visible_page_buttons()==3);
    review_capture(display,"files-page3","居中胶囊 · 当前最后一页");
    lv_obj_send_event(((struct glass_pager *)lv_obj_get_user_data(g_files_browser.pager))->slots[0].hit,LV_EVENT_CLICKED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(g_files_page==0);
    review_files_pages_fixture(1);glass_files_refresh(NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(visible_page_buttons()==1);
    for(unsigned i=12;i<GLASS_FILE_PAGE_SIZE;i++){
      lv_obj_t *hidden=g_files_browser.tiles[i].button;
      assert(!hidden || (lv_obj_has_flag(hidden,LV_OBJ_FLAG_HIDDEN)&&!lv_obj_get_user_data(hidden)));
    }

    review_capture(display,"files-single-page","居中胶囊 · 单页状态");

    lv_obj_t *tile=review_find(g_desktop.panel,"透明图.png");assert(tile);lv_obj_send_event(tile,LV_EVENT_SHORT_CLICKED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(review_find(g_desktop.panel,"返回文件"));review_capture(display,"files-png","PNG 查看 · 返回文件管理");
    review_click("返回文件");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    tile=review_find(g_desktop.panel,"手机照片.HEIC");assert(tile);lv_obj_send_event(tile,LV_EVENT_SHORT_CLICKED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    review_capture(display,"files-heic","HEIC · 明确格式与转换提示");
    review_click("返回文件");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    tile=review_find(g_desktop.panel,"旅行短片.MOV");assert(tile);lv_obj_send_event(tile,LV_EVENT_SHORT_CLICKED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(review_find(g_desktop.panel,"返回文件"));review_capture(display,"files-mov","MOV · 按编码播放");
    review_click("返回文件");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    tile=review_find(g_desktop.panel,"notes.txt");assert(tile);lv_obj_send_event(tile,LV_EVENT_LONG_PRESSED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    review_capture(display,"files-details","文件详情 · 原有操作保留");
    review_click("重命名");lv_tick_inc(101);lv_timer_handler();review_capture(display,"files-rename","重命名 · 确认后执行");
    assert(g_files_input);lv_textarea_set_text(g_files_input,"新文件.txt");unsigned renamed=review_file_action_count(GLASS_FILE_RENAME);
    review_click("确认");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}assert(review_file_action_count(GLASS_FILE_RENAME)==renamed+1);
    tile=review_find(g_desktop.panel,"notes.txt");assert(tile);lv_obj_send_event(tile,LV_EVENT_LONG_PRESSED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}review_click("复制");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    review_capture(display,"files-copy","复制 · 选择目标并粘贴");unsigned copied=review_file_action_count(GLASS_FILE_COPY);
    review_click("粘贴");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}assert(review_file_action_count(GLASS_FILE_COPY)==copied+1);
    tile=review_find(g_desktop.panel,"notes.txt");assert(tile);lv_obj_send_event(tile,LV_EVENT_LONG_PRESSED,NULL);
    for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}review_click("删除");lv_tick_inc(101);lv_timer_handler();
    unsigned removed=review_file_action_count(GLASS_FILE_REMOVE);review_capture(display,"files-delete","删除确认 · 保留既有安全步骤");
    assert(review_file_action_count(GLASS_FILE_REMOVE)==removed);review_click("确认");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}assert(review_file_action_count(GLASS_FILE_REMOVE)==removed+1);
    tile=review_find(g_desktop.panel,"music");assert(tile);lv_obj_send_event(tile,LV_EVENT_SHORT_CLICKED,NULL);for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    assert(strstr(g_files_path,"/music"));glass_files_up(NULL);for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}
    review_click("SD 卡");for(unsigned i=0;i<4;i++){lv_tick_inc(101);lv_timer_handler();}assert(!strcmp(g_files_root,"/sdcard"));review_capture(display,"files-sd","SD 卡 · 同一文件管理界面");
    panel_hide(NULL);
    for(unsigned i=0;i<20;i++){glass_files_render(NULL);lv_tick_inc(101);lv_timer_handler();panel_hide(NULL);}
    assert(!g_files_shell && !g_files_job && !g_files_list_job);
    files_boundary_checks();
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);
  puts("FILES_NATIVE_OK: both themes; all file types stay visible; PNG/view return, HEIC explanation, MOV player, long-press management, rename/copy/delete confirmation dispatch, centered capsule first/middle/last/single-page navigation; source/folder navigation and 40 cleanup cycles; 200 warm browser refreshes reused every tile without deletion; viewer entry freed tile pools; all filesystem mutations mocked");return 0;
}
