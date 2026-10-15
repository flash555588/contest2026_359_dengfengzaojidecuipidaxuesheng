/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_music.h"
#include "glass_lock_photo.h"
struct music_cover_view {
  lv_obj_t *host,*canvas;unsigned owner,track_id;
  bool requested,done;char url[1024];
};
static void music_cover_deleted(lv_event_t *e)
{
  lv_image_cache_drop(lv_canvas_get_image(lv_event_get_target(e)));
  free(lv_event_get_user_data(e));
}
static void music_cover_reset(struct music_cover_view *view)
{
  glass_lock_cover_cancel(view->owner);
  if(view->canvas){lv_obj_delete(view->canvas);view->canvas=NULL;}
  view->requested=view->done=false;
}
static void music_cover_tick(struct music_cover_view *view,const struct music_now_playing *now)
{
  if(!view->host)return;
  if(view->track_id!=now->track_id || strcmp(view->url,now->cover_url)) {
    music_cover_reset(view);view->track_id=now->track_id;memcpy(view->url,now->cover_url,sizeof(view->url));
  }
  if(!view->url[0] || view->done)return;
  if(!view->requested) {
    int ret=glass_lock_cover_request(view->owner,view->url);
    if(ret==-EBUSY)return;
    if(ret){view->done=true;return;}
    view->requested=true;
  }
  struct glass_lock_photo cover;if(!glass_lock_cover_take(view->owner,&cover))return;
  view->done=true;
  if(cover.error || !cover.pixels){free(cover.pixels);return;}
  lv_obj_update_layout(view->host);
  int w=lv_obj_get_width(view->host),h=lv_obj_get_height(view->host);
  unsigned scale=((unsigned)w*256+cover.width-1)/cover.width;
  if((uint64_t)cover.height*scale/256<(unsigned)h)scale=((unsigned)h*256+cover.height-1)/cover.height;
  lv_obj_t *canvas=lv_canvas_create(view->host);view->canvas=canvas;
  lv_canvas_set_buffer(canvas,cover.pixels,cover.width,cover.height,LV_COLOR_FORMAT_RGB565);
  lv_image_set_pivot(canvas,0,0);lv_image_set_scale(canvas,scale);
  lv_obj_set_pos(canvas,(w-(int)(cover.width*scale/256))/2,(h-(int)(cover.height*scale/256))/2);
  lv_obj_remove_flag(canvas,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_clip_corner(view->host,true,0);
  lv_obj_add_event_cb(canvas,music_cover_deleted,LV_EVENT_DELETE,cover.pixels);
}
