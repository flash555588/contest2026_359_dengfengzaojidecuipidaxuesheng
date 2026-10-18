/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_MUSIC_TRACK_KEY_H
#define GLASS_MUSIC_TRACK_KEY_H
#include <stdint.h>
static uint32_t music_track_key(const char *name,const char *artist)
{
  uint32_t h=2166136261u;const char *s[2]={name,artist};
  for(unsigned i=0;i<2;i++){const unsigned char *p=(const unsigned char *)s[i];do{h=(h^*p)*16777619u;}while(*p++);}
  return h;
}
#endif
