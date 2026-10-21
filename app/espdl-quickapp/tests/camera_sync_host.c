/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_cache.h"
#include "qpk_camera_present.h"

int esp_camera_invalidate_framebuffer(void *,size_t);
static unsigned calls,fail_call,pan_calls,get_calls;
static unsigned char *cpu,*dma;
static size_t synced;
static int pan_error,get_error,stale;
static struct fb_planeinfo_s scanout;

int esp_cache_msync(void *address,size_t bytes,int flags)
{
  assert(flags==(ESP_CACHE_MSYNC_FLAG_DIR_M2C|ESP_CACHE_MSYNC_FLAG_TYPE_DATA));
  assert(address==cpu+synced && bytes && bytes<=16384);
  assert(bytes%CONFIG_ESPRESSIF_CACHE_L2_CACHE_LINE_SIZE==0);
  assert((uintptr_t)address%CONFIG_ESPRESSIF_CACHE_L2_CACHE_LINE_SIZE==0);
  calls++;
  if(calls==fail_call)return 1;
  /* Model stale CPU lines after DMA, refreshed only by M2C invalidation. */
  memcpy(address,dma+synced,bytes);synced+=bytes;
  return ESP_OK;
}

int ioctl(int fd,unsigned long request,...)
{
  assert(fd==7);
  va_list ap;va_start(ap,request);
  struct fb_planeinfo_s *plane=(void *)va_arg(ap,uintptr_t);va_end(ap);
  if(request==FBIOPAN_DISPLAY){
    pan_calls++;
    if(pan_error){errno=EBUSY;return -1;}
    if(!stale)scanout=*plane;
  }else{
    assert(request==FBIOGET_PLANEINFO);get_calls++;
    if(get_error){errno=ENODEV;return -1;}
    *plane=scanout;
  }
  return 0;
}

int main(void)
{
  const size_t bytes=1024*600*2,line=CONFIG_ESPRESSIF_CACHE_L2_CACHE_LINE_SIZE;
  assert(posix_memalign((void **)&cpu,line,bytes)==0);
  assert(posix_memalign((void **)&dma,line,bytes)==0);
  memset(cpu,0x17,bytes);memset(dma,0xa5,bytes);
  assert(esp_camera_invalidate_framebuffer(NULL,bytes)==-EINVAL);
  assert(esp_camera_invalidate_framebuffer(cpu,0)==-EINVAL);
  assert(esp_camera_invalidate_framebuffer(cpu+1,bytes)==-EINVAL);
  assert(esp_camera_invalidate_framebuffer(cpu,bytes-1)==-EINVAL);
  assert(calls==0);
  assert(esp_camera_invalidate_framebuffer(cpu,bytes)==0);
  assert(calls==(bytes+16383)/16384 && synced==bytes);
  assert(memcmp(cpu,dma,bytes)==0);
  calls=0;synced=0;fail_call=3;memset(cpu,0x17,bytes);
  assert(esp_camera_invalidate_framebuffer(cpu,bytes)==-EIO);
  assert(calls==3 && synced==32768 && cpu[synced]==0x17);
  struct fb_planeinfo_s target={.yoffset=600};
  assert(qpk_camera_present(7,&target)==0 && scanout.yoffset==600);
  stale=1;target.yoffset=1200;
  assert(qpk_camera_present(7,&target)==-1 && errno==EIO);
  target.yoffset=600;target.xoffset=1;
  assert(qpk_camera_present(7,&target)==-1 && errno==EIO);
  get_error=1;
  assert(qpk_camera_present(7,&target)==-1 && errno==ENODEV);
  unsigned previous=get_calls;pan_error=1;
  assert(qpk_camera_present(7,&target)==-1 && errno==EBUSY && get_calls==previous);
  assert(pan_calls==5);
  free(cpu);free(dma);
  printf("camera cache/scanout ownership passed (cache line %zu)\n",line);
  return 0;
}
