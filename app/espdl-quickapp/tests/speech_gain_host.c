/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_gain.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool cancelled(void *user){return *(bool *)user;}
int main(void)
{
  int16_t quiet[]={1000,-2000,1000,-2000},loud[]={3000,-6000,3000,-6000};unsigned a,b;
  assert(!glass_speech_normalize(quiet,4,NULL,NULL,&a));
  assert(!glass_speech_normalize(loud,4,NULL,NULL,&b));
  for(unsigned i=0;i<4;i++)assert(quiet[i]-loud[i]>=-1&&quiet[i]-loud[i]<=1);
  assert(a>b);
  int16_t extreme[]={-32768,32767,-32768,32767};
  assert(!glass_speech_normalize(extreme,4,NULL,NULL,&a));
  for(unsigned i=0;i<4;i++)assert(extreme[i]>=-30000&&extreme[i]<=30000);
  int16_t silence[]={0,0,0,0},low[]={1,-1,0,3};
  assert(!glass_speech_normalize(silence,4,NULL,NULL,&a)&&a==100);
  assert(!glass_speech_normalize(low,4,NULL,NULL,&a)&&low[0]==1&&low[1]==-1);
  int16_t small[]={200,-200};assert(!glass_speech_normalize(small,2,NULL,NULL,&a)&&a==1800&&small[0]==3600);
  bool stop=true;int16_t original[4];memcpy(original,loud,sizeof(loud));
  assert(glass_speech_normalize(loud,4,cancelled,&stop,&a)==-ECANCELED&&!memcmp(original,loud,sizeof(loud)));
  /* A single phrase with quiet and loud syllables must be leveled too. */
  enum {N=16000};int16_t *phrase=malloc(N*sizeof(*phrase));assert(phrase);
  for(unsigned i=0;i<N;i++)phrase[i]=(i&1?-1:1)*(i<N/2?1000:6000);
  assert(!glass_speech_normalize(phrase,N,NULL,NULL,&a));
  unsigned quiet_level=(unsigned)abs(phrase[3000]),loud_level=(unsigned)abs(phrase[12000]);
  assert(abs((int)quiet_level-(int)loud_level)<=10);
  /* In a constant waveform, abrupt gain changes would be audible clicks. */
  for(unsigned i=1;i<N;i++)assert(abs(abs(phrase[i])-abs(phrase[i-1]))<=80||i==N/2);
  /* The reported regression: a loud onset followed by a quiet word tail.
   * Keep the quiet tail audible and preserve pauses and total sample count. */
  for(unsigned i=0;i<N;i++)phrase[i]=(i&1?-1:1)*(i<N/2?6000:600);
  assert(!glass_speech_normalize(phrase,N,NULL,NULL,&a));
  assert(abs(abs(phrase[3000])-abs(phrase[12000]))<=20);
  for(unsigned i=0;i<N;i++)assert(phrase[i]!=0);
  for(unsigned i=0;i<N;i++)phrase[i]=(i>6000&&i<10000)?0:(i&1?600:-600);
  assert(!glass_speech_normalize(phrase,N,NULL,NULL,&a));
  for(unsigned i=6001;i<10000;i++)assert(phrase[i]==0);
  free(phrase);
  puts("SPEECH_GAIN_HOST_OK: fading tails retained; within/cross-phrase leveling; smooth gain; pauses and timing preserved; 18x combined gain bound; no clipping including -32768; cancellation");
}
