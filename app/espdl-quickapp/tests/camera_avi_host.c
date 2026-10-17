/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_camera_avi.h"
#include <assert.h>
#include <math.h>
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"w+b");assert(f);struct qpk_avi a;
  assert(qpk_avi_begin(&a,f,320,180,10,16000)==0);
  uint16_t *frame=malloc(320*180*2);assert(frame);int16_t pcm[1600];
  for(unsigned y=0;y<180;y++)for(unsigned x=0;x<320;x++)frame[y*320+x]=y<90?0xf800:0x001f;
  for(unsigned n=0;n<30;n++){
    for(unsigned i=0;i<1600;i++)pcm[i]=(int16_t)(sin((n*1600+i)*6.283185307179586*440/16000)*12000);
    assert(qpk_avi_chunk(&a,pcm,sizeof(pcm),false)==0);
    assert(qpk_avi_chunk(&a,frame,320*180*2,true)==0);
  }
  assert(a.frames==30&&a.audio_bytes==96000&&a.count==60);
  assert(qpk_avi_chunk(&a,frame,2,true)==-EINVAL);
  assert(qpk_avi_chunk(&a,pcm,3,false)==-EINVAL);
  assert(qpk_avi_finish(&a)==0);assert(fclose(f)==0);free(frame);
  puts("CAMERA_AVI_OK: interleaved RGB565/PCM16, header/index finalization, rejected malformed chunks");
}
