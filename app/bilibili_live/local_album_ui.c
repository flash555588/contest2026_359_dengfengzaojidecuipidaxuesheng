/* SPDX-License-Identifier: Apache-2.0 */
#include "local_album_ui.h"
#include "local_album.h"
#include "local_video_ui.h"
#include "glass_icons.h"
#include "glass_pager.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __NuttX__
#include <sys/statfs.h>
#endif

static struct {
  lv_obj_t *root,*note,*pager,*view,*canvas,*overlay;
  lv_obj_t *thumb[ALBUM_PAGE],*placeholder[ALBUM_PAGE];
  struct album_listing *listing;
  lv_timer_t *timer;
  const lv_font_t *font;
  lv_event_cb_t return_to_files;
  char direct_path[ALBUM_PATH];
  unsigned page,filter,selected,visible[ALBUM_PAGE],count,thumb_next,pending_slot;
  int mode,job,action;
  bool need_list,pending_list,queued,light,image_attempted;
  uint32_t background,paper,soft,ink,muted;
} album_ui;
static char album_directory[ALBUM_PATH]=ALBUM_ROOT;
static void album_grid(void);
static void album_open_index(unsigned index);
static void album_tick(lv_timer_t *timer);
static void album_dispatch(void *unused);

static lv_obj_t *album_box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius)
{
  lv_obj_t *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
  lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
  lv_obj_set_style_radius(obj,radius,0);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}
static lv_obj_t *album_text(lv_obj_t *parent,const char *text,int x,int y,int w,uint32_t color)
{
  lv_obj_t *label=lv_label_create(parent);lv_label_set_text(label,text);
  lv_obj_set_pos(label,x,y);lv_obj_set_width(label,w);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(label,album_ui.font,0);lv_obj_set_style_text_color(label,lv_color_hex(color),0);
  lv_obj_remove_flag(label,LV_OBJ_FLAG_CLICKABLE);return label;
}
static void album_queue(int action)
{
  if (album_ui.queued || !album_ui.root) return;
  album_ui.action=action;
  if (lv_async_call(album_dispatch,NULL)==LV_RESULT_OK) album_ui.queued=true;
}
static void album_clicked(lv_event_t *e) {album_queue((intptr_t)lv_event_get_user_data(e));}
static void album_page_select(void *context,unsigned page)
{
  (void)context;
  if(!album_ui.listing)return;
  if(album_ui.mode==0){album_queue(-100-(int)page);return;}
  unsigned ordinal=0;
  for(unsigned i=0;i<album_ui.listing->count;i++)if(album_ui.listing->entries[i].kind==ALBUM_PHOTO){
    if(ordinal++==page){album_queue(i+1);return;}
  }
}
static void album_photo_pager(unsigned index)
{
  unsigned ordinal=0,total=0;
  for(unsigned i=0;i<album_ui.listing->count;i++)if(album_ui.listing->entries[i].kind==ALBUM_PHOTO){
    if(i<index)ordinal++;total++;
  }
  album_ui.pager=glass_pager_create(album_ui.root,album_ui.soft,album_ui.ink,album_ui.muted,album_page_select,NULL);
  glass_pager_update(album_ui.pager,ordinal,total,true);
  if(album_ui.pager)lv_obj_align(album_ui.pager,LV_ALIGN_BOTTOM_MID,0,-8);
}
static void album_video_back(lv_event_t *e) {(void)e;album_queue(-1);}
static lv_obj_t *album_button(lv_obj_t *parent,const char *text,int x,int y,int w,int action,bool selected)
{
  lv_obj_t *button=album_box(parent,x,y,w,40,selected?album_ui.paper:album_ui.background,12);
  lv_obj_add_flag(button,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(button,LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(button,album_clicked,LV_EVENT_CLICKED,(void *)(intptr_t)action);
  lv_obj_set_style_bg_color(button,lv_color_hex(album_ui.soft),LV_STATE_PRESSED);
  lv_obj_t *label=album_text(button,text,0,0,w,selected?album_ui.ink:album_ui.muted);
  lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);return button;
}
struct album_bitmap {lv_image_dsc_t image;uint16_t *pixels;};
static void album_pixels_deleted(lv_event_t *e)
{
  struct album_bitmap *bitmap=lv_event_get_user_data(e);
  lv_image_cache_drop(&bitmap->image);free(bitmap->pixels);free(bitmap);
}
static lv_obj_t *album_canvas(lv_obj_t *host,struct album_image *image)
{
  struct album_bitmap *bitmap=calloc(1,sizeof(*bitmap));
  if(!bitmap)return NULL;
  bitmap->pixels=image->pixels;
  bitmap->image.header.magic=LV_IMAGE_HEADER_MAGIC;bitmap->image.header.cf=LV_COLOR_FORMAT_RGB565;
  bitmap->image.header.w=image->width;bitmap->image.header.h=image->height;
  /* Decoded rows are tightly packed, including odd-width portrait images.
   * lv_canvas_set_buffer would silently add the configured stride padding. */
  bitmap->image.header.stride=image->width*2;
  bitmap->image.data_size=image->width*image->height*2;bitmap->image.data=(const uint8_t *)image->pixels;
  lv_obj_t *canvas=lv_image_create(host);lv_image_set_src(canvas,&bitmap->image);
  lv_obj_remove_flag(canvas,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(canvas,album_pixels_deleted,LV_EVENT_DELETE,bitmap);
  lv_obj_move_to_index(canvas,0);image->pixels=NULL;return canvas;
}
static void album_fit(lv_obj_t *canvas,lv_obj_t *host)
{
  if(!canvas)return;
  lv_obj_update_layout(host);const lv_image_dsc_t *src=lv_image_get_src(canvas);
  if(!src || !src->header.w || !src->header.h)return;
  unsigned scale=lv_obj_get_width(host)*256/src->header.w;
  unsigned vertical=lv_obj_get_height(host)*256/src->header.h;if(vertical<scale)scale=vertical;
  lv_image_set_pivot(canvas,src->header.w/2,src->header.h/2);lv_image_set_scale(canvas,scale);
  lv_image_set_antialias(canvas,false);lv_obj_center(canvas);
}
static void album_fullscreen_close(void)
{
  if (!album_ui.overlay) return;
  lv_obj_t *overlay=album_ui.overlay;album_ui.overlay=NULL;
  if(album_ui.canvas && album_ui.view){lv_obj_set_parent(album_ui.canvas,album_ui.view);album_fit(album_ui.canvas,album_ui.view);}
  lv_obj_delete(overlay);
}
static void album_fullscreen(void)
{
  if (album_ui.overlay) {album_fullscreen_close();return;}
  if (!album_ui.canvas) return;
  lv_obj_t *overlay=album_box(lv_layer_top(),0,0,1024,600,0x000000,0);album_ui.overlay=overlay;
  lv_obj_set_parent(album_ui.canvas,overlay);album_fit(album_ui.canvas,overlay);
  album_button(overlay,"收起",920,12,88,-4,true);
}
static void album_clear(void)
{
  local_album_cancel();album_fullscreen_close();album_ui.canvas=album_ui.view=NULL;
  if(album_ui.timer)lv_timer_resume(album_ui.timer);
  album_ui.job=0;album_ui.pending_list=false;
  memset(album_ui.thumb,0,sizeof(album_ui.thumb));memset(album_ui.placeholder,0,sizeof(album_ui.placeholder));
  lv_obj_clean(album_ui.root);album_ui.note=album_ui.pager=NULL;
}
static const char *album_image_error(int error)
{
  return error==-E2BIG?"图片超出当前解码范围":
         error==-ENOMEM?"内存不足，请关闭其他页面后重试":
         error==-ENOENT || error==-ENODEV || error==-EIO?"无法读取图片，请检查 SD 卡":
         "暂时无法显示此图片，可能使用了不支持的编码";
}
static void album_gesture(lv_event_t *e)
{
  lv_indev_t *input=lv_indev_active();if(!input || album_ui.mode!=1)return;
  lv_dir_t direction=lv_indev_get_gesture_dir(input);
  int delta=direction==LV_DIR_LEFT?1:direction==LV_DIR_RIGHT?-1:0;
  if(!delta || !album_ui.listing)return;
  for(int i=(int)album_ui.selected+delta;i>=0&&i<(int)album_ui.listing->count;i+=delta)
    if(album_ui.listing->entries[i].kind==ALBUM_PHOTO){lv_indev_wait_release(input);album_queue(i+1);break;}
}
static bool album_entry_path(char *path,size_t size,unsigned index)
{
  if(album_ui.direct_path[0])
    return index==0 && snprintf(path,size,"%s",album_ui.direct_path)<(int)size;
  return snprintf(path,size,"%s/%s",album_ui.listing->path,
    album_ui.listing->entries[index].name)<(int)size;
}
static void album_open_index(unsigned index)
{
  if(!album_ui.listing || index>=album_ui.listing->count)return;
  struct album_entry entry=album_ui.listing->entries[index];char path[ALBUM_PATH];
  if(!album_entry_path(path,sizeof(path),index))return;
  if(entry.kind==ALBUM_FOLDER){strcpy(album_directory,path);album_ui.page=0;album_ui.need_list=true;album_grid();return;}
  album_clear();album_ui.selected=index;
  lv_obj_update_layout(album_ui.root);
  int width=lv_obj_get_width(album_ui.root),height=lv_obj_get_height(album_ui.root);
  enum album_format format=entry.format?entry.format:local_album_name_format(entry.name);
  if(entry.kind==ALBUM_VIDEO && local_album_format_supported(format)){
    album_ui.mode=2;lv_obj_t *holder=album_box(album_ui.root,0,0,width,height,album_ui.background,0);
    local_video_view(holder,album_ui.font,path,album_video_back);
    if(album_ui.return_to_files)lv_label_set_text(lv_obj_get_child(lv_obj_get_child(holder,0),0),"返回文件");
    if(album_ui.timer)lv_timer_pause(album_ui.timer);
    return;
  }
  album_ui.mode=1;album_ui.image_attempted=!local_album_format_supported(format);
  album_button(album_ui.root,album_ui.return_to_files?"返回文件":"返回相册",16,10,112,-1,false);
  const char *title=strrchr(path,'/');title=title?title+1:entry.name;
  lv_obj_t *heading=album_text(album_ui.root,title,150,21,width-300,album_ui.ink);
  lv_obj_set_height(heading,24);
  if(entry.kind==ALBUM_PHOTO)album_photo_pager(index);
  if(album_ui.image_attempted){
    int card_height=height-88;
    if(album_ui.pager&&!lv_obj_has_flag(album_ui.pager,LV_OBJ_FLAG_HIDDEN))card_height-=40;
    album_ui.view=album_box(album_ui.root,24,64,width-48,card_height,album_ui.paper,16);
    lv_obj_t *badge=album_box(album_ui.view,24,24,240,card_height-48,album_ui.soft,12);
    lv_obj_t *icon=album_text(badge,entry.kind==ALBUM_PHOTO?LV_SYMBOL_IMAGE:LV_SYMBOL_VIDEO,0,72,240,album_ui.muted);
    lv_obj_set_style_text_font(icon,glass_icon_font(48),0);
    lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *type=album_text(badge,local_album_format_label(format),16,154,208,album_ui.ink);
    lv_obj_set_style_text_font(type,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_align(type,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *caption=album_text(badge,"已识别文件格式",16,200,208,album_ui.muted);
    lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
    album_text(album_ui.view,"此格式需要转换后查看",304,44,width-400,album_ui.ink);
    lv_obj_t *filename=album_text(album_ui.view,title,304,82,width-400,album_ui.muted);
    lv_obj_set_height(filename,24);
    album_ui.note=album_text(album_ui.view,"当前设备尚未提供此格式的解码器。",304,134,width-400,album_ui.ink);
    album_box(album_ui.view,304,178,width-400,1,album_ui.soft,0);
    album_text(album_ui.view,"建议在电脑或手机上转换为",304,202,width-400,album_ui.muted);
    lv_obj_t *target=album_box(album_ui.view,304,240,width-400,56,album_ui.soft,10);
    album_text(target,entry.kind==ALBUM_PHOTO?"JPEG  /  PNG":"MP4  /  MOV · H.264 + AAC",20,18,width-440,album_ui.ink);
    album_text(album_ui.view,"原文件保留，可返回继续浏览或管理。",304,card_height-104,width-400,album_ui.muted);
    lv_obj_t *back=album_button(album_ui.view,album_ui.return_to_files?"返回文件管理":"返回相册",304,card_height-72,168,-1,true);
    lv_obj_set_style_bg_color(back,lv_color_hex(album_ui.soft),0);
    return;
  }
  album_button(album_ui.root,"全屏",width-96,10,80,-4,false);
  album_ui.view=album_box(album_ui.root,24,64,width-48,height-128,album_ui.light?0xefedf2:0x0e0f14,12);
  lv_obj_set_style_clip_corner(album_ui.view,true,0);
  album_ui.note=album_text(album_ui.view,"正在载入…",16,16,width-80,album_ui.muted);
  if(entry.kind==ALBUM_VIDEO)return;

}
static void album_grid(void)
{
  if(album_ui.direct_path[0]){album_ui.direct_path[0]=0;album_ui.need_list=true;}
  album_clear();album_ui.mode=0;album_ui.thumb_next=album_ui.count=0;
  lv_obj_update_layout(album_ui.root);
  int width=lv_obj_get_width(album_ui.root),height=lv_obj_get_height(album_ui.root);
  const char *names[]={"全部","照片","视频"};
  for(unsigned i=0;i<3;i++)album_button(album_ui.root,names[i],16+i*78,10,70,-10-(int)i,album_ui.filter==i);
  bool local=!strncmp(album_directory,ALBUM_LOCAL_ROOT,strlen(ALBUM_LOCAL_ROOT));
  album_button(album_ui.root,"本机",274,10,70,-30,local);
  album_button(album_ui.root,"SD 卡",352,10,70,-31,!local);
  album_text(album_ui.root,album_directory,450,22,width-686,album_ui.muted);
  album_button(album_ui.root,"上一级",width-216,10,104,-2,false);
  album_button(album_ui.root,"刷新",width-104,10,88,-3,false);
  album_ui.note=album_text(album_ui.root,"",24,height-40,width/2-116,album_ui.muted);
  album_ui.pager=glass_pager_create(album_ui.root,album_ui.soft,album_ui.ink,album_ui.muted,album_page_select,NULL);
  if(album_ui.need_list || !album_ui.listing){lv_label_set_text(album_ui.note,"正在读取 SD 卡…");return;}
  if(album_ui.listing->error){lv_label_set_text(album_ui.note,local?"本机还没有照片，可先拍一张再刷新":"SD 卡未就绪，插入 FAT32 卡后点刷新");return;}
  unsigned count=0,indices[ALBUM_FILES];
  for(unsigned i=0;i<album_ui.listing->count;i++){
    enum album_kind kind=album_ui.listing->entries[i].kind;
    if(kind==ALBUM_FOLDER || !album_ui.filter || kind==(int)album_ui.filter)indices[count++]=i;
  }
  unsigned pages=(count+ALBUM_PAGE-1)/ALBUM_PAGE;if(!pages)pages=1;
  if(album_ui.page>=pages)album_ui.page=pages-1;
  glass_pager_update(album_ui.pager,album_ui.page,pages,true);
  if(album_ui.pager)lv_obj_align(album_ui.pager,LV_ALIGN_BOTTOM_MID,0,-8);
  lv_label_set_text(album_ui.note,!count?"这里还没有照片或视频 · 支持多种图片与视频格式":
    album_ui.listing->truncated?"已载入前 256 项，可进入子目录继续查看":"轻点查看 · 照片按需加载");
  int cardw=(width-48-3*16)/4,cardh=(height-144-16)/2;
  for(unsigned slot=0;slot<ALBUM_PAGE && album_ui.page*ALBUM_PAGE+slot<count;slot++){
    unsigned index=indices[album_ui.page*ALBUM_PAGE+slot];album_ui.visible[slot]=index;album_ui.count++;
    struct album_entry *entry=&album_ui.listing->entries[index];
    lv_obj_t *card=album_box(album_ui.root,24+(slot%4)*(cardw+16),70+(slot/4)*(cardh+16),cardw,cardh,album_ui.paper,12);
    lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(card,album_clicked,LV_EVENT_CLICKED,(void *)(uintptr_t)(index+1));
    lv_obj_t *host=album_box(card,0,0,cardw,cardh-36,album_ui.soft,12);album_ui.thumb[slot]=host;
    lv_obj_remove_flag(host,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_clip_corner(host,true,0);
    lv_obj_t *icon=lv_label_create(host);album_ui.placeholder[slot]=icon;
    lv_label_set_text(icon,entry->kind==ALBUM_FOLDER?LV_SYMBOL_DIRECTORY:entry->kind==ALBUM_VIDEO?LV_SYMBOL_PLAY:LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(icon,glass_icon_font(32),0);lv_obj_set_style_text_color(icon,lv_color_hex(album_ui.muted),0);
    lv_obj_center(icon);lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);
    album_text(card,entry->name,10,cardh-27,cardw-20,album_ui.ink);
    enum album_format format=entry->format?entry->format:local_album_name_format(entry->name);
    if(entry->kind!=ALBUM_FOLDER){
      char badge[64];snprintf(badge,sizeof(badge),"%s%s",local_album_format_label(format),local_album_format_supported(format)?"":" · 需转换");
      lv_obj_t *tag=album_text(host,badge,8,6,cardw-16,album_ui.muted);
      lv_obj_set_style_bg_color(tag,lv_color_hex(album_ui.soft),0);lv_obj_set_style_bg_opa(tag,LV_OPA_90,0);
    }
  }
}
static void album_dispatch(void *unused)
{
  (void)unused;album_ui.queued=false;if(!album_ui.root)return;
  int action=album_ui.action;
  if(action>0)album_open_index(action-1);
  else if(action==-4)album_fullscreen();
  else if(action==-1){if(album_ui.return_to_files)album_ui.return_to_files(NULL);else album_grid();}
  else if(action==-2){
    const char *base=!strncmp(album_directory,ALBUM_LOCAL_ROOT,strlen(ALBUM_LOCAL_ROOT))?ALBUM_LOCAL_ROOT:ALBUM_ROOT;
    if(strcmp(album_directory,base)){char *slash=strrchr(album_directory,'/');if(slash)*slash=0;}
    album_ui.page=0;album_ui.need_list=true;album_grid();
  }
  else if(action==-3){album_ui.need_list=true;album_grid();}
  else if(action==-30 || action==-31){strcpy(album_directory,action==-30?ALBUM_LOCAL_ROOT:ALBUM_ROOT);album_ui.page=0;album_ui.need_list=true;album_grid();}
  else if(action<=-10&&action>=-12){album_ui.filter=-action-10;album_ui.page=0;album_grid();}
  else if(action<=-100){album_ui.page=(unsigned)(-action-100);album_grid();}
}
static void album_tick(lv_timer_t *timer)
{
  (void)timer;if(!album_ui.root)return;
  if(album_ui.mode==2)return;
  if(album_ui.need_list && !album_ui.pending_list){
    int ret=local_album_scan(album_directory);if(ret==-EBUSY)return;
    if(ret){lv_label_set_text(album_ui.note,"读取失败，请稍后点刷新");album_ui.need_list=false;if(album_ui.timer)lv_timer_pause(album_ui.timer);return;}
    album_ui.pending_list=true;
  }
  if(album_ui.pending_list){
    struct album_listing *listing=local_album_take_listing();if(!listing)return;
    free(album_ui.listing);album_ui.listing=listing;album_ui.need_list=false;album_grid();
  }
  if(album_ui.job){
    struct album_image image;if(!local_album_take_image(&image))return;
    if(album_ui.job==1){
      unsigned slot=album_ui.pending_slot;
      if(!image.error && image.pixels && image.width && image.height){lv_obj_t *canvas=album_canvas(album_ui.thumb[slot],&image);album_fit(canvas,album_ui.thumb[slot]);if(canvas)lv_obj_add_flag(album_ui.placeholder[slot],LV_OBJ_FLAG_HIDDEN);}
    }else if(!image.error && image.pixels && image.width && image.height){
      album_ui.canvas=album_canvas(album_ui.view,&image);album_fit(album_ui.canvas,album_ui.view);if(album_ui.canvas)lv_obj_add_flag(album_ui.note,LV_OBJ_FLAG_HIDDEN);else lv_label_set_text(album_ui.note,album_image_error(-ENOMEM));
    }else lv_label_set_text(album_ui.note,album_image_error(image.error));
    free(image.pixels);album_ui.job=0;
    /* One photo request per viewer entry, including failed decodes. */
    if(album_ui.mode==1){if(album_ui.timer)lv_timer_pause(album_ui.timer);return;}
  }
  lv_obj_update_layout(album_ui.root);
  char path[ALBUM_PATH];
  if(album_ui.mode==1){
    if(album_ui.image_attempted){if(album_ui.timer)lv_timer_pause(album_ui.timer);return;}
    if(!album_entry_path(path,sizeof(path),album_ui.selected))return;
    int ret=local_album_decode(path,lv_obj_get_width(album_ui.view),lv_obj_get_height(album_ui.view));
    if(!ret){album_ui.job=2;album_ui.image_attempted=true;}else if(ret!=-EBUSY){album_ui.image_attempted=true;lv_label_set_text(album_ui.note,album_image_error(ret));}
    return;
  }
  while(album_ui.thumb_next<album_ui.count){
    unsigned slot=album_ui.thumb_next,index=album_ui.visible[slot];
    if(album_ui.listing->entries[index].kind!=ALBUM_PHOTO){album_ui.thumb_next++;continue;}
    if(snprintf(path,sizeof(path),"%s/%s",album_ui.listing->path,album_ui.listing->entries[index].name)>=(int)sizeof(path)){album_ui.thumb_next++;continue;}
    int ret=local_album_decode(path,lv_obj_get_width(album_ui.thumb[slot]),lv_obj_get_height(album_ui.thumb[slot]));
    if(ret==-EBUSY)return;
    album_ui.thumb_next++;if(!ret){album_ui.pending_slot=slot;album_ui.job=1;return;}
  }
  if(album_ui.timer)lv_timer_pause(album_ui.timer);
}
static void album_deleted(lv_event_t *e)
{
  if(lv_event_get_target(e)!=album_ui.root)return;
  local_album_cancel();album_fullscreen_close();
  if(album_ui.queued)lv_async_call_cancel(album_dispatch,NULL);
  if(album_ui.timer)lv_timer_delete(album_ui.timer);
  free(album_ui.listing);memset(&album_ui,0,sizeof(album_ui));
}
static void album_create(lv_obj_t *parent,const lv_font_t *font,bool browse)
{
  if(album_ui.root)lv_obj_delete(album_ui.root);
  lv_obj_update_layout(parent);int width=lv_obj_get_width(parent),height=lv_obj_get_height(parent);
  lv_obj_t *root=lv_obj_create(parent);lv_obj_remove_style_all(root);lv_obj_set_size(root,width,height);
  memset(&album_ui,0,sizeof(album_ui));album_ui.root=root;album_ui.font=font;
  album_ui.light=lv_color_brightness(lv_obj_get_style_bg_color(parent,0))>128;
  album_ui.background=album_ui.light?0xf8f7fa:0x15131d;album_ui.paper=album_ui.light?0xffffff:0x24222c;
  album_ui.soft=album_ui.light?0xeeecf2:0x2c2a35;album_ui.ink=album_ui.light?0x292734:0xefedf5;
  album_ui.muted=album_ui.light?0x85818e:0xa19ba9;
  lv_obj_set_style_bg_color(root,lv_color_hex(album_ui.background),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
  lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_add_event_cb(root,album_deleted,LV_EVENT_DELETE,NULL);
  lv_obj_add_event_cb(root,album_gesture,LV_EVENT_GESTURE,NULL);
  album_ui.need_list=browse;
  if(browse)album_grid();
  album_ui.timer=lv_timer_create(album_tick,100,NULL);
  if(browse)album_tick(NULL);else if(album_ui.timer)lv_timer_pause(album_ui.timer);
}
void local_album_show(lv_obj_t *parent,const lv_font_t *font)
{album_create(parent,font,true);}
void local_album_show_camera(lv_obj_t *parent,const lv_font_t *font)
{
  const char *directory=ALBUM_LOCAL_ROOT;
  struct stat st;
  bool mounted=true;
#ifdef __NuttX__
  struct statfs fs;
  mounted=statfs(ALBUM_ROOT,&fs)==0 &&
    (fs.f_type==MSDOS_SUPER_MAGIC || fs.f_type==FATFS_SUPER_MAGIC);
#endif
  if(mounted && !lstat(ALBUM_ROOT "/photos",&st) && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
    directory=ALBUM_ROOT "/photos";
  snprintf(album_directory,sizeof(album_directory),"%s",directory);
  album_create(parent,font,true);
}
static void album_open_error(const char *message)
{
  album_clear();album_ui.need_list=false;album_ui.mode=1;album_ui.image_attempted=true;
  lv_obj_update_layout(album_ui.root);
  int width=lv_obj_get_width(album_ui.root);
  album_button(album_ui.root,album_ui.return_to_files?"返回文件":"返回相册",16,10,112,-1,false);
  album_text(album_ui.root,message,24,88,width-48,album_ui.muted);
  if(album_ui.timer)lv_timer_pause(album_ui.timer);
}
void local_album_open_path(const char *path)
{
  if(!album_ui.root)return;
  if(!local_album_valid_path(path)){album_open_error("文件路径无效");return;}
  int kind=local_album_kind(path);if(kind<0){album_open_error("此文件不提供媒体预览");return;}
  const char *name=strrchr(path,'/');
  if(!name || !name[1]){album_open_error("文件名无效");return;}
  struct album_listing *listing=calloc(1,sizeof(*listing));
  if(!listing){album_open_error("内存不足，无法打开文件");return;}
  memcpy(listing->path,path,name-path);
  /* Direct viewing keeps the full path separately. The bounded grid entry
   * name is only a placeholder and is never used to decode the direct file. */
  snprintf(listing->entries[0].name,sizeof(listing->entries[0].name),"%s",name+1);
  listing->entries[0].kind=kind;listing->entries[0].format=local_album_name_format(path);listing->count=1;
  free(album_ui.listing);album_ui.listing=listing;album_ui.need_list=false;
  strcpy(album_ui.direct_path,path);
  if(!album_ui.return_to_files)strcpy(album_directory,listing->path);
  album_open_index(0);
}
void local_album_view(lv_obj_t *parent,const lv_font_t *font,const char *path,lv_event_cb_t back)
{
  album_create(parent,font,false);album_ui.return_to_files=back;
  local_album_open_path(path);
}
