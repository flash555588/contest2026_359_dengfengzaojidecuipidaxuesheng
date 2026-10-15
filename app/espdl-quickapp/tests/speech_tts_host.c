/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SPEECH_HOST_TEST 1
#include "glass_speech_tts.c"
#include "glass_speech_assets.c"
static size_t samples;
static int write_audio(void *user,const int16_t *pcm,size_t n)
{samples+=n;return fwrite(pcm,sizeof(*pcm),n,user)==n?0:-EIO;}
static bool stop_now(void *user){(void)user;return true;}
int main(int argc,char **argv)
{
  assert(argc==4);
  int32_t ids[SPEECH_IDS];struct sano_job job={NULL,NULL,sano_now()+30000};
  int count=sano_ids(argv[1],"你好。",ids,&job);
  const int32_t expected[]={1,10,39,66,0,14,32,66,0,69,0,2};
  assert(count==(int)(sizeof(expected)/sizeof(*expected)));
  assert(!memcmp(ids,expected,sizeof(expected)));
  assert(sano_ids(argv[1],"",ids,&job)<0);
  assert(glass_sano_synthesize(argv[1],"你好",write_audio,stop_now,NULL)==-ECANCELED);
  FILE *vectors=fopen(argv[3],"r");assert(vectors);
  char *line=NULL;size_t capacity=0;unsigned checked=0;
  while(getline(&line,&capacity,vectors)>0){
    char *tab=strchr(line,'\t');assert(tab);*tab++=0;
    int32_t reference[SPEECH_IDS];unsigned n=0;char *save;
    for(char *token=strtok_r(tab,",\n",&save);token;token=strtok_r(NULL,",\n",&save)){assert(n<SPEECH_IDS);reference[n++]=atoi(token);}
    job.deadline=sano_now()+30000;count=sano_ids(argv[1],line,ids,&job);
    assert(count==(int)n&&!memcmp(ids,reference,n*sizeof(*ids)));checked++;
  }
  free(line);fclose(vectors);assert(checked>=14);
  printf("G2P_NATIVE_PARITY_OK: %u upstream vectors\n",checked);
  FILE *file=fopen(argv[2],"wb");assert(file);
  int ret=glass_sano_synthesize(argv[1],"你好，欢迎使用智能家居。",write_audio,NULL,file);
  fclose(file);assert(ret==0&&samples>22050&&samples<22050*15);
  printf("SANO_ZH_HOST_OK: exact Chinese phoneme ids; real Chinese model generated %zu samples at 22050 Hz; cancellation and empty rejection\n",samples);
  return 0;
}
