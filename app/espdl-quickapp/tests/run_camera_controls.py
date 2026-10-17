#!/usr/bin/env python3
"""Check production RGB565 camera controls under ASan/UBSan, without hardware."""
from pathlib import Path
import argparse,subprocess,tempfile

p=argparse.ArgumentParser();p.add_argument('--output',type=Path);a=p.parse_args()
out=a.output or Path(tempfile.mkdtemp(prefix='camera-controls-'));out.mkdir(parents=True,exist_ok=True)
source=(Path(__file__).resolve().parents[1]/'overlay/apps/system/desktop/qpk_runtime.c').read_text()
start=source.index('#define QPK_CAMERA_CORNER_RADIUS')
end=source.index('static uint64_t qpk_now_ms(void)\n{',start)
prelude='''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#define QPK_CAMERA_RAW_HEIGHT 600
#define QPK_CAMERA_BAR_LEFT 896
#define QPK_CAMERA_PHOTO_NOTICE_MS 2000
static pthread_mutex_t g_camera_lock=PTHREAD_MUTEX_INITIALIZER;
static int g_photo_status;
static uint64_t g_photo_status_ms,now=100;
static uint64_t qpk_now_ms(void){return now;}
static bool video;
static int recording;
static bool qpk_video_selected(void){return video;}
static int qpk_video_status(unsigned *elapsed){*elapsed=65000;return recording;}
'''
test='''
int main(void){
  uint16_t *allocation=malloc((1024*600+2)*sizeof(uint16_t));assert(allocation);
  uint16_t *pixels=allocation+1;allocation[0]=allocation[1024*600+1]=0x55aa;
  for(int i=0;i<1024*600;i++)pixels[i]=0x1234;
  assert(qpk_camera_bar_blit(pixels)==0);
  assert(pixels[200*1024+512]==0x1234); /* Controls preserve the image interior. */
  assert(pixels[599*1024+512]==0x1234); /* No bottom bar covers the landscape image. */
  assert(pixels[599*1024+960]==0);
  assert(pixels[300*1024+960]==0xffff);
  assert(pixels[300*1024+997]==0); /* Black gap separates white shutter rings. */
  assert(pixels[300*1024+1001]==0xffff);
  assert(pixels[540*1024+960]==0xffff); /* Close remains visible and aligned. */
  assert((QPK_CAMERA_BAR_LEFT*2)%128==0);
  g_photo_status=-5;g_photo_status_ms=now;
  assert(qpk_camera_bar_blit(pixels)==-5);
  assert(pixels[300*1024+960]==qpk_camera_rgb565(0xe34f4f));
  g_photo_status=2;assert(qpk_camera_bar_blit(pixels)==2);
  assert(pixels[300*1024+960]==0xffff);
  assert(pixels[378*1024+960]==qpk_camera_rgb565(0x2fbf8f));
  now+=2001;assert(qpk_camera_bar_blit(pixels)==0);
  video=true;recording=2;qpk_camera_bar_blit(pixels);
  assert(pixels[300*1024+960]==qpk_camera_rgb565(0xff453a));
  assert(pixels[300*1024+997]==0);
  assert(allocation[0]==0x55aa&&allocation[1024*600+1]==0x55aa);
  free(allocation);puts("CAMERA_CONTROLS_OK: actual RGB565 renderer, black/white rings, close alignment, transient status, frame boundaries");
}
'''
unit=out/'camera-controls.c';unit.write_text(prelude+source[start:end]+test)
binary=out/'camera-controls'
subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',str(unit),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)

usb=(Path(__file__).resolve().parents[1]/'overlay/apps/system/desktop/qpk_camera_usb.inc').read_text()
flush=usb[usb.index('static int qpk_camera_clean_frame'):usb.index('/* Runs entirely outside LVGL.')]
cache_test='''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
static uintptr_t next,end;
static unsigned calls,fail;
static int esp_mipi_dsi_flush_framebuffer(void *address,size_t bytes){
  assert((uintptr_t)address==next&&bytes>0&&bytes<=16384&&next+bytes<=end);
  calls++;if(fail&&calls==fail)return -5;next+=bytes;return 0;
}
'''+flush+'''
int main(void){
  size_t bytes=1024*600*2;uint16_t *frame=malloc(bytes);assert(frame);
  next=(uintptr_t)frame;end=next+bytes;
  assert(qpk_camera_clean_frame(frame,bytes)==0&&next==end&&calls==75);
  next=(uintptr_t)frame;calls=0;fail=3;
  assert(qpk_camera_clean_frame(frame,bytes)==-5&&calls==3);
  free(frame);puts("CAMERA_CACHE_OK: actual display writeback invoked, bounded 16KiB chunks, full frame covered, failure propagated");
}
'''
unit=out/'camera-cache.c';unit.write_text(cache_test);binary=out/'camera-cache'
subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(unit),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
