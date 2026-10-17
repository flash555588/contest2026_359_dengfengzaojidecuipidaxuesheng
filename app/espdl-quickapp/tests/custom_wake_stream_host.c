/* SPDX-License-Identifier: Apache-2.0 */
/* Run long negative streams under ASan/UBSan without resetting VAD state. */
#include "glass_custom_wake.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc,char **argv)
{
  assert(argc==4);
  const size_t chunks[]={1,319,320,496,1023,4096};
  for(int input=2;input<4;input++){
    FILE *file=fopen(argv[input],"rb");assert(file);
    struct glass_custom_wake *state=NULL;
    assert(!glass_custom_wake_create(argv[1],&state));
    int16_t pcm[4096];size_t got,step=0;uint64_t samples=0;unsigned events=0;
    while((got=fread(pcm,sizeof(*pcm),chunks[step++%6],file))){
      int ret=glass_custom_wake_feed(state,pcm,got);assert(ret>=0);
      events+=ret;samples+=got;
    }
    assert(!ferror(file));fclose(file);
    assert(samples>=16000*60&&events==0);
    printf("WAKE_LONG_ASAN samples=%llu events=%u evaluations=%u\n",
      (unsigned long long)samples,events,glass_custom_wake_evaluations(state));
    glass_custom_wake_destroy(state);
  }
  puts("WAKE_LONG_ASAN_OK: continuous state and irregular chunks; no negative wake events");
}
