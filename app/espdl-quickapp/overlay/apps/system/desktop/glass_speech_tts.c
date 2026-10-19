/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_tts.h"
#include "glass_speech_assets.h"
#include "glass_speech_compute.h"
#include "snt_front_f32.h"
#include "snt_piperlite.h"
#include <quickjs.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SPEECH_IDS 2048
#define SPEECH_CHUNK_IDS 40
#define SPEECH_FRAMES 256
#define SPEECH_ARENA_FLOATS (8 * 1024 * 1024 / sizeof(float))
#define SPEECH_DECODER_TILE 64
#define SPEECH_DECODER_HALO 16

/* The speech service serializes jobs. The mutex also protects host callers
 * and makes eviction nonblocking for the UI while inference owns the data. */
static pthread_mutex_t sano_cache_lock=PTHREAD_MUTEX_INITIALIZER;
static atomic_uint sano_cache_epoch;
static struct {char *root,*front,*decoder;} sano_cache;
static void sano_cache_free(void)
{
  free(sano_cache.front);free(sano_cache.decoder);free(sano_cache.root);
  memset(&sano_cache,0,sizeof(sano_cache));
}
void glass_sano_cache_clear(void)
{
  atomic_fetch_add(&sano_cache_epoch,1);
  if(!pthread_mutex_trylock(&sano_cache_lock)){
    sano_cache_free();pthread_mutex_unlock(&sano_cache_lock);
  }
}

struct sano_job { speech_cancel_fn cancel; void *user; uint64_t deadline; unsigned char *lexicon; uint32_t chars,phrases; const unsigned char *records; const char *strings; };
static uint64_t sano_now(void)
{
  struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
  return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static bool sano_cancelled(struct sano_job *job)
{ return job->cancel && job->cancel(job->user); }
static int sano_interrupt(JSRuntime *rt,void *opaque)
{ (void)rt;struct sano_job *job=opaque;return sano_cancelled(job)||sano_now()>job->deadline; }

static void *sano_file(const char *root,const char *name,size_t limit,size_t *size,struct sano_job *job)
{
  (void)limit;
  char relative[64];snprintf(relative,sizeof(relative),"sanotts-zh/%s",name);
  return glass_speech_asset_cancelable(root,relative,size,job->cancel,job->user);
}

static uint32_t sano_u32(const void *p)
{ const unsigned char *b=p;return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24; }
static JSValue sano_lookup(JSContext *ctx,JSValueConst self,int argc,JSValueConst *args)
{
  (void)self;struct sano_job *job=JS_GetContextOpaque(ctx);int32_t group;
  if(argc!=3||JS_ToInt32(ctx,&group,args[0])||group<0||group>1||!JS_IsString(args[1]))return JS_NULL;
  size_t length;const char *key=JS_ToCStringLen(ctx,&length,args[1]);if(!key)return JS_EXCEPTION;
  unsigned base=group?job->chars:0,n=group?job->phrases:job->chars,lo=0,hi=n;
  while(lo<hi){unsigned mid=lo+(hi-lo)/2;const unsigned char *record=job->records+(base+mid)*8;
    int cmp=strcmp(job->strings+sano_u32(record),key);if(cmp<0)lo=mid+1;else hi=mid;}
  JSValue result=JS_NULL;
  bool prefix=JS_ToBool(ctx,args[2])>0;
  if(lo<n){const unsigned char *record=job->records+(base+lo)*8;const char *found=job->strings+sano_u32(record);
    if(prefix)result=JS_NewBool(ctx,!strncmp(found,key,length));
    else if(!strcmp(found,key))result=JS_NewString(ctx,job->strings+sano_u32(record+4));}
  else if(prefix)result=JS_FALSE;
  JS_FreeCString(ctx,key);return result;
}
static int sano_ids(const char *root,const char *text,int32_t ids[SPEECH_IDS],struct sano_job *job)
{
  int count=-EBADMSG;JSRuntime *rt=JS_NewRuntime();if(!rt)return -ENOMEM;
  JS_SetMemoryLimit(rt,6*1024*1024);JS_SetMaxStackSize(rt,48*1024);
  JS_SetInterruptHandler(rt,sano_interrupt,job);
  JSContext *ctx=JS_NewContext(rt);if(!ctx){JS_FreeRuntime(rt);return -ENOMEM;}
  JSValue global=JS_GetGlobalObject(ctx),tables=JS_NewObject(ctx);
  JS_SetContextOpaque(ctx,job);
  size_t lex_bytes;job->lexicon=sano_file(root,"lexicon.bin",3*1024*1024,&lex_bytes,job);
  if(!job->lexicon){count=-ENOENT;goto done;}
  if(lex_bytes<16||memcmp(job->lexicon,"SNLX",4)){count=-EBADMSG;goto done;}
  job->chars=sano_u32(job->lexicon+4);job->phrases=sano_u32(job->lexicon+8);
  uint32_t strings=sano_u32(job->lexicon+12);
  if(job->chars>100000||job->phrases>100000||16+(size_t)(job->chars+job->phrases)*8+strings!=lex_bytes){count=-EBADMSG;goto done;}
  job->records=job->lexicon+16;job->strings=(char *)job->records+(job->chars+job->phrases)*8;
  JS_SetPropertyStr(ctx,global,"__sanoLookup",JS_NewCFunction(ctx,sano_lookup,"lookup",3));
  const char *names[]={"chars","phrases","phrase_keys"};
  for(unsigned i=0;i<3;i++)JS_SetPropertyStr(ctx,tables,names[i],JS_NewString(ctx,"native"));
  JS_SetPropertyStr(ctx,global,"__sanoTables",JS_DupValue(ctx,tables));
  size_t bytes;char *data=sano_file(root,"phonemes.json",8192,&bytes,job);
  if(!data){count=-ENOENT;goto done;}
  JSValue map=JS_ParseJSON(ctx,data,bytes,"phonemes.json");free(data);
  if(JS_IsException(map)){JS_FreeValue(ctx,map);goto done;}
  JS_SetPropertyStr(ctx,global,"__sanoIdMap",map);
  data=sano_file(root,"g2p.js",128*1024,&bytes,job);
  if(!data){count=-ENOENT;goto done;}
  JSValue result=JS_Eval(ctx,data,bytes,"sanotts-zh-g2p.js",JS_EVAL_TYPE_GLOBAL);free(data);
  if(JS_IsException(result)){JS_FreeValue(ctx,result);goto done;}
  JS_FreeValue(ctx,result);
  JSValue method=JS_GetPropertyStr(ctx,global,"sanoPhonemize"),arg=JS_NewString(ctx,text);
  result=JS_Call(ctx,method,global,1,&arg);JS_FreeValue(ctx,arg);JS_FreeValue(ctx,method);
  if(!JS_IsException(result))
    {
      JSValue length=JS_GetPropertyStr(ctx,result,"length");uint32_t n=0;
      int ret=JS_ToUint32(ctx,&n,length);JS_FreeValue(ctx,length);
      if(ret>=0&&n>2&&n<=SPEECH_IDS)
        {
          count=n;
          for(unsigned i=0;i<n;i++)
            {
              JSValue id=JS_GetPropertyUint32(ctx,result,i);
              if(JS_ToInt32(ctx,&ids[i],id)<0||ids[i]<0||ids[i]>=73)count=-EBADMSG;
              JS_FreeValue(ctx,id);if(count<0)break;
            }
        }
      else count=-E2BIG;
    }
  JS_FreeValue(ctx,result);
done:
  JSValue exception=JS_GetException(ctx);
  JS_FreeValue(ctx,exception);
  JS_FreeValue(ctx,tables);JS_FreeValue(ctx,global);JS_FreeContext(ctx);JS_FreeRuntime(rt);
  free(job->lexicon);job->lexicon=NULL;
  return sano_cancelled(job)?-ECANCELED:count;
}

struct sano_compute_call {
  snt_front_model *front;snt_piperlite_model *decoder;
  int32_t *ids,*dur;int n,stage;long frames;
  float *latent,*audio,*arena;size_t floats;
};
struct sano_profile {uint64_t last;unsigned count;struct {const char *name;unsigned long ms;} stage[16];};
static void sano_stage(const char *name,const float *data,int ch,int len,void *opaque)
{
  (void)data;(void)ch;(void)len;struct sano_profile *p=opaque;uint64_t now=sano_now();
  if(p->count<16){p->stage[p->count].name=name;p->stage[p->count++].ms=(unsigned long)(now-p->last);}
  p->last=now;
}
static int sano_compute_body(void *opaque)
{
  struct sano_compute_call *call=opaque;
  if(call->stage==0)return (int)snt_front_durations(call->front,call->ids,call->n,1.0f,call->dur,call->arena,call->floats);
  if(call->stage==1)return snt_front_latent(call->front,call->ids,call->dur,call->n,call->frames,call->latent,call->arena,call->floats);
  return snt_piperlite_synthesize(call->decoder,call->latent,call->frames,call->audio,call->arena,call->floats);
}

/* The pinned, unfiltered Chinese decoder has a finite receptive field:
 * pre-conv radius 3, then three upsample/residual banks (radius <=45 in
 * each bank), and post-conv radius 3. Sixteen latent frames on either side
 * cover that field. Decode overlapping tiles and emit only their centers;
 * keep original utterance durations and acoustic context. No crossfade,
 * resampling, new padding at emitted boundaries, or weight approximation.
 * Reject a changed decoder geometry instead of silently truncating context.
 */
static int sano_decode(snt_piperlite_model *model,float *latent,long frames,
                       speech_pcm_fn write_pcm,speech_cancel_fn cancel,
                       void *user,struct sano_profile *profile)
{
  if(model->in_ch!=192||model->c0!=160||model->c1!=80||
     model->c2!=40||model->c3!=20||model->pf_channels)return -ENOTSUP;
  long capacity=SPEECH_DECODER_TILE+2*SPEECH_DECODER_HALO;
  if(capacity>frames)capacity=frames;
  size_t floats=snt_piperlite_arena_floats(model,capacity);
  size_t latent_floats=(size_t)model->in_ch*capacity;
  size_t audio_floats=(size_t)capacity*SNT_PIPERLITE_HOP;
  if(floats+latent_floats+audio_floats>SPEECH_ARENA_FLOATS)return -E2BIG;
  float *arena=calloc(floats+latent_floats+audio_floats,sizeof(float));
  if(!arena)return -ENOMEM;
  float *tile=arena+floats,*audio=tile+latent_floats;
  printf("[speech] tts decoder workspace_bytes=%u tile=%d halo=%d\n",
    (unsigned)((floats+latent_floats+audio_floats)*sizeof(float)),
    SPEECH_DECODER_TILE,SPEECH_DECODER_HALO);
  int ret=0;
  for(long start=0;start<frames&&!ret;start+=SPEECH_DECODER_TILE){
    if(cancel&&cancel(user)){ret=-ECANCELED;break;}
    long stop=start+SPEECH_DECODER_TILE;if(stop>frames)stop=frames;
    long begin=start>SPEECH_DECODER_HALO?start-SPEECH_DECODER_HALO:0;
    long end=stop+SPEECH_DECODER_HALO;if(end>frames)end=frames;
    long count=end-begin;
    for(int ch=0;ch<model->in_ch;ch++)
      memcpy(tile+ch*count,latent+ch*frames+begin,count*sizeof(float));
    struct sano_compute_call call={.decoder=model,.stage=2,.frames=count,
      .latent=tile,.audio=audio,.arena=arena,.floats=floats};
    profile->last=sano_now();
    ret=glass_speech_compute_run(sano_compute_body,&call,cancel,user);
    for(size_t offset=(start-begin)*SNT_PIPERLITE_HOP,
               limit=(stop-begin)*SNT_PIPERLITE_HOP;!ret&&offset<limit;){
      if(cancel&&cancel(user)){ret=-ECANCELED;break;}
      int16_t pcm[512];size_t size=limit-offset;if(size>512)size=512;
      for(size_t i=0;i<size;i++){
        float sample=audio[offset+i];
        if(!isfinite(sample)){ret=-EBADMSG;break;}
        if(sample>1)sample=1;if(sample<-1)sample=-1;
        pcm[i]=(int16_t)(sample*32767);
      }
      if(!ret)ret=write_pcm(user,pcm,size);
      offset+=size;
    }
  }
  free(arena);return ret;
}
int glass_sano_synthesize(const char *root,const char *text,
                          speech_pcm_fn write_pcm,speech_cancel_fn cancel,void *user)
{
  if(!root||!text||!text[0]||strlen(text)>1024||!write_pcm)return -EINVAL;
  pthread_mutex_lock(&sano_cache_lock);
  unsigned epoch=atomic_load(&sano_cache_epoch);
  uint64_t model_started=sano_now();
  struct sano_job job={cancel,user,sano_now()+30000};int32_t ids[SPEECH_IDS];
  int count=sano_ids(root,text,ids,&job);
  uint64_t g2p_ms=sano_now()-model_started;
  if(count<0){sano_cache_free();pthread_mutex_unlock(&sano_cache_lock);return count;}
  size_t front_bytes=0,decoder_bytes=0;
  bool hit=sano_cache.root&&!strcmp(sano_cache.root,root);
  if(!hit)sano_cache_free();
  char *front=hit?sano_cache.front:sano_file(root,"front.bin",4*1024*1024,&front_bytes,&job);
  char *decoder=hit?sano_cache.decoder:NULL;
  int ret=-EBADMSG;
  snt_front_model fm={0};snt_piperlite_model dm={0};
  if(!front){ret=-ENOENT;goto done;}
  if(!hit)decoder=sano_file(root,"dec.bin",4*1024*1024,&decoder_bytes,&job);
  if(!front||!decoder){ret=-ENOENT;goto done;}
  if((!hit&&(front_bytes!=3294820||decoder_bytes!=2923300))||
      snt_front_init(&fm,front,568,(float *)(front+568),823563)||
      snt_piperlite_init(&dm,decoder,416,(float *)(decoder+416),730721))goto done;
  if(fm.d_vocab!=73||fm.a_out!=192||dm.in_ch!=192)goto done;
  if(!hit){
    sano_cache.root=strdup(root);
    if(!sano_cache.root){ret=-ENOMEM;goto done;}
    sano_cache.front=front;sano_cache.decoder=decoder;
  }
  printf("[speech] tts model_ms=%lu phonemes=%d\n",(unsigned long)(sano_now()-model_started),count);
  printf("[speech] tts prepare g2p_ms=%lu weights_ms=%lu cache_hit=%u cached_bytes=%u\n",
    (unsigned long)g2p_ms,(unsigned long)(sano_now()-model_started-g2p_ms),hit,3294820u+2923300u);
  ret=0;
  for(int cursor=1;cursor<count-1&&!ret;)
    {
      if(sano_cancelled(&job)){ret=-ECANCELED;break;}
      uint64_t chunk_started=sano_now();
      int end=cursor+SPEECH_CHUNK_IDS-2;if(end>count-1)end=count-1;
      if(end<count-1)while(end>cursor&&ids[end-1]!=0)end--;
      if(end==cursor){ret=-E2BIG;break;}
      int32_t chunk[SPEECH_CHUNK_IDS],dur[SPEECH_CHUNK_IDS];int n=end-cursor+2;
      chunk[0]=1;memcpy(chunk+1,ids+cursor,(n-2)*sizeof(int32_t));chunk[n-1]=2;cursor=end;
      size_t floats=snt_front_duration_arena_floats(&fm,n);
      float *arena=calloc(floats,sizeof(float));if(!arena){ret=-ENOMEM;break;}
      struct sano_compute_call call={.front=&fm,.decoder=&dm,.ids=chunk,.dur=dur,.n=n,.arena=arena,.floats=floats};
      uint64_t stage_started=sano_now();
      long frames=glass_speech_compute_run(sano_compute_body,&call,cancel,user);free(arena);
      unsigned long duration_ms=(unsigned long)(sano_now()-stage_started);
      if(frames<0){ret=(int)frames;break;}
      if(frames<=0||frames>SPEECH_FRAMES){ret=-E2BIG;break;}
      floats=snt_front_latent_arena_floats(&fm,n,frames);
      size_t latent_floats=(size_t)fm.a_out*frames;
      if(floats+latent_floats>SPEECH_ARENA_FLOATS){ret=-E2BIG;break;}
      arena=calloc(floats,sizeof(float));
      float *latent=calloc(latent_floats,sizeof(float));
      if(!arena||!latent){free(arena);free(latent);ret=-ENOMEM;break;}
      call.frames=frames;call.latent=latent;call.arena=arena;call.floats=floats;call.stage=1;
      stage_started=sano_now();
      ret=glass_speech_compute_run(sano_compute_body,&call,cancel,user);
      free(arena);
      unsigned long latent_ms=(unsigned long)(sano_now()-stage_started);stage_started=sano_now();
      struct sano_profile profile={.last=stage_started};
      dm.stage_cb=sano_stage;dm.stage_user=&profile;
      if(!ret&&!sano_cancelled(&job))ret=sano_decode(&dm,latent,frames,write_pcm,cancel,user,&profile);
      else if(!ret)ret=-ECANCELED;
      unsigned long decoder_ms=(unsigned long)(sano_now()-stage_started);
      for(unsigned i=0;i<profile.count;i++)printf("[speech] tts hotspot=%s elapsed_ms=%lu\n",profile.stage[i].name,profile.stage[i].ms);
      printf("[speech] tts stages duration_ms=%lu latent_ms=%lu decoder_ms=%lu\n",
        duration_ms,latent_ms,decoder_ms);
      printf("[speech] tts chunk frames=%ld compute_ms=%lu result=%d\n",frames,(unsigned long)(sano_now()-chunk_started),ret);
      free(latent);
    }
done:
  if(front!=sano_cache.front)free(front);
  if(decoder!=sano_cache.decoder)free(decoder);
  if(ret||sano_cancelled(&job)||epoch!=atomic_load(&sano_cache_epoch))sano_cache_free();
  pthread_mutex_unlock(&sano_cache_lock);
  /* Cover an eviction racing the final epoch check and mutex release. */
  if(epoch!=atomic_load(&sano_cache_epoch))glass_sano_cache_clear();
  return sano_cancelled(&job)?-ECANCELED:ret;
}
