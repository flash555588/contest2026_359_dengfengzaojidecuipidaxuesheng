/* SPDX-License-Identifier: Apache-2.0 */
/* Immutable UI snapshot copied under a separate lock; camera workers use no
 * LVGL calls. Composition happens after photo/video saves, display only. */
static pthread_mutex_t wake_overlay_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {uint8_t *data;size_t capacity;unsigned width,height,stride;int x,y;} wake_overlay;
int qpk_runtime_wake_overlay(const uint8_t *bgra,unsigned width,unsigned height,unsigned stride,int x,int y)
{
  if(bgra&&(!width||!height||width>400||height>360||stride<width*4||stride>4096))return -EINVAL;
  pthread_mutex_lock(&wake_overlay_lock);
  if(!bgra){free(wake_overlay.data);memset(&wake_overlay,0,sizeof(wake_overlay));}
  else{
    size_t bytes=(size_t)stride*height;
    uint8_t *data=bytes>wake_overlay.capacity?realloc(wake_overlay.data,bytes):wake_overlay.data;
    if(!data){pthread_mutex_unlock(&wake_overlay_lock);return -ENOMEM;}
    memcpy(data,bgra,bytes);wake_overlay.data=data;wake_overlay.capacity=bytes;wake_overlay.width=width;
    wake_overlay.height=height;wake_overlay.stride=stride;wake_overlay.x=x;wake_overlay.y=y;
  }
  pthread_mutex_unlock(&wake_overlay_lock);return 0;
}
static void qpk_wake_overlay_blit(uint16_t *pixels,unsigned width,unsigned height,unsigned stride)
{
  pthread_mutex_lock(&wake_overlay_lock);
  for(unsigned y=0;wake_overlay.data&&y<wake_overlay.height;y++){
    int dy=wake_overlay.y+(int)y;if(dy<0||dy>=(int)height)continue;
    const uint8_t *row=wake_overlay.data+y*wake_overlay.stride;
    for(unsigned x=0;x<wake_overlay.width;x++){
      int dx=wake_overlay.x+(int)x;if(dx<0||dx>=(int)width)continue;
      unsigned alpha=row[x*4+3];if(!alpha)continue;
      uint16_t *dest=pixels+dy*stride+dx,old=*dest;
      unsigned r=(old>>11)*255/31,g=((old>>5)&63)*255/63,b=(old&31)*255/31;
      r=(row[x*4+2]*alpha+r*(255-alpha)+127)/255;
      g=(row[x*4+1]*alpha+g*(255-alpha)+127)/255;
      b=(row[x*4]*alpha+b*(255-alpha)+127)/255;
      *dest=(r>>3)<<11|(g>>2)<<5|(b>>3);
    }
  }
  pthread_mutex_unlock(&wake_overlay_lock);
}
