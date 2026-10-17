#!/usr/bin/env python3
"""Exercise the real buffer-pool implementation and camera REQBUFS sequence."""
from pathlib import Path
import argparse,subprocess

p=argparse.ArgumentParser();p.add_argument('--nuttx',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
pool=(a.nuttx/'drivers/video/video_framebuff.c').read_text()
functions=pool[pool.index('static void init_buf_chain'):pool.index('static inline bool is_last_one')]
functions+=pool[pool.index('int video_framebuff_realloc_container'):pool.index('int video_framebuff_is_empty')]
functions+=pool[pool.index('vbuf_container_t *video_framebuff_get_container'):pool.index('void video_framebuff_free_container')]
source=(Path(__file__).resolve().parents[1]/'overlay/apps/system/desktop/qpk_runtime.c').read_text()
start=source.index('  memset(&request, 0, sizeof(request));',source.index('static int qpk_camera_start_source'))
end=source.index('  camera->memory_type = V4L2_MEMORY_USERPTR;',start)
prelude='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define OK 0
#define nxmutex_lock(p) ((void)0)
#define nxmutex_unlock(p) ((void)0)
#define kmm_realloc realloc
#define kmm_free free
typedef struct container {struct container *next;} vbuf_container_t;
typedef struct {int lock_empty,container_size;vbuf_container_t *vbuf_empty,*vbuf_next,*vbuf_top,*vbuf_tail,*vbuf_alloced;} video_framebuff_t;
static video_framebuff_t pool;
'''
mock='''
#define QPK_CAMERA_DISPLAY_BUFFER_COUNT 2
#define V4L2_MEMORY_USERPTR 2
#define V4L2_BUF_MODE_RING 1
#define VIDIOC_REQBUFS 1
struct request {unsigned type,memory,count,mode;};
static int ioctl(int fd,unsigned command,uintptr_t arg){
  (void)fd;assert(command==VIDIOC_REQBUFS);struct request *r=(void*)arg;
  int ret=video_framebuff_realloc_container(&pool,r->count);
  if(ret){errno=-ret;return -1;}return 0;
}
static int prepare(void){
  struct request request;int type=1,ret;struct {int fd;} object={3},*camera=&object;
'''
test='''
  return 0;error:return ret;
}
int main(void){
  assert(video_framebuff_realloc_container(&pool,2)==0);
  assert(video_framebuff_get_container(&pool));assert(video_framebuff_get_container(&pool));
  assert(video_framebuff_realloc_container(&pool,2)==0);
  assert(!video_framebuff_get_container(&pool)); /* Reproduce stale same-count queue. */
  for(unsigned i=0;i<50;i++){
    assert(prepare()==0);
    assert(video_framebuff_get_container(&pool));assert(video_framebuff_get_container(&pool));
    assert(!video_framebuff_get_container(&pool));
  }
  assert(video_framebuff_realloc_container(&pool,0)==0);
  puts("CAMERA_REOPEN_OK: actual V4L2 queue reproduces stale pool; production sequence recovers 50 reopens");
}
'''
unit=a.output/'camera-reopen.c';unit.write_text(prelude+functions+mock+source[start:end]+test)
binary=a.output/'camera-reopen'
subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(unit),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
