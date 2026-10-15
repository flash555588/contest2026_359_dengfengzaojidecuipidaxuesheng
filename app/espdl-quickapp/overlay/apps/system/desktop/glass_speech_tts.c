/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_tts.h"
#include "glass_speech_assets.h"
#include "snt_front_f32.h"
#include "snt_piperlite.h"
#include <quickjs.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SPEECH_IDS 2048
#define SPEECH_CHUNK_IDS 40
#define SPEECH_FRAMES 256
#define SPEECH_ARENA_FLOATS (24 * 1024 * 1024 / sizeof(float))

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

static void *sano_file(const char *root,const char *name,size_t limit,size_t *size)
{
  (void)limit;
  char relative[64];snprintf(relative,sizeof(relative),"sanotts-zh/%s",name);
  return glass_speech_asset(root,relative,size);
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
  size_t lex_bytes;job->lexicon=sano_file(root,"lexicon.bin",3*1024*1024,&lex_bytes);
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
  size_t bytes;char *data=sano_file(root,"phonemes.json",8192,&bytes);
  if(!data){count=-ENOENT;goto done;}
  JSValue map=JS_ParseJSON(ctx,data,bytes,"phonemes.json");free(data);
  if(JS_IsException(map)){JS_FreeValue(ctx,map);goto done;}
  JS_SetPropertyStr(ctx,global,"__sanoIdMap",map);
  data=sano_file(root,"g2p.js",128*1024,&bytes);
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

int glass_sano_synthesize(const char *root,const char *text,
                          speech_pcm_fn write_pcm,speech_cancel_fn cancel,void *user)
{
  if(!root||!text||!text[0]||strlen(text)>1024||!write_pcm)return -EINVAL;
  struct sano_job job={cancel,user,sano_now()+30000};int32_t ids[SPEECH_IDS];
  int count=sano_ids(root,text,ids,&job);if(count<0)return count;
  size_t front_bytes=0,decoder_bytes=0;
  char *front=sano_file(root,"front.bin",4*1024*1024,&front_bytes);
  char *decoder=sano_file(root,"dec.bin",4*1024*1024,&decoder_bytes);
  int ret=-EBADMSG;
  snt_front_model fm={0};snt_piperlite_model dm={0};
  if(!front||!decoder){ret=-ENOENT;goto done;}
  if(front_bytes!=3294820||decoder_bytes!=2923300||
      snt_front_init(&fm,front,568,(float *)(front+568),823563)||
      snt_piperlite_init(&dm,decoder,416,(float *)(decoder+416),730721))goto done;
  if(fm.d_vocab!=73||fm.a_out!=192||dm.in_ch!=192)goto done;
  ret=0;
  for(int cursor=1;cursor<count-1&&!ret;)
    {
      if(sano_cancelled(&job)){ret=-ECANCELED;break;}
      int end=cursor+SPEECH_CHUNK_IDS-2;if(end>count-1)end=count-1;
      if(end<count-1)while(end>cursor&&ids[end-1]!=0)end--;
      if(end==cursor){ret=-E2BIG;break;}
      int32_t chunk[SPEECH_CHUNK_IDS],dur[SPEECH_CHUNK_IDS];int n=end-cursor+2;
      chunk[0]=1;memcpy(chunk+1,ids+cursor,(n-2)*sizeof(int32_t));chunk[n-1]=2;cursor=end;
      size_t floats=snt_front_duration_arena_floats(&fm,n);
      float *arena=calloc(floats,sizeof(float));if(!arena){ret=-ENOMEM;break;}
      long frames=snt_front_durations(&fm,chunk,n,1.0f,dur,arena,floats);free(arena);
      if(frames<=0||frames>SPEECH_FRAMES){ret=-E2BIG;break;}
      size_t af=snt_front_latent_arena_floats(&fm,n,frames),df=snt_piperlite_arena_floats(&dm,frames);
      floats=af>df?af:df;
      size_t latent_floats=(size_t)fm.a_out*frames,audio_floats=frames*SNT_PIPERLITE_HOP;
      if(floats+latent_floats+audio_floats>SPEECH_ARENA_FLOATS){ret=-E2BIG;break;}
      arena=calloc(floats+latent_floats+audio_floats,sizeof(float));
      if(!arena){ret=-ENOMEM;break;}
      float *latent=arena+floats,*audio=latent+latent_floats;
      ret=snt_front_latent(&fm,chunk,dur,n,frames,latent,arena,floats);
      if(!ret&&!sano_cancelled(&job))ret=snt_piperlite_synthesize(&dm,latent,frames,audio,arena,floats);
      if(!ret)
        for(size_t offset=0;offset<audio_floats;)
          {
            if(sano_cancelled(&job)){ret=-ECANCELED;break;}
            int16_t pcm[512];size_t size=audio_floats-offset;if(size>512)size=512;
            for(size_t i=0;i<size;i++){
              float sample=audio[offset+i];
              if(!isfinite(sample)){ret=-EBADMSG;break;}
              if(sample>1)sample=1;if(sample<-1)sample=-1;
              pcm[i]=(int16_t)(sample*32767);
            }
            if(ret)break;
            ret=write_pcm(user,pcm,size);if(ret)break;offset+=size;
          }
      free(arena);
    }
done:
  free(front);free(decoder);return ret;
}
