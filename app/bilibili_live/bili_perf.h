/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_PERF_H
#define BILI_PERF_H
#include <stdint.h>
/* UI-thread window counters. "Display" means handed to LVGL, not panel vsync. */
struct bili_perf_window {
  uint64_t at_ms,decoder_us,convert_us;
  unsigned decoded,submitted,displayed;
  unsigned decode_fps_x100,display_fps_x100,decode_ms,convert_ms;
};
static inline int bili_perf_update(struct bili_perf_window *p,uint64_t now,
  unsigned decoded,unsigned submitted,unsigned displayed,uint64_t decoder_us,uint64_t convert_us)
{
  if(!p->at_ms||now<p->at_ms||decoded<p->decoded||submitted<p->submitted||displayed<p->displayed){
    p->at_ms=now;p->decoded=decoded;p->submitted=submitted;p->displayed=displayed;
    p->decoder_us=decoder_us;p->convert_us=convert_us;
    p->decode_fps_x100=p->display_fps_x100=p->decode_ms=p->convert_ms=0;return 0;
  }
  uint64_t elapsed=now-p->at_ms;if(elapsed<2000)return 0;
  unsigned d=decoded-p->decoded,c=submitted-p->submitted;
  p->decode_fps_x100=(uint64_t)d*100000/elapsed;
  p->display_fps_x100=(uint64_t)(displayed-p->displayed)*100000/elapsed;
  p->decode_ms=d?(decoder_us-p->decoder_us)/d/1000:0;
  p->convert_ms=c?(convert_us-p->convert_us)/c/1000:0;
  p->at_ms=now;p->decoded=decoded;p->submitted=submitted;p->displayed=displayed;
  p->decoder_us=decoder_us;p->convert_us=convert_us;return 1;
}
#endif
