/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <nuttx/video/fb.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#if defined(__NuttX__) && defined(CONFIG_ESPRESSIF_MIPI_DSI)
#include <nuttx/video/camera_timing.h>
#endif

/* This NuttX framebuffer upper half can discard the driver's pan result.
 * Confirm actual scanout before returning its previous page to CSI DMA. */
static int qpk_camera_present(int fd,const struct fb_planeinfo_s *requested)
{
  int ret;
#if defined(__NuttX__) && defined(CONFIG_ESPRESSIF_MIPI_DSI)
  uint64_t started=esp_camera_timing_now();
#endif
  ret=ioctl(fd,FBIOPAN_DISPLAY,(uintptr_t)requested);
  struct fb_planeinfo_s actual={.display=0};
  if(ret>=0)ret=ioctl(fd,FBIOGET_PLANEINFO,(uintptr_t)&actual);
  if(ret>=0&&(actual.xoffset!=requested->xoffset||actual.yoffset!=requested->yoffset)){errno=EIO;ret=-1;}
  int saved_errno=errno;
#if defined(__NuttX__) && defined(CONFIG_ESPRESSIF_MIPI_DSI)
  esp_camera_timing_pan(started,ret<0?saved_errno:0);
#endif
  errno=saved_errno;return ret<0?-1:0;
}

/* Camera and LVGL share three scanout pages, but LVGL draws in pages 0/1.
 * After the producer has stopped/joined, park the last image on page 2 before
 * publishing GUI ownership. A successful pan must be read back, not assumed.
 */
static inline int qpk_camera_park_scanout(int fd,
    const struct fb_planeinfo_s *expected,unsigned height,size_t bytes,
    int (*clean)(uint16_t *,size_t))
{
  struct fb_planeinfo_s actual={.display=0};
  if(!height||!bytes||!clean||!expected||!expected->fbmem){errno=EINVAL;return -1;}
  if(ioctl(fd,FBIOGET_PLANEINFO,(uintptr_t)&actual)<0)return -1;
  if(actual.fbmem!=expected->fbmem||actual.stride!=expected->stride||
     actual.fblen/3<bytes||actual.yres_virtual/3<height||
     actual.xoffset||actual.yoffset%height||actual.yoffset/height>=3||
     bytes/height!=actual.stride||bytes%height){errno=EIO;return -1;}
  unsigned page=actual.yoffset/height;
  if(page==2)return 0;
  uint8_t *base=actual.fbmem;
  uint16_t *parked=(uint16_t *)(base+2*bytes);
  memcpy(parked,base+page*bytes,bytes);
  int ret=clean(parked,bytes);
  if(ret<0){errno=-ret;return -1;}
  actual.yoffset=2*height;
  return qpk_camera_present(fd,&actual);
}
