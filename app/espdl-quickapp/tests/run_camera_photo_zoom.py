#!/usr/bin/env python3
"""Check production RGB565 photo crops and BMP metadata under ASan/UBSan."""
from pathlib import Path
import argparse
import subprocess

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
desktop=Path(__file__).resolve().parents[1]/'overlay/apps/system/desktop'
source=(desktop/'qpk_runtime.c').read_text()
function=source[source.index('static uint32_t g_photo_job;'):source.index('bool qpk_runtime_camera_capture_request(')]
prelude=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "camera_color.h"
#include "qpk_photo_store.c"
#define QPK_CAMERA_RAW_WIDTH 1024
#define QPK_CAMERA_RAW_HEIGHT 600
#define QPK_CAMERA_FRAME_SIZE (1024*600*2)
static pthread_mutex_t g_camera_lock=PTHREAD_MUTEX_INITIALIZER;
static bool g_photo_pending;
static unsigned zoom=100,style,aspect;
static unsigned qpk_camera_aspect(void){return aspect;}
static unsigned qpk_camera_style(void){return style;}
static int g_photo_status;
static char g_photo_path[128];
static struct {bool stop_requested;} g_camera;
static unsigned qpk_camera_zoom_percent(void){return zoom;}
static void qpk_camera_set_photo_status(int status){g_photo_status=status;}
'''
test=r'''
static uint32_t little(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wait_photo(void){
  for(unsigned i=0;i<5000;i++){
    pthread_mutex_lock(&g_camera_lock);qpk_camera_photo_poll_locked();
    bool done=!g_photo_job;pthread_mutex_unlock(&g_camera_lock);
    if(done)return;usleep(1000);
  }abort();
}
int main(void){
  assert(qpk_photo_store_initialize()==0);
  uint16_t *frame=malloc(1024*600*2);assert(frame);
  for(unsigned y=0;y<600;y++)for(unsigned x=0;x<1024;x++)frame[y*1024+x]=(uint16_t)((y<<5)^x);
  const unsigned values[]={100,150,200,300};
  for(style=0;style<CAMERA_COLOR_STYLE_COUNT;style++)for(unsigned width=896;width<=1024;width+=128)for(unsigned z=0;z<4;z++){
    zoom=values[z];g_photo_pending=true;g_photo_status=1;qpk_camera_save(frame,width);wait_photo();
    assert(g_photo_status==2&&!g_photo_pending);
    FILE *f=fopen(g_photo_path,"rb");assert(f);uint8_t h[66];assert(fread(h,1,66,f)==66);
    unsigned height=600*512/width;
    assert(h[0]=='B'&&h[1]=='M'&&little(h+10)==66&&little(h+18)==512);
    assert(little(h+22)==(uint32_t)-height&&little(h+2)==66+height*1024);
    unsigned cw=width*100/zoom,ch=600*100/zoom,cx=(width-cw)/2,cy=(600-ch)/2;
    for(unsigned y=0;y<height;y+=height/9)for(unsigned x=0;x<512;x+=51){
      assert(!fseek(f,66+(y*512+x)*2,SEEK_SET));uint8_t pixel[2];assert(fread(pixel,1,2,f)==2);
      unsigned value=pixel[0]|pixel[1]<<8;
      assert(value==camera_color_pixel(frame[(cy+y*ch/height)*1024+cx+x*cw/512],style));
    }
    fclose(f);unsigned before=sequence;qpk_camera_save(frame,width);
    assert(sequence==before); /* No second photo without a shutter request. */
  }
  style=0;
  for(aspect=1;aspect<=4;aspect++)for(unsigned width=896;width<=1024;width+=128)for(unsigned z=0;z<4;z++){
    zoom=values[z];g_photo_pending=true;g_photo_status=1;
    qpk_camera_save_region(frame,width,width==896?640:1024,width==896?480:600);wait_photo();assert(g_photo_status==2);
    FILE *f=fopen(g_photo_path,"rb");assert(f);uint8_t h[66];assert(fread(h,1,66,f)==66);
    unsigned height=aspect==1?320:aspect==2?288:aspect==3?384:512;
    assert(little(h+18)==512&&little(h+22)==(uint32_t)-height&&little(h+2)==66+height*1024);
    struct camera_reference_rect crop=camera_frame_crop(width,600,width==896?640:1024,width==896?480:600,zoom,aspect);
    for(unsigned y=0;y<height;y+=height/7)for(unsigned x=0;x<512;x+=51){
      assert(!fseek(f,66+(y*512+x)*2,SEEK_SET));uint8_t p[2];assert(fread(p,1,2,f)==2);
      assert((p[0]|p[1]<<8)==frame[(crop.y+y*crop.h/height)*1024+crop.x+x*crop.w/512]);
    }fclose(f);
  }
  free(frame);qpk_photo_store_shutdown();puts("CAMERA_PHOTO_ZOOM_OK: 48 native style/zoom crops and 32 explicit aspect/zoom photos, matching pixels and BMP dimensions");
}
'''
unit=out/'photo-zoom.c';unit.write_text(prelude+function+test)
binary=out/'photo-zoom'
subprocess.run(['gcc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-I',str(desktop),
                '-fno-omit-frame-pointer',f'-DQPK_PHOTO_DIR="{out}/photos"',str(unit),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
