/* SPDX-License-Identifier: Apache-2.0
 * Decoder-only LodePNG with a per-job heap budget. The album serializes jobs.
 * No LVGL allocator or UI object is used from the image worker.
 */
#include "local_album.h"
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define LODEPNG_NO_COMPILE_ENCODER
#define LODEPNG_NO_COMPILE_DISK
#define LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
#define LODEPNG_NO_COMPILE_ALLOCATORS
#include "local_vendor/lodepng/lodepng.h"
union png_allocation {max_align_t align;size_t size;};
static size_t png_allocated;
#define PNG_HEAP_LIMIT (8u*1024u*1024u)
void *lodepng_malloc(size_t bytes)
{
  if(bytes>PNG_HEAP_LIMIT || png_allocated>PNG_HEAP_LIMIT-bytes)return NULL;
  union png_allocation *p=malloc(sizeof(*p)+bytes);if(!p)return NULL;
  p->size=bytes;png_allocated+=bytes;return p+1;
}
void lodepng_free(void *ptr)
{if(ptr){union png_allocation *p=(union png_allocation *)ptr-1;png_allocated-=p->size;free(p);}}
void *lodepng_realloc(void *ptr,size_t bytes)
{
  if(!ptr)return lodepng_malloc(bytes);
  union png_allocation *p=(union png_allocation *)ptr-1;
  if(bytes>PNG_HEAP_LIMIT || png_allocated-p->size>PNG_HEAP_LIMIT-bytes)return NULL;
  size_t old=p->size;p=realloc(p,sizeof(*p)+bytes);if(!p)return NULL;
  p->size=bytes;png_allocated=png_allocated-old+bytes;return p+1;
}
#include "local_vendor/lodepng/lodepng.c"
int local_album_png(const unsigned char *input,size_t size,unsigned maxw,unsigned maxh,struct album_image *out)
{
  LodePNGState state;lodepng_state_init(&state);unsigned w=0,h=0;unsigned char *rgba=NULL;
  unsigned error=lodepng_inspect(&w,&h,&state,input,size);int ret=-EBADMSG;
  if(error)goto done;
  if(!w||!h||w>4096||h>4096||(uint64_t)w*h>1024u*1024u){ret=-E2BIG;goto done;}
  state.decoder.zlibsettings.max_output_size=8u*1024u*1024u;
  error=lodepng_decode(&rgba,&w,&h,&state,input,size);
  if(error){ret=error==83?-ENOMEM:-EBADMSG;goto done;}
  out->width=w;out->height=h;
  if(out->width>maxw){out->width=maxw;out->height=(uint64_t)h*maxw/w;}
  if(out->height>maxh){out->height=maxh;out->width=(uint64_t)w*maxh/h;}
  if(!out->width)out->width=1;
  if(!out->height)out->height=1;
  out->pixels=malloc((size_t)out->width*out->height*2);if(!out->pixels){ret=-ENOMEM;goto done;}
  for(unsigned y=0;y<out->height;y++)for(unsigned x=0;x<out->width;x++){
    const unsigned char *p=rgba+((uint64_t)y*h/out->height*w+(uint64_t)x*w/out->width)*4;
    unsigned bg=((x/8+y/8)&1)?238:255,a=p[3];
    unsigned r=(p[0]*a+bg*(255-a)+127)/255,g=(p[1]*a+bg*(255-a)+127)/255,b=(p[2]*a+bg*(255-a)+127)/255;
    out->pixels[y*out->width+x]=((r>>3)<<11)|((g>>2)<<5)|(b>>3);
  }
  ret=0;
done:
  lodepng_free(rgba);lodepng_state_cleanup(&state);return ret;
}
