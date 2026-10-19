#!/usr/bin/env python3
"""Exercise the production recording worker, queue, AVI writer and teardown."""
from pathlib import Path
import argparse, subprocess, tempfile, json
p=argparse.ArgumentParser();p.add_argument('--output',type=Path);a=p.parse_args()
out=(a.output or Path(tempfile.mkdtemp(prefix='camera-video-'))).resolve();out.mkdir(parents=True,exist_ok=True)
desktop=Path(__file__).resolve().parents[1]/'overlay/apps/system/desktop'
source=(desktop/'qpk_camera_video.inc').read_text().replace('#include "qpk_hardware.h"','').replace('#include "qpk_volume.h"','')
prelude=r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
struct qpk_hw_progress {atomic_bool cancel,finish;atomic_uint bytes,elapsed_ms,peak;};
typedef int (*qpk_pcm_sink_t)(void*,const uint8_t*,size_t);
#define QPK_VOLUME_SD 1
static int volume_error,audio_error;
static unsigned zoom_percent=100,style;
static unsigned qpk_camera_style(void){return style;}
static unsigned qpk_camera_zoom_percent(void){return zoom_percent;}
static int qpk_volume_mounted(int volume){assert(volume==1);return volume_error;}
static uint64_t qpk_now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int qpk_audio_capture_pcm(qpk_pcm_sink_t sink,void *cookie,struct qpk_hw_progress *p){
  if(audio_error)return audio_error;
  uint8_t pcm[1024]={0};unsigned n=0;
  while(!atomic_load(&p->finish)&&n++<200){
    usleep(32000);int ret=sink(cookie,pcm,sizeof(pcm));if(ret)return ret;
  }
  return 0;
}
'''
test=r'''
static uint16_t frame[1024*600];
static unsigned frame_w=640,frame_h=480;
static void wait_end(void){for(unsigned n=0;n<5000;n++){pthread_mutex_lock(&g_video_lock);bool busy=g_video_job!=NULL;pthread_mutex_unlock(&g_video_lock);if(!busy)return;usleep(1000);}assert(!"worker leaked");}
static void capture(unsigned ms){uint64_t end=qpk_now_ms()+ms;while(qpk_now_ms()<end){qpk_video_frame(frame,frame_w,frame_h,frame_w);usleep(15000);}}
int main(void){
  /* Exercise the actual frame queue with a coloured margin: zoom must remove
   * that margin in the recorded frame, not just update the preview label. */
  struct qpk_video_job *sample=calloc(1,sizeof(*sample));assert(sample);sample->ready=true;
  g_video_job=sample;
  for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)frame[y*640+x]=x<100||x>=540?0xf800:0x001f;
  for(unsigned zoom=100;zoom<=200;zoom+=100){
    zoom_percent=zoom;sample->next_frame=0;memset(sample->slot_state,0,sizeof(sample->slot_state));
    qpk_video_frame(frame,640,480,640);assert(sample->slot_state[0]==2);
    assert(sample->frames[0][90*320+40]==(zoom==100?0xf800:0x001f));
  }
  style=2;sample->next_frame=0;memset(sample->slot_state,0,sizeof(sample->slot_state));
  qpk_video_frame(frame,640,480,640);assert(sample->slot_state[0]==2);
  assert(sample->frames[0][90*320+40]==camera_color_pixel(0x001f,2));
  style=0;g_video_job=NULL;free(sample);zoom_percent=100;
  assert(qpk_video_start()==-EPERM);assert(!qpk_video_mode(true));
  qpk_video_authorize(true);assert(qpk_video_mode(true));
  volume_error=-ENODEV;assert(!qpk_video_start());wait_end();assert(qpk_video_status(NULL)==-ENODEV);
  volume_error=0;audio_error=-EIO;assert(!qpk_video_start());wait_end();assert(qpk_video_status(NULL)==-EIO);
  audio_error=0;
  assert(!qpk_video_start());qpk_video_stop();wait_end();assert(qpk_video_status(NULL)==-ENODATA);
  for(unsigned i=0;i<1024*600;i++)frame[i]=0xf800;
  for(unsigned cycle=0;cycle<3;cycle++){
    frame_w=cycle?1024:640;frame_h=cycle?600:480;zoom_percent=cycle==0?100:cycle==1?150:300;
    assert(!qpk_video_start());assert(qpk_video_start()==-EBUSY);assert(!qpk_video_mode(false));
    capture(1200);assert(qpk_video_status(NULL)==2);
    if(cycle==2)qpk_video_authorize(false);else qpk_video_stop();
    wait_end();assert(qpk_video_status(NULL)==4);assert(g_video.elapsed_ms>=1000);
    printf("VIDEO_FILE %s\n",g_video.path);
  }
  assert(!qpk_video_selected());assert(qpk_video_start()==-EPERM);
  puts("CAMERA_VIDEO_OK: real writer and worker; mic failure, missing SD, busy guard, three stop/reopen cycles, authorization revocation; ASan/UBSan");
}
'''
unit=out/'camera-video.c';unit.write_text(prelude+source+test)
binary=out/'camera-video'
subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I',str(desktop),f'-DQPK_VIDEO_MOUNT="{out}"',f'-DQPK_VIDEO_DIR="{out}/videos"',str(unit),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],check=True,capture_output=True,text=True);print(r.stdout)
for row in r.stdout.splitlines():
    if row.startswith('VIDEO_FILE '):
        media=Path(row[11:]);probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(media)]))
        video,audio=probe['streams'];assert video['codec_name']=='rawvideo' and video['width']==320 and video['height']==180
        assert audio['codec_name']=='pcm_s16le' and audio['sample_rate']=='16000' and audio['channels']==1
        assert abs(float(video['duration'])-int(audio['nb_frames'])/int(audio['sample_rate']))<.11
        subprocess.run(['ffmpeg','-v','error','-i',str(media),'-f','null','-'],check=True)
print('CAMERA_AV_SYNC_OK: all three files decode completely; video/audio duration mismatch <110ms')
