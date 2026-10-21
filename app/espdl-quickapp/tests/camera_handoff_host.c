/* Exercise the production park helper against current scanout ownership. */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qpk_camera_present.h"
static unsigned char pages[3][32];
static struct fb_planeinfo_s current;
static unsigned pans,cleans;
static bool fail_clean,fail_pan,stale,get_failure;
int ioctl(int fd,unsigned long request,...)
{
  assert(fd==9);va_list args;va_start(args,request);
  struct fb_planeinfo_s *plane=(void *)va_arg(args,uintptr_t);va_end(args);
  if(request==FBIOGET_PLANEINFO) {
    if(get_failure){errno=ENODEV;return -1;}
    *plane=current;return 0;
  }
  assert(request==FBIOPAN_DISPLAY);pans++;
  assert(plane->yoffset==8&&cleans);
  if(fail_pan){errno=EBUSY;return -1;}
  if(!stale)current=*plane;
  return 0;
}
static int clean(uint16_t *memory,size_t bytes)
{
  assert((void *)memory==pages[2]&&bytes==32&&current.yoffset!=8);
  cleans++;return fail_clean?-EIO:0;
}
int main(void)
{
  struct fb_planeinfo_s expected={.fbmem=pages,.stride=8,.fblen=sizeof(pages),.yres_virtual=12};
  for(unsigned page=0;page<3;page++) {
    current=expected;current.yoffset=page*4;pans=cleans=0;
    for(unsigned i=0;i<3;i++)memset(pages[i],0x20+i,32);
    assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==0);
    assert(current.yoffset==8&&pans==(page!=2)&&cleans==(page!=2));
    for(unsigned i=0;i<32;i++)assert(pages[2][i]==0x20+page);
  }
  current=expected;fail_clean=true;pans=0;
  assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==-1&&errno==EIO&&pans==0&&current.yoffset==0);
  fail_clean=false;fail_pan=true;
  assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==-1&&errno==EBUSY&&current.yoffset==0);
  fail_pan=false;stale=true;
  assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==-1&&errno==EIO&&current.yoffset==0);
  stale=false;get_failure=true;
  assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==-1&&errno==ENODEV);
  get_failure=false;current.yoffset=1;pans=cleans=0;
  assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==-1&&pans==0&&cleans==0);
  current=expected;current.fblen=95;
  assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==-1&&pans==0);
  current=expected;current.fbmem=pages[1];
  assert(qpk_camera_park_scanout(9,&expected,4,32,clean)==-1&&pans==0);
  assert(qpk_camera_park_scanout(9,NULL,4,32,clean)==-1&&errno==EINVAL);
  puts("camera handoff: pages 0/1/2, copied last image, verified pan, clean/IO/stale/geometry errors passed");
  return 0;
}
