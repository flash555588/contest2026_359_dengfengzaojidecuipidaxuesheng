/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../overlay/apps/system/desktop/glass_music_duration.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "../overlay/apps/system/desktop/music_vendor/minimp3.h"

static void be32(unsigned char *p, uint32_t n)
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }

int main(int argc, char **argv)
{
  unsigned char frame[256]={0xff,0xfb,0x90,0};
  memcpy(frame+36,"Xing",4); be32(frame+40,1); be32(frame+44,1000);
  assert(music_frame_duration(frame,sizeof(frame),44100)==26122);
  for (size_t size=0;size<48;size++) assert(!music_frame_duration(frame,size,44100));
  assert(!music_frame_duration(NULL,256,44100));
  assert(!music_frame_duration(frame,256,0));
  be32(frame+40,2); assert(!music_frame_duration(frame,256,44100));
  be32(frame+40,1); be32(frame+44,UINT32_MAX);
  assert(!music_frame_duration(frame,256,1));
  memset(frame+4,0,252);
  frame[1]=0xf3; frame[3]=0xc0;
  memcpy(frame+13,"Info",4); be32(frame+17,1); be32(frame+21,1000);
  assert(music_frame_duration(frame,256,22050)==26122);
  memset(frame+4,0,252);
  frame[1]=0xfb; frame[3]=0;
  memcpy(frame+36,"VBRI",4); be32(frame+50,1000);
  assert(music_frame_duration(frame,256,44100)==26122);
  assert(!music_frame_duration(frame,53,44100));
  frame[1]=0xf5; assert(!music_frame_duration(frame,256,44100));
  for (int i=1;i<argc;i++) {
    FILE *file=fopen(argv[i],"rb"); assert(file);
    unsigned char buffer[32768]; size_t size=fread(buffer,1,sizeof(buffer),file); fclose(file);
    mp3dec_t decoder; mp3dec_init(&decoder);
    mp3dec_frame_info_t info={0};
    assert(mp3dec_decode_frame(&decoder,buffer,size,NULL,&info)>0);
    unsigned duration=music_frame_duration(buffer+info.frame_offset,
      info.frame_bytes-info.frame_offset,info.hz);
    /* Fixture files contain ten seconds, plus encoder padding. */
    assert(duration>=10000 && duration<=10100);
    printf("MP3_DURATION_OK %s %u ms\n",argv[i],duration);
  }
  puts("MUSIC_DURATION_OK");
  return 0;
}
