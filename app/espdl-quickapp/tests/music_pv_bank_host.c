/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdlib.h>
#include "glass_music_pv_bank.h"
int main(int argc,char **argv)
{
  assert(argc==4);unsigned style=strtoul(argv[2],NULL,10);bool valid=atoi(argv[3]);
  struct jpv_bank bank;int ret=jpv_open(&bank,argv[1],"慢下来的光","原创演示",style);
  struct jpv_reader *reader=calloc(1,sizeof(*reader));assert(reader);
  assert(jpv_frame(&bank,0,NULL,reader)==-EINVAL);
  assert(jpv_frame(NULL,0,NULL,reader)==-EINVAL);
  uint16_t *pixels=malloc(JPV_BYTES+4);assert(pixels);pixels[JPV_PIXELS]=0xacf1;pixels[JPV_PIXELS+1]=0xed27;
  if(!ret){
    for(unsigned i=0;i<bank.frames;i++){ret=jpv_frame(&bank,i,pixels,reader);if(ret)break;if(valid)printf("DECODED %u %u %u\n",style,i,jpv_adler((unsigned char *)pixels,JPV_BYTES));}
    assert(jpv_frame(&bank,bank.frames,pixels,reader)==-ERANGE);close(bank.fd);
  }
  assert(pixels[JPV_PIXELS]==0xacf1&&pixels[JPV_PIXELS+1]==0xed27);
  if((ret==0)!=valid){fprintf(stderr,"unexpected validation %s %d\n",argv[1],ret);abort();}
  free(pixels);jpv_reader_release(reader);free(reader);return 0;
}
