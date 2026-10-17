/* SPDX-License-Identifier: Apache-2.0 */
#define main review_full_pages_main
#include "render_all_ui.c"
#undef main
extern double qpk_review_number(const char *source);
extern void qpk_review_root_area(lv_area_t *area);
static lv_point_t camera_point;
static lv_indev_state_t camera_pressed;
static void camera_read(lv_indev_t *input,lv_indev_data_t *data)
{LV_UNUSED(input);data->point=camera_point;data->state=camera_pressed;}
static void camera_tick(lv_display_t *display)
{for(unsigned i=0;i<12;i++){if(qpk_runtime_running())assert(qpk_review_eval("__reviewTick()")==0);lv_tick_inc(20);lv_timer_handler();}lv_refr_now(display);}
static void camera_press(lv_indev_t *input,const char *caption,lv_display_t *display)
{
  lv_obj_update_layout(g_desktop.screen);lv_obj_t *label=review_label_find(g_desktop.current_card,caption);assert(label);
  lv_area_t a;lv_obj_get_coords(label,&a);camera_point=(lv_point_t){(a.x1+a.x2)/2,(a.y1+a.y2)/2};
  camera_pressed=LV_INDEV_STATE_PRESSED;lv_indev_read(input);lv_tick_inc(25);
  camera_pressed=LV_INDEV_STATE_RELEASED;lv_indev_read(input);camera_tick(display);
}
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];FILE *f=fopen(argv[1],"rb");assert(f);
  fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);void *font=malloc(size);assert(font&&fread(font,1,size,f)==size);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(font,size,16);g_desktop.font20=lv_tiny_ttf_create_data(font,size,20);g_desktop.font28=lv_tiny_ttf_create_data(font,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);
  g_glass.reduced_motion=true;g_desktop.screen=glass_box(lv_screen_active(),0,32,1024,568,0,0);lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,600,0,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  for(unsigned theme=0;theme<2;theme++){
    panel_hide(NULL);g_desktop.light_theme=theme==0;glass_rebuild();
    lv_obj_set_pos(g_desktop.panel,0,0);lv_obj_set_height(g_desktop.panel,568);
    launch_builtin_camera_qapp(NULL);camera_tick(display);
    lv_area_t root;qpk_review_root_area(&root);assert(root.y1==0&&lv_area_get_height(&root)==600);
    assert(!g_desktop.statusbar||lv_obj_has_flag(g_desktop.statusbar,LV_OBJ_FLAG_HIDDEN));
    assert(!review_label_find(g_desktop.current_card,"相机"));
    review_capture(display,"camera-empty","相机 · 未连接设备");
    assert(qpk_review_eval("__devices=Array.from({length:9},(_,i)=>({id:'usb:'+i,generation:1,name:'USB 相机 '+i,transport:'usb',previewSupported:true}));CameraPreview.refresh();system.camera.photoStatus=()=>({status:0,path:''});globalThis.__starts=0;system.camera.startDevice=()=>{__starts++;__cameraState={busy:true,error:0};return true;}")==0);
    camera_tick(display);review_capture(display,"camera-ready","相机 · 照片取景");
    review_click("照片");camera_tick(display);assert(review_label_find(g_desktop.current_card,"视频"));
    review_capture(display,"camera-video","相机 · 录像含麦克风声音");
    review_click("视频");camera_tick(display);
    review_click("设备");camera_tick(display);review_capture(display,"camera-devices","相机 · 设备与取景设置");
    const char *actions[]={"开始预览","刷新设备","下一页","上一页"};
    for(unsigned i=0;i<4;i++){
      lv_obj_t *label=review_label_find(g_desktop.current_card,actions[i]);assert(label);
      lv_area_t a;lv_obj_get_coords(lv_obj_get_parent(label),&a);
      assert(a.x1>=root.x1&&a.y1>=root.y1&&a.x2<=root.x2&&a.y2<=root.y2);
    }
    lv_indev_t *input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_display(input,display);lv_indev_set_read_cb(input,camera_read);
    camera_press(input,"下一页",display);camera_press(input,"下一页",display);
    review_capture(display,"camera-page3","相机 · 更多设备");assert(qpk_review_number("__starts")==0);
    camera_press(input,"开始预览",display);assert(qpk_review_number("__starts")==1);
    camera_press(input,"停止",display);assert(qpk_review_number("!__cameraState.busy"));
    camera_press(input,LV_SYMBOL_CLOSE,display);assert(!qpk_runtime_running());
    lv_indev_delete(input);
    assert(lv_obj_get_y(g_desktop.panel)==0&&lv_obj_get_height(g_desktop.panel)==568);
    assert(lv_obj_get_y(g_desktop.screen)==32&&lv_obj_get_height(g_desktop.screen)==568);
    assert(!g_desktop.statusbar||!lv_obj_has_flag(g_desktop.statusbar,LV_OBJ_FLAG_HIDDEN));
    puts("CAMERA_NATIVE_OK: fullscreen 600px, no header, safe-area restored; empty/9 devices/paging; actual pointer start-stop; both themes");
  }
  fputs("\n]\n",review_manifest);fclose(review_manifest);return 0;
}
