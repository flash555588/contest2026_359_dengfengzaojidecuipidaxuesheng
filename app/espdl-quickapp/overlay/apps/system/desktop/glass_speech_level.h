/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* Bounded, allocation-free envelope of the PCM already sent to the speaker. */
static inline unsigned glass_speech_pcm_level(const int16_t *pcm,size_t frames)
{
  unsigned peak=0;
  for(size_t i=0;i<frames;i++){
    int sample=pcm[i];unsigned magnitude=sample<0?(unsigned)-sample:(unsigned)sample;
    if(magnitude>peak)peak=magnitude;
  }
  return peak>32767?32767:peak;
}
