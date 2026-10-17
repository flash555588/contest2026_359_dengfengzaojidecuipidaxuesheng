/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SPEECH_HOST_TEST 1
#include "glass_speech_tts.c"
#include "glass_speech_assets.c"
static size_t samples;
static bool evict_on_write;
static int write_audio(void *user,const int16_t *pcm,size_t n)
{
  if(evict_on_write){evict_on_write=false;glass_sano_cache_clear();}
  samples+=n;return fwrite(pcm,sizeof(*pcm),n,user)==n?0:-EIO;
}
static bool stop_now(void *user){(void)user;return true;}
static bool cancel_on_third_poll(void *user){unsigned *polls=user;return ++*polls==3;}
static void check_model_cancellation(const char *root,const int32_t *ids,int n)
{
  size_t bytes;char *data=glass_speech_asset(root,"sanotts-zh/front.bin",&bytes);assert(data);
  snt_front_model fm={0};assert(!snt_front_init(&fm,data,568,(float *)(data+568),823563));
  int32_t durations[SPEECH_IDS];size_t floats=snt_front_duration_arena_floats(&fm,n);
  float *arena=calloc(floats,sizeof(float));assert(arena);
  struct sano_compute_call call={.front=&fm,.ids=(int32_t *)ids,.dur=durations,.n=n,.arena=arena,.floats=floats};
  unsigned polls=0;
  assert(glass_speech_compute_run(sano_compute_body,&call,cancel_on_third_poll,&polls)==-ECANCELED&&polls==3);
  free(arena);free(data);
  data=glass_speech_asset(root,"sanotts-zh/dec.bin",&bytes);assert(data);
  snt_piperlite_model dm={0};assert(!snt_piperlite_init(&dm,data,416,(float *)(data+416),730721));
  floats=snt_piperlite_arena_floats(&dm,8);arena=calloc(floats,sizeof(float));assert(arena);
  float latent[192*8]={0},audio[8*SNT_PIPERLITE_HOP];
  call=(struct sano_compute_call){.decoder=&dm,.stage=2,.frames=8,.latent=latent,.audio=audio,.arena=arena,.floats=floats};
  polls=0;assert(glass_speech_compute_run(sano_compute_body,&call,cancel_on_third_poll,&polls)==-ECANCELED&&polls==3);
  free(arena);free(data);
  puts("SANO_COMPUTE_CANCEL_OK: cancellation inside real front and decoder kernels, caller arenas released");
}
int main(int argc,char **argv)
{
  assert(argc==4);
  int32_t ids[SPEECH_IDS];struct sano_job job={NULL,NULL,sano_now()+30000};
  int count=sano_ids(argv[1],"你好。",ids,&job);
  const int32_t expected[]={1,10,39,66,0,14,32,66,0,69,0,2};
  assert(count==(int)(sizeof(expected)/sizeof(*expected)));
  assert(!memcmp(ids,expected,sizeof(expected)));
  check_model_cancellation(argv[1],ids,count);
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
  assert(sano_cache.front&&sano_cache.decoder&&sano_cache.root);
  size_t cold_samples=samples;samples=0;file=tmpfile();assert(file);
  /* Evict from inside active PCM output: must not block the owning worker,
   * destroy its weights early, or change even one sample of the warm run. */
  evict_on_write=true;
  assert(!glass_sano_synthesize(argv[1],"你好，欢迎使用智能家居。",write_audio,NULL,file));
  assert(samples==cold_samples&&!sano_cache.root&&!sano_cache.front&&!sano_cache.decoder);
  rewind(file);FILE *cold=fopen(argv[2],"rb");assert(cold);
  int a,b;do{a=fgetc(cold);b=fgetc(file);assert(a==b);}while(a!=EOF);
  fclose(cold);fclose(file);glass_sano_cache_clear();glass_sano_cache_clear();
  puts("SANO_CACHE_OK: cold/warm PCM identical; deferred eviction during PCM output; repeated clear safe");
  printf("SANO_ZH_HOST_OK: exact Chinese phoneme ids; real Chinese model generated %zu samples at 22050 Hz; cancellation and empty rejection\n",samples);
  return 0;
}
