/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_pinyin.h"
#include "music_vendor/googlepinyin/include/pinyinime.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
using namespace ime_pinyin;
extern "C" {
extern const unsigned char glass_pinyin_dictionary[];
extern const unsigned int glass_pinyin_dictionary_size;
FILE *glass_pinyin_dictionary_open(void)
{ return fmemopen((void *)glass_pinyin_dictionary,glass_pinyin_dictionary_size,"rb"); }
}
static bool initialized;
static unsigned candidates;
static bool utf8(const char16 *text,char *out,size_t capacity)
{
  size_t n=0;
  if(!capacity) return false;
  for(;*text;text++) {
    unsigned c=*text, bytes=c<0x80?1:c<0x800?2:3;
    if(n+bytes>=capacity) { out[0]=0; return false; }
    if(bytes==1) out[n++]=(char)c;
    else {
      if(bytes==3) { out[n++]=(char)(0xe0|(c>>12)); out[n++]=(char)(0x80|((c>>6)&63)); }
      else out[n++]=(char)(0xc0|(c>>6));
      out[n++]=(char)(0x80|(c&63));
    }
  }
  out[n]=0; return true;
}
bool glass_pinyin_init(void)
{
  if(initialized) return true;
#ifdef __NuttX__
  mkdir("/data/qpk",0700); mkdir("/data/qpk/pinyin",0700);
  const char *user_path="/data/qpk/pinyin/user.dat";
#else
  const char *user_path="/tmp/music-ime-tests/user.dat";
#endif
  initialized=im_open_decoder("@builtin",user_path);
  if(initialized) { im_set_max_lens(32,16); im_reset_search(); }
  else im_close_decoder();
  printf("[ime] Google Pinyin dictionary=%u loaded=%d\n",glass_pinyin_dictionary_size,initialized);
  return initialized;
}
void glass_pinyin_reset(void)
{ if(initialized) im_reset_search(); candidates=0; }
unsigned glass_pinyin_search(const char *input)
{
  if(!initialized || !input || strlen(input)>32) return 0;
  candidates=(unsigned)im_search(input,strlen(input)); return candidates;
}
bool glass_pinyin_candidate(unsigned index,char *out,size_t capacity)
{
  char16 text[40]={0};
  if(capacity) out[0]=0;
  return initialized && index<candidates && im_get_candidate(index,text,40) && utf8(text,out,capacity);
}
bool glass_pinyin_choose(unsigned index,char *out,size_t capacity,unsigned *consumed)
{
  *consumed=0; if(capacity) out[0]=0;
  if(!initialized || index>=candidates) return false;
  size_t decoded=0; im_get_sps_str(&decoded);
  candidates=(unsigned)im_choose(index);
  const uint16 *starts=NULL;
  size_t syllables=im_get_spl_start_pos(starts);
  if(candidates==1 && im_get_fixed_len()==syllables) {
    if(!glass_pinyin_candidate(0,out,capacity)) return false;
    *consumed=(unsigned)decoded;
  }
  return true;
}
void glass_pinyin_flush(void) { if(initialized) im_flush_cache(); }
void glass_pinyin_close(void)
{ if(initialized) im_close_decoder(); initialized=false; candidates=0; }
