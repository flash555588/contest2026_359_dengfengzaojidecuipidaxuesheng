#!/usr/bin/env python3
"""Execute actual camera queue/service functions across the GUI pass boundary."""
import argparse
from pathlib import Path
import subprocess
from test_display_flash import function

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=(repo/'app/espdl-quickapp/overlay/apps/system/desktop/qpk_runtime.c').read_text()
start=source.index('static struct qpk_camera_start_s\n')
end=source.index('} g_camera_start;',start)+len('} g_camera_start;')
production=source[start:end]+'\n'+''.join(function(source,name,prefix)+'\n' for name,prefix in [
 ('qpk_camera_start_source','static int '),('qpk_camera_service_start','static void '),
 ('qpk_runtime_camera_busy','bool '),('qpk_runtime_camera_active','bool ')])
before='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
enum {QPK_CAMERA_IDLE,QPK_CAMERA_ACTIVE,QPK_CAMERA_STOPPING};
static unsigned g_camera_state,starts,flushes;
static bool g_camera_redraw,in_lv_pass,flushed;
static struct {bool direct_preview;} g_camera;
static int g_camera_last_error,start_error;
static pthread_mutex_t g_camera_lock=PTHREAD_MUTEX_INITIALIZER;
static void qpk_camera_stop(void);
static void lv_refr_now(void *display) {(void)display;assert(!in_lv_pass);flushes++;flushed=true;}
static int qpk_camera_start_source_now(int x,int y,int usb,uint32_t gen,unsigned mode,uint32_t interval)
{
 assert(!in_lv_pass&&flushed&&x==2&&y==3&&usb==1&&gen==7&&mode==8&&interval==333333);
 starts++;if(start_error)return start_error;
 g_camera_state=QPK_CAMERA_ACTIVE;g_camera.direct_preview=true;return 0;
}
'''
after='''static void qpk_camera_stop(void)
{
 g_camera_start.pending=false;
 if(g_camera_state==QPK_CAMERA_ACTIVE)g_camera_state=QPK_CAMERA_STOPPING;
}
int main(void)
{
 in_lv_pass=true;
 assert(qpk_camera_start_source(2,3,1,7,8,333333)==0);
 assert(starts==0&&flushes==0&&qpk_runtime_camera_busy()&&!qpk_runtime_camera_active());
 /* A later refresh timer in this same pass still has exclusive GUI ownership. */
 assert(qpk_camera_start_source(2,3,1,7,8,333333)==-EBUSY);
 assert(g_camera_start.pending&&starts==0);
 in_lv_pass=false;qpk_camera_service_start();
 assert(starts==1&&flushes==1&&qpk_runtime_camera_active());
 assert(qpk_camera_start_source(2,3,1,7,8,333333)==-EBUSY);
 g_camera_state=QPK_CAMERA_IDLE;g_camera.direct_preview=false;g_camera_redraw=true;
 assert(qpk_runtime_camera_active()&&qpk_runtime_camera_busy());
 assert(qpk_camera_start_source(2,3,1,7,8,333333)==-EBUSY);
 /* The UI consumes redraw and invalidates before resuming its handler. */
 g_camera_redraw=false;
 assert(!qpk_runtime_camera_active()&&!qpk_runtime_camera_busy());
 assert(qpk_camera_start_source(2,3,1,7,8,333333)==0);
 qpk_camera_stop();qpk_camera_service_start();assert(starts==1); /* cancelled */
 start_error=-ENODEV;
 assert(qpk_camera_start_source(2,3,1,7,8,333333)==0);
 qpk_camera_service_start();assert(starts==2&&g_camera_last_error==-ENODEV);
 assert(!qpk_runtime_camera_busy());
 puts("CAMERA_BOUNDARY_OK: actual queue/service, same-pass exclusion, pending duplicate, cleanup redraw gate, cancel and async failure");
 return 0;
}
'''
(out/'boundary.c').write_text(before+production+after)
exe=out/'boundary'
subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-O1','-g','-pthread',
 '-fsanitize=address,undefined','-fno-pie','-no-pie',str(out/'boundary.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
