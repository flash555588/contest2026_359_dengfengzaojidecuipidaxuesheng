#include <bili_catchup.h>
#include "bili_decoder.h"
#include "bili_flv.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
struct record {uint64_t pts,hash;};
static bool fail_picture;
void *__real_malloc(size_t n);
void *__wrap_malloc(size_t n){if(fail_picture&&n>=100000){fail_picture=false;return NULL;}return __real_malloc(n);}
struct probe {FILE *file;void *decoder;bool configured,drop,reset,reset_done,seeking,fault;unsigned count,skipped;struct record frames[4096];uint8_t config[BILI_CONFIG_LIMIT];size_t config_size;};
static int picture(void *opaque,const uint8_t *const p[3],const int st[2],int w,int h,uint64_t pts){
  struct probe *s=opaque;assert(s->count<4096);uint64_t hash=1469598103934665603ULL;
  for(int plane=0;plane<3;plane++)for(int y=0;y<(plane?(h+1)/2:h);y++)for(int x=0;x<(plane?(w+1)/2:w);x++)hash=(hash^p[plane][y*st[plane?1:0]+x])*1099511628211ULL;
  s->frames[s->count++]=(struct record){pts,hash};return 0;
}
static int input(void *opaque,uint8_t *p,size_t n){return fread(p,1,n,((struct probe *)opaque)->file);}
static int packet_test(void *opaque,const uint8_t *p,size_t n,uint32_t ts){
  struct probe *s=opaque;uint8_t config[BILI_CONFIG_LIMIT];struct bili_access_unit a;assert(!bili_inspect_au(p,n,&a,config));
  if(a.sps&&a.pps){memcpy(s->config,config,a.config_bytes);s->config_size=a.config_bytes;}
  if(!a.vcl)return 0;
  if(s->reset&&!s->reset_done&&s->count>=6)s->seeking=true;
  if(s->seeking){
    if(!a.idr){s->skipped++;return 0;}
    bili_decoder_destroy(s->decoder);s->decoder=bili_decoder_create_timed(picture,s);assert(s->decoder);
    s->configured=false;s->seeking=false;s->reset_done=true;
  }
  if(s->drop&&a.disposable){s->skipped++;return 0;}
  uint8_t *joined=NULL;
  if(!s->configured){assert(s->config_size);joined=malloc(n+s->config_size);assert(joined);memcpy(joined,s->config,s->config_size);memcpy(joined+s->config_size,p,n);p=joined;n+=s->config_size;s->configured=true;}
  if(s->fault)fail_picture=true;
  int r=bili_decoder_feed_timestamp(s->decoder,p,n,ts);free(joined);return r;
}
static void run(struct probe *s,const char *path){
  s->file=fopen(path,"rb");assert(s->file);s->decoder=bili_decoder_create_timed(picture,s);assert(s->decoder);
  struct bili_flv f={.read=input,.video=packet_test,.opaque=s,.managed_buffers=true};
  int r=bili_flv_run(&f);
  if(s->fault){assert(r==-ENOMEM&&!fail_picture&&s->count==0);puts("PASS single-thread picture allocation failure returns ENOMEM without activating worker or crashing");}
  else{assert(!r||(r==-EPIPE&&feof(s->file)));assert(!bili_decoder_flush(s->decoder));}
  bili_decoder_destroy(s->decoder);free(f.tag);free(f.annexb);fclose(s->file);
}
static void pacing(void){
  struct bili_access_unit idr={.vcl=true,.idr=true},ref={.vcl=true},nonref={.vcl=true,.disposable=true};struct bili_pacer c={0};uint64_t pts;
  assert(bili_pace_packet(&c,&idr,100,1000000,true,&pts)==BILI_PACE_FEED&&pts==0);
  assert(bili_pace_packet(&c,&nonref,110,1400000,true,&pts)==BILI_PACE_SKIP);
  assert(bili_pace_packet(&c,&ref,120,1400000,true,&pts)==BILI_PACE_FEED);
  assert(bili_pace_packet(&c,&ref,500,5000000,true,&pts)==BILI_PACE_FEED&&!c.seeking);
  assert(bili_pace_packet(&c,&ref,800,5100000,true,&pts)==BILI_PACE_FEED);
  assert(bili_pace_packet(&c,&idr,2200,5200000,true,&pts)==BILI_PACE_FEED&&!c.seeking&&pts==2100&&c.late_ms==0);
  assert(bili_pace_render(&c,2200,5500000));assert(!bili_pace_render(&c,2200,5550000));assert(bili_pace_render(&c,2200,5800000));
  memset(&c,0,sizeof(c));assert(bili_pace_packet(&c,&idr,0xfffffff0u,0,true,&pts)==BILI_PACE_FEED);assert(bili_pace_packet(&c,&ref,16,32000,true,&pts)==BILI_PACE_FEED&&pts==32);
  uint8_t config[BILI_CONFIG_LIMIT];struct bili_access_unit a;
  const uint8_t unit[]={0,0,0,1,1,0xaa};assert(!bili_inspect_au(unit,sizeof(unit),&a,config)&&a.disposable);
  const uint8_t sei[]={0,0,0,1,6,0xaa,0,0,0,1,1,0xbb};assert(!bili_inspect_au(sei,sizeof(sei),&a,config)&&!a.disposable);
  const uint8_t bad[]={0,0,0,1};assert(bili_inspect_au(bad,sizeof(bad),&a,config)==-EPROTO);
}
int main(int argc,char **argv){assert(argc==2);pacing();struct probe *full=calloc(1,sizeof(*full)),*drop=calloc(1,sizeof(*drop)),*reset=calloc(1,sizeof(*reset)),*fault=calloc(1,sizeof(*fault));assert(full&&drop&&reset&&fault);fault->fault=true;run(fault,argv[1]);free(fault);drop->drop=true;reset->reset=true;run(full,argv[1]);run(drop,argv[1]);run(reset,argv[1]);assert(full->count>=30&&drop->skipped>0&&drop->count>0&&drop->count<full->count);assert(reset->reset_done&&reset->count>6);
  for(unsigned i=0;i<drop->count;i++){bool match=false;for(unsigned j=0;j<full->count;j++)if(drop->frames[i].pts==full->frames[j].pts&&drop->frames[i].hash==full->frames[j].hash)match=true;assert(match);}
  for(unsigned i=0;i<reset->count;i++){bool match=false;for(unsigned j=0;j<full->count;j++)if(reset->frames[i].pts==full->frames[j].pts&&reset->frames[i].hash==full->frames[j].hash)match=true;assert(match);}
  printf("PASS single-thread FLV decode + merged config; full=%u retained=%u safely skipped=%u; real decoder reset retained=%u; all retained YUV hashes equal baseline; PTS pacing/GOP recovery/wrap/starvation guard pass\n",full->count,drop->count,drop->skipped,reset->count);free(full);free(drop);free(reset);}
