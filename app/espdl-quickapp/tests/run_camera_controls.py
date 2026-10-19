#!/usr/bin/env python3
"""Check production RGB565 camera controls under ASan/UBSan, without hardware."""
from pathlib import Path
import argparse,subprocess,tempfile

p=argparse.ArgumentParser();p.add_argument('--output',type=Path);a=p.parse_args()
out=a.output or Path(tempfile.mkdtemp(prefix='camera-controls-'));out.mkdir(parents=True,exist_ok=True)
source=(Path(__file__).resolve().parents[1]/'overlay/apps/system/desktop/qpk_runtime.c').read_text()
start=source.index('static unsigned g_camera_zoom_percent=100;')
end=source.index('static uint64_t qpk_now_ms(void)\n{',start)
prelude='''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "camera_reference_pixels.h"
#include "camera_reference_layout.h"
#include "camera_reference_controls.h"
#include "camera_color.h"
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
int main(int argc,char **argv){
  assert(argc==2);
  uint16_t *allocation=malloc((1024*600+2)*sizeof(uint16_t));assert(allocation);
  uint16_t *pixels=allocation+1;allocation[0]=allocation[1024*600+1]=0x55aa;
  for(int i=0;i<1024*600;i++)pixels[i]=0x1234;
  assert(qpk_camera_bar_blit(pixels,1024)==0);
  assert(pixels[200*1024+512]==0x1234); /* Visible image interior survives. */
  assert(pixels[599*1024+512]==0&&pixels[300*1024+20]==0);
  assert(pixels[74*1024+300]==qpk_camera_rgb565(0x676268));
  assert(pixels[26*1024+28]==qpk_camera_rgb565(0x35ca70));
  const struct camera_reference_tile *shutter=&camera_reference_tiles[CAMERA_REFERENCE_SHUTTER_TILE];
  for(int y=0;y<shutter->h;y++)assert(!memcmp(pixels+(shutter->y+y)*1024+shutter->x,
                                     shutter->pixels+y*shutter->w,shutter->w*2));
  assert(pixels[300*1024+892]==qpk_camera_rgb565(0xef3829));
  for(int i=0;i<shutter->w*shutter->h;i++)assert(shutter->pixels[i]!=0xffff);
  assert(camera_reference_hit(892,300)==1&&camera_reference_hit(1008,300)==2);
  assert(camera_reference_hit(90,300)==0&&camera_reference_hit(892,60)==3);
  assert(camera_reference_hit(90,48)==0&&camera_reference_hit(90,550)==0&&camera_reference_hit(732,540)==0);
  assert(camera_reference_hit(732,64)==6&&camera_reference_hit(892,540)==7);
  assert(!qpk_camera_style_set(3));
  assert(camera_color_pixel(0xf800,0)==0xf800);
  assert(camera_color_pixel(0xf800,2)!=0xf800);
  assert(qpk_camera_style_set(2)&&qpk_camera_style()==2);
  assert(camera_color_pixel(0xffff,2)==0xffff&&camera_color_pixel(0,2)==0);
  assert(qpk_camera_style_set(0));
  assert(camera_reference_hit(892,160)==4&&camera_reference_hit(810,300)==0&&camera_reference_hit(500,300)==0);
  FILE *screenshot=fopen(argv[1],"wb");assert(screenshot);
  assert(fwrite(pixels,2,1024*600,screenshot)==1024*600);fclose(screenshot);
  g_photo_status=-5;g_photo_status_ms=now;
  assert(qpk_camera_bar_blit(pixels,1024)==-5);
  assert(pixels[364*1024+892]==qpk_camera_rgb565(0xe34f4f));
  g_photo_status=2;assert(qpk_camera_bar_blit(pixels,1024)==2);
  assert(pixels[364*1024+892]==qpk_camera_rgb565(0x2fbf8f));
  now+=2001;assert(qpk_camera_bar_blit(pixels,1024)==0);
  video=true;recording=2;qpk_camera_bar_blit(pixels,1024);
  assert(pixels[300*1024+892]==0xffff);
  assert(allocation[0]==0x55aa&&allocation[1024*600+1]==0x55aa);
  assert(!qpk_camera_zoom_set(0)&&!qpk_camera_zoom_set(999));
  assert(camera_reference_hit(732,300)==5);
  assert(camera_reference_zoom_drag(0,390,330)==1);
  assert(camera_reference_zoom_drag(0,390,100)==3);
  assert(camera_reference_zoom_drag(3,100,500)==0);
  assert(camera_reference_zoom_tap(268)==2);
  for(unsigned style=0;style<3;style++)for(unsigned width=896;width<=1024;width+=128)for(unsigned index=0;index<4;index++){
    assert(qpk_camera_style_set(style));
    unsigned zoom=camera_reference_zoom_values[index];assert(qpk_camera_zoom_set(zoom));
    assert(qpk_camera_zoom_percent()==zoom);
    for(unsigned y=0;y<600;y++)for(unsigned x=0;x<width;x++)pixels[y*1024+x]=(uint16_t)((y<<5)^x);
    unsigned height=600*570/width,top=22+(556-height)/2;
    unsigned crop_w=width*100/zoom,crop_h=600*100/zoom;
    unsigned crop_x=(width-crop_w)/2,crop_y=(600-crop_h)/2;
    qpk_camera_reference_prepare_view(pixels,width,zoom);
    for(unsigned y=0;y<height;y+=height/11)for(unsigned x=0;x<570;x+=51){
      unsigned sy=crop_y+y*crop_h/height,sx=crop_x+x*crop_w/570;
      assert(pixels[(top+y)*1024+126+x]==camera_color_pixel((uint16_t)((sy<<5)^sx),style));
    }
    assert(allocation[0]==0x55aa&&allocation[1024*600+1]==0x55aa);
  }
  free(allocation);puts("CAMERA_CONTROLS_OK: shared plain red shutter pixels, landscape guides, shared hit geometry, in-place zoom 1/1.5/2/3x for CSI/USB, frame boundaries, transient result and video stop");
}
'''
unit=out/'camera-controls.c';unit.write_text(prelude+source[start:end]+test)
binary=out/'camera-controls'
subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I'+str(Path(__file__).resolve().parents[1]/'overlay/apps/system/desktop'),str(unit),'-o',str(binary)],check=True)
subprocess.run([str(binary),str(out/"live-camera.rgb565")],check=True)

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

# Consume navigation only after display and recording ownership have ended.
gate=source[source.index('bool qpk_runtime_camera_take_album(void)'):source.index('static JSValue js_camera_photo_status(')]
gate_test='''#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
static pthread_mutex_t g_camera_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {char package[64];} g_qpk;
enum {QPK_CAMERA_IDLE,QPK_CAMERA_RUNNING};
static int g_camera_state,recording;
static unsigned g_camera_ui_action;
static int qpk_video_status(void *unused){(void)unused;return recording;}
'''+gate+'''
int main(void){
  strcpy(g_qpk.package,"com.openvela.camera.preview");g_camera_ui_action=7;
  g_camera_state=QPK_CAMERA_RUNNING;assert(!qpk_runtime_camera_take_album()&&g_camera_ui_action==7);
  g_camera_state=QPK_CAMERA_IDLE;recording=3;assert(!qpk_runtime_camera_take_album()&&g_camera_ui_action==7);
  recording=4;strcpy(g_qpk.package,"other.app");assert(!qpk_runtime_camera_take_album()&&g_camera_ui_action==7);
  strcpy(g_qpk.package,"com.openvela.camera.preview");assert(qpk_runtime_camera_take_album()&&g_camera_ui_action==0);
  assert(!qpk_runtime_camera_take_album());g_camera_ui_action=3;assert(!qpk_runtime_camera_take_album()&&g_camera_ui_action==3);
  puts("CAMERA_ALBUM_GATE_OK: waits for preview release and video finalization, package ownership, consume once");
}
'''
unit=out/'camera-album-gate.c';unit.write_text(gate_test);binary=out/'camera-album-gate'
subprocess.run(['gcc','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined',str(unit),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
