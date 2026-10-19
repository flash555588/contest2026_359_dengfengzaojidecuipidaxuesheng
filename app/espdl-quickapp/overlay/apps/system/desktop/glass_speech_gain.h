/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>

static inline uint32_t speech_sqrt_u64(uint64_t value)
{
  uint64_t root=0,bit=1ull<<62;
  while(bit>value)bit>>=2;
  while(bit){
    if(value>=root+bit){value-=root+bit;root=(root>>1)+bit;}
    else root>>=1;
    bit>>=2;
  }
  return (uint32_t)root;
}

/* Level 16 kHz speech, including fading syllable tails, without changing
 * sample count or timing. Overlapping 32 ms RMS windows control a continuous
 * gain curve every 4 ms. Hold gain through pauses: never gate a weak tail or
 * restart its gain at a vendor/DMA boundary. Bound amplification and peaks. */
static inline int speech_level_pass(int16_t *pcm,size_t frames,
                                   bool (*cancel)(void *),void *user,
                                   unsigned *percent,uint32_t max_gain)
{
  *percent=100;
  if(cancel&&cancel(user))return -ECANCELED;
  if(!frames)return 0;
  const size_t hop=64,half=256;
  size_t points=(frames-1)/hop+2;
  if(points>SIZE_MAX/sizeof(uint32_t))return -EOVERFLOW;
  uint32_t *gains=malloc(points*sizeof(*gains));
  if(!gains)return -ENOMEM;
  size_t first=points;
  for(size_t block=0;block<points;block++){
    if(cancel&&cancel(user)){free(gains);return -ECANCELED;}
    size_t center=block*hop;if(center>=frames)center=frames-1;
    size_t begin=center>half?center-half:0;
    size_t end=frames-center>half?center+half:frames;
    uint64_t energy=0;unsigned peak=0;
    for(size_t i=begin;i<end;i++){
      int value=pcm[i];unsigned absolute=value<0?(unsigned)-value:(unsigned)value;
      if(absolute>peak)peak=absolute;
      energy+=(uint64_t)absolute*absolute;
    }
    uint32_t gain=0;
    unsigned rms=speech_sqrt_u64(energy/(end-begin));
    if(rms>=64){
      gain=(uint32_t)((5000ull<<16)/rms);
      if(gain>max_gain)gain=max_gain;
      if(peak){uint32_t ceiling=(uint32_t)((30000ull<<16)/peak);if(gain>ceiling)gain=ceiling;}
      if(first==points)first=block;
    }
    gains[block]=gain;
  }
  if(first==points){free(gains);return 0;}
  uint32_t held=gains[first];
  for(size_t i=0;i<points;i++){
    if(gains[i])held=gains[i];else gains[i]=held;
  }
  /* Anticipate loud onsets and recover quickly enough for a quiet syllable
   * tail, with interpolation preventing discrete gain steps. */
  for(size_t i=points-1;i>0;i--){
    uint32_t limit=(uint32_t)(((uint64_t)gains[i]*115+50)/100);
    if(gains[i-1]>limit)gains[i-1]=limit;
  }
  uint64_t total=gains[0];
  for(size_t i=1;i<points;i++){
    uint32_t limit=(uint32_t)(((uint64_t)gains[i-1]*120+50)/100);
    if(gains[i]>limit)gains[i]=limit;
    total+=gains[i];
  }
  *percent=(unsigned)((total*100/points+32768)/65536);
  for(size_t i=0;i<frames;i++){
    if(!(i&4095)&&cancel&&cancel(user)){free(gains);return -ECANCELED;}
    size_t block=i/hop,position=i%hop;
    uint32_t gain=(uint32_t)(((uint64_t)gains[block]*(hop-position)+(uint64_t)gains[block+1]*position)/hop);
    int64_t value=(int64_t)pcm[i]*gain;
    value=value>=0?(value+32768)/65536:-((-value+32768)/65536);
    if(value>30000)value=30000;
    if(value<-30000)value=-30000;
    pcm[i]=(int16_t)value;
  }
  free(gains);
  return 0;
}

static inline int glass_speech_normalize(int16_t *pcm,size_t frames,
                                         bool (*cancel)(void *),void *user,unsigned *percent)
{
  unsigned first=100,second=100;
  int ret=speech_level_pass(pcm,frames,cancel,user,&first,12u*65536u);
  /* A bounded second pass corrects residual level differences left by the
   * smooth first curve; it can add at most 1.5x. Total gain <=18x, and each
   * pass independently keeps the PCM peak below 30000. */
  if(!ret)ret=speech_level_pass(pcm,frames,cancel,user,&second,98304u);
  *percent=(unsigned)(((uint64_t)first*second+50)/100);
  return ret;
}
