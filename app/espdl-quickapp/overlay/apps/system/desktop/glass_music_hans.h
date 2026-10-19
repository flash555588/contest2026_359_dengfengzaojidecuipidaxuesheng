/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "glass_music_hans_table.h"

/* Input is already checked by the strict LRC UTF-8 validator. */
static uint32_t music_hans_decode(const char **text)
{
  const unsigned char *p=(const unsigned char *)*text;uint32_t cp=*p++;
  unsigned more=cp<0x80?0:cp<0xe0?1:cp<0xf0?2:3;
  if(more)cp&=more==1?31:more==2?15:7;
  while(more--)cp=(cp<<6)|(*p++&63);
  *text=(const char *)p;return cp;
}
static unsigned music_hans_encode(uint32_t cp,char *out)
{
  if(cp<0x80){out[0]=cp;return 1;}
  if(cp<0x800){out[0]=0xc0|(cp>>6);out[1]=0x80|(cp&63);return 2;}
  if(cp<0x10000){out[0]=0xe0|(cp>>12);out[1]=0x80|((cp>>6)&63);out[2]=0x80|(cp&63);return 3;}
  out[0]=0xf0|(cp>>18);out[1]=0x80|((cp>>12)&63);out[2]=0x80|((cp>>6)&63);out[3]=0x80|(cp&63);return 4;
}
static bool music_hans_foreign(const char *text)
{
  if(strstr(text,"[language:ja]")||strstr(text,"[language:ko]"))return true;
  while(*text){uint32_t cp=music_hans_decode(&text);
    if((cp>=0x3040&&cp<=0x30ff)||(cp>=0x31f0&&cp<=0x31ff)||
       (cp>=0xff66&&cp<=0xff9f)||(cp>=0xac00&&cp<=0xd7a3)||
       (cp>=0x1100&&cp<=0x11ff)||(cp>=0x3130&&cp<=0x318f)||
       (cp>=0xa960&&cp<=0xa97f)||(cp>=0xd7b0&&cp<=0xd7ff))return true;
  }
  return false;
}
static void music_hans_convert(char *text,size_t capacity)
{
  char out[MUSIC_LYRIC_TEXT_MAX];size_t used=0;const char *p=text;
  if(!capacity||capacity>sizeof(out)||music_hans_foreign(text))return;
  while(*p){const char *after=p;uint32_t cp=music_hans_decode(&after);const char *replace=NULL;
    size_t lo=0,hi=sizeof(music_hans_phrases)/sizeof(music_hans_phrases[0]);
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(music_hans_phrases[mid].first<cp)lo=mid+1;else hi=mid;}
    for(size_t i=lo;i<sizeof(music_hans_phrases)/sizeof(music_hans_phrases[0])&&music_hans_phrases[i].first==cp;i++){
      size_t n=strlen(music_hans_phrases[i].from);
      if(!strncmp(p,music_hans_phrases[i].from,n)){replace=music_hans_phrases[i].to;after=p+n;break;}
    }
    char encoded[4];size_t n;
    if(replace)n=strlen(replace);
    else {
      lo=0;hi=sizeof(music_hans_chars)/sizeof(music_hans_chars[0]);
      while(lo<hi){size_t mid=lo+(hi-lo)/2;if(music_hans_chars[mid].from<cp)lo=mid+1;else hi=mid;}
      if(lo<sizeof(music_hans_chars)/sizeof(music_hans_chars[0])&&music_hans_chars[lo].from==cp)cp=music_hans_chars[lo].to;
      n=music_hans_encode(cp,encoded);replace=encoded;
    }
    if(n>=capacity-used)break;
    memcpy(out+used,replace,n);used+=n;p=after;
  }
  out[used]=0;memcpy(text,out,used+1);
}
