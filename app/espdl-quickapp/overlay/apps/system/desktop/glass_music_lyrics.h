/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "glass_music.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "glass_music_hans.h"

#define MUSIC_LRC_MAX 32768
#define MUSIC_LRC_LINES 192
struct music_lrc_line { unsigned ms; char text[MUSIC_LYRIC_TEXT_MAX],translation[MUSIC_LYRIC_TEXT_MAX]; };
struct music_lrc { unsigned count; bool foreign; struct music_lrc_line lines[MUSIC_LRC_LINES]; };

/* Reject malformed UTF-8 before handing external lyrics to the font engine. */
static bool music_lrc_utf8(const char *s, size_t n)
{
  for (size_t i=0;i<n;) {
    unsigned c=(unsigned char)s[i++], cp=c, extra=0, min=0;
    if (c<0x80) { if (c<32 && c!='\r' && c!='\n' && c!='\t') return false; continue; }
    if (c>=0xc2 && c<=0xdf) { extra=1; cp=c&31; min=0x80; }
    else if (c>=0xe0 && c<=0xef) { extra=2; cp=c&15; min=0x800; }
    else if (c>=0xf0 && c<=0xf4) { extra=3; cp=c&7; min=0x10000; }
    else return false;
    if (extra>n-i) return false;
    while (extra--) { c=(unsigned char)s[i++]; if ((c&0xc0)!=0x80) return false; cp=(cp<<6)|(c&63); }
    if (cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
  }
  return true;
}

static int music_lrc_compare(const void *a,const void *b)
{
  const struct music_lrc_line *x=a,*y=b;
  return x->ms<y->ms?-1:x->ms>y->ms;
}

/* Parse mm:ss[.f{1,3}], multiple timestamps, CRLF/BOM and a global offset.
 * Fixed limits keep malformed responses from consuming unbounded RAM. */
static int music_lrc_parse(const char *body,size_t size,struct music_lrc *out)
{
  if (!body || !out || !size || size>MUSIC_LRC_MAX || !music_lrc_utf8(body,size)) return -EINVAL;
  memset(out,0,sizeof(*out)); long offset=0;
  const char *end=body+size,*line=body;
  if (size>=3 && !memcmp(line,"\xef\xbb\xbf",3)) line+=3;
  while (line<end) {
    const char *e=memchr(line,'\n',end-line); if (!e) e=end;
    if (e>line && e[-1]=='\r') e--;
    const char *p=line;
    if((e-p>=13&&!memcmp(p,"[language:ja]",13))||
       (e-p>=13&&!memcmp(p,"[language:ko]",13)))out->foreign=true;
    if (e-p>9 && !memcmp(p,"[offset:",8)) {
      const char *v=p+8; bool neg=v<e && *v=='-'; if (v<e && (*v=='-' || *v=='+')) v++;
      long value=0; bool digits=false,valid=true;
      while (v<e && isdigit((unsigned char)*v)) { digits=true; value=value*10+(*v++-'0'); if (value>3600000) { valid=false; break; } }
      if (valid && digits && v<e && *v==']') offset=neg?-value:value;
    }
    unsigned stamps[16],count=0;
    while (p<e && *p=='[') {
      const char *v=p+1; unsigned mins=0,secs=0,fraction=0,digits=0;
      while (v<e && isdigit((unsigned char)*v) && digits<4) { mins=mins*10+(*v++-'0'); digits++; }
      if (!digits || v>=e || *v++!=':') break;
      if (e-v<3 || !isdigit((unsigned char)v[0]) || !isdigit((unsigned char)v[1])) break;
      secs=(v[0]-'0')*10+v[1]-'0'; v+=2; if (secs>=60) break;
      if (v<e && *v=='.') {
        v++; unsigned scale=100; digits=0;
        while (v<e && isdigit((unsigned char)*v) && digits<3) { fraction+=(*v++-'0')*scale; scale/=10; digits++; }
        if (!digits) break;
      }
      if (v>=e || *v++!=']') break;
      if (count>=16) return -E2BIG;
      stamps[count++]=(mins*60+secs)*1000+fraction; p=v;
    }
    if (count) {
      while (p<e && (*p==' ' || *p=='\t')) p++;
      while (e>p && (e[-1]==' ' || e[-1]=='\t')) e--;
      size_t n=e-p;
      if (n>=MUSIC_LYRIC_TEXT_MAX) { n=MUSIC_LYRIC_TEXT_MAX-1; while(n && ((unsigned char)p[n]&0xc0)==0x80)n--; }
      for (unsigned i=0;i<count;i++) {
        unsigned existing=0;
        while(existing<out->count&&out->lines[existing].ms!=stamps[i])existing++;
        if(existing<out->count){
          struct music_lrc_line *dest=&out->lines[existing];
          if(!dest->text[0]){memcpy(dest->text,p,n);dest->text[n]=0;
            if(music_hans_foreign(dest->text))out->foreign=true;}
          else if(n&&!dest->translation[0]&&(strlen(dest->text)!=n||memcmp(dest->text,p,n))){
            memcpy(dest->translation,p,n);dest->translation[n]=0;
          }
          continue;
        }
        if (out->count>=MUSIC_LRC_LINES) return -E2BIG;
        struct music_lrc_line *dest=&out->lines[out->count++]; dest->ms=stamps[i];
        memcpy(dest->text,p,n); dest->text[n]=0;
        if(music_hans_foreign(dest->text))out->foreign=true;
      }
    }
    line=memchr(line,'\n',end-line); if (!line) break; line++;
  }
  if (!out->count) return -ENODATA;
  qsort(out->lines,out->count,sizeof(out->lines[0]),music_lrc_compare);
  for (unsigned i=0;i<out->count;i++) {
    long long adjusted=(long long)out->lines[i].ms+offset;
    out->lines[i].ms=adjusted<0?0:(unsigned)adjusted;
  }
  /* Offsets can clamp several earlier lines to zero: retain the earliest
   * rather than interpreting these different timestamps as translations. */
  unsigned used=0;
  for (unsigned i=0;i<out->count;i++) {
    if (used && out->lines[used-1].ms==out->lines[i].ms) {
      if (!out->lines[used-1].text[0]) out->lines[used-1]=out->lines[i];
    } else out->lines[used++]=out->lines[i];
  }
  out->count=used; return 0;
}

static int music_lrc_translation(struct music_lrc *main,const char *body,size_t size)
{
  struct music_lrc *sub=calloc(1,sizeof(*sub));if(!sub)return -ENOMEM;
  int ret=music_lrc_parse(body,size,sub);
  if(!ret){unsigned j=0;
    for(unsigned i=0;i<main->count;i++){
      while(j<sub->count&&sub->lines[j].ms<main->lines[i].ms)j++;
      if(j==sub->count)break;
      const char *text=sub->lines[j].translation[0]?sub->lines[j].translation:sub->lines[j].text;
      if(main->lines[i].ms==sub->lines[j].ms&&!main->lines[i].translation[0]&&
         text[0]&&strcmp(text,main->lines[i].text))
        memcpy(main->lines[i].translation,text,strlen(text)+1);
    }
  }
  free(sub);return ret;
}
static void music_lrc_simplify(struct music_lrc *lrc)
{
  for(unsigned i=0;i<lrc->count;i++){
    if(!lrc->foreign)music_hans_convert(lrc->lines[i].text,MUSIC_LYRIC_TEXT_MAX);
    music_hans_convert(lrc->lines[i].translation,MUSIC_LYRIC_TEXT_MAX);
    if(!strcmp(lrc->lines[i].text,lrc->lines[i].translation))lrc->lines[i].translation[0]=0;
  }
}

static int music_lrc_index(const struct music_lrc *lrc,unsigned ms)
{
  unsigned lo=0,hi=lrc?lrc->count:0;
  while (lo<hi) { unsigned mid=lo+(hi-lo)/2; if (lrc->lines[mid].ms<=ms) lo=mid+1; else hi=mid; }
  return (int)lo-1;
}

static int music_metadata_url(const struct music_song *song,bool cover,char *url,size_t cap)
{
  const char *prefix=cover?"https://api.lrc.cx/cover?title=":"https://api.lrc.cx/jsonapi?title=";
  const char *values[]={song->name,song->artist,song->album};
  const char *keys[]={"","&artist=","&album="};
  size_t n=strlen(prefix);if(cap<=n)return -E2BIG;memcpy(url,prefix,n);
  static const char hex[]="0123456789ABCDEF";
  for(unsigned i=0;i<3;i++) {
    size_t k=strlen(keys[i]);if(k>=cap-n)return -E2BIG;memcpy(url+n,keys[i],k);n+=k;
    for(const unsigned char *p=(const unsigned char *)values[i];*p;p++) {
      bool plain=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-_.~",*p);
      if((plain?1u:3u)>=cap-n)return -E2BIG;
      if(plain)url[n++]=*p;else{url[n++]='%';url[n++]=hex[*p>>4];url[n++]=hex[*p&15];}
    }
  }
  url[n]=0;return 0;
}
static int music_lyrics_url(const struct music_song *song,char *url,size_t cap)
{return music_metadata_url(song,false,url,cap);}
static int music_cover_url(const struct music_song *song,char *url,size_t cap)
{return music_metadata_url(song,true,url,cap);}

/* Conservative identity match: do not display another artist's same-title song. */
static bool music_lyrics_identity(const char *a,const char *b)
{
  if(!a||!b||!*a||!*b)return false;
  while(*a && isspace((unsigned char)*a))a++;
  while(*b && isspace((unsigned char)*b))b++;
  size_t na=strlen(a),nb=strlen(b);
  while(na&&isspace((unsigned char)a[na-1]))na--;
  while(nb&&isspace((unsigned char)b[nb-1]))nb--;
  if(na!=nb)return false;
  for(size_t i=0;i<na;i++)if(tolower((unsigned char)a[i])!=tolower((unsigned char)b[i]))return false;
  return na>0;
}
