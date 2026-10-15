/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_CATCHUP_H
#define BILI_CATCHUP_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#define BILI_CONFIG_LIMIT 16384u
struct bili_access_unit {bool vcl,idr,disposable,sps,pps;size_t config_bytes;};
static size_t bili_start_code(const uint8_t *p,size_t n,size_t i)
{
  if(i+3<=n&&!p[i]&&!p[i+1]&&p[i+2]==1)return 3;
  if(i+4<=n&&!p[i]&&!p[i+1]&&!p[i+2]&&p[i+3]==1)return 4;
  return 0;
}
static int bili_inspect_au(const uint8_t *p,size_t n,struct bili_access_unit *a,uint8_t *config)
{
  memset(a,0,sizeof(*a));bool safe=true;size_t pos=0;
  while(pos<n){
    size_t prefix=bili_start_code(p,n,pos);if(!prefix)return -EPROTO;
    size_t begin=pos+prefix,end=begin;while(end<n&&!bili_start_code(p,n,end))end++;
    if(end==begin||(p[begin]&128))return -EPROTO;
    unsigned type=p[begin]&31;
    if(type>=1&&type<=5){a->vcl=true;if(type==5)a->idr=true;if(type!=1||(p[begin]&0x60))safe=false;}
    else if(type==7||type==8){
      if(type==7)a->sps=true;else a->pps=true;
      size_t bytes=end-begin;if(a->config_bytes+4+bytes>BILI_CONFIG_LIMIT)return -EFBIG;
      memcpy(config+a->config_bytes,"\0\0\0\1",4);memcpy(config+a->config_bytes+4,p+begin,bytes);a->config_bytes+=4+bytes;safe=false;
    }else if(type!=9&&type!=12)safe=false; /* Preserve SEI/unknown side effects. */
    pos=end;
  }
  a->disposable=a->vcl&&safe;return 0;
}
struct bili_pacer {
  bool clock_valid,seeking,displayed;
  uint32_t origin_ts;
  uint64_t origin_us,last_display_us,clock_offset_ms;
  unsigned decode_skipped,render_skipped,resyncs,late_ms;
};
enum bili_pace_action {BILI_PACE_FEED,BILI_PACE_SKIP,BILI_PACE_RESET};
static enum bili_pace_action bili_pace_packet(struct bili_pacer *c,const struct bili_access_unit *a,
                                              uint32_t timestamp,uint64_t now,bool config_valid,uint64_t *pts)
{
  *pts=0;if(!a->vcl)return BILI_PACE_FEED;
  if(!c->clock_valid){c->clock_valid=true;c->origin_ts=timestamp;c->origin_us=now;}
  int32_t delta=(int32_t)(timestamp-c->origin_ts);
  if(delta < -1000 && a->idr){ /* A provider restarted its timestamp timeline. */
    c->origin_ts=timestamp;c->origin_us=now;c->clock_offset_ms=0;c->seeking=false;delta=0;
    if(config_valid){c->resyncs++;return BILI_PACE_RESET;}
  }
  *pts=delta<0?0:(uint32_t)delta;
  uint64_t clock=(now-c->origin_us)/1000+c->clock_offset_ms;
  c->late_ms=clock>*pts?(unsigned)(clock-*pts):0;
  /* Never abandon a GOP merely because decode is slower than the stream.
   * This starves output and repeatedly pays the full threaded pool startup.
   * Rebase presentation at an IDR, while preserving reference decoding and
   * monotonically increasing timestamps for pictures still in the pipeline.
   */
  if(c->late_ms>1000&&a->idr){
    c->origin_us=now;c->clock_offset_ms=*pts;c->late_ms=0;c->resyncs++;
  }
  if(c->late_ms>150&&a->disposable){c->decode_skipped++;return BILI_PACE_SKIP;}
  return BILI_PACE_FEED;
}
static bool bili_pace_render(struct bili_pacer *c,uint64_t pts,uint64_t now)
{
  if(c->clock_valid){
    uint64_t clock=(now-c->origin_us)/1000+c->clock_offset_ms;c->late_ms=clock>pts?(unsigned)(clock-pts):0;
    if(c->late_ms>150&&c->displayed&&now-c->last_display_us<100000){c->render_skipped++;return false;}
  }
  c->last_display_us=now;c->displayed=true;return true;
}
#endif
