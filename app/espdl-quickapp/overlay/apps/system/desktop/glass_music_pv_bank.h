/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_MUSIC_PV_BANK_H
#define GLASS_MUSIC_PV_BANK_H
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "glass_music_track_key.h"
#include "qpk_mjpeg.h"
#if defined(__GNUC__) && !defined(__clang__)
#define JPV_HOT __attribute__((optimize("O3")))
#else
#define JPV_HOT
#endif

/* Independently seekable, checked RGB565 frames; bounded JPEG input, no font,
 * JS engine or growing frame index is retained in RAM. */
#define JPV_WIDTH 640u
#define JPV_HEIGHT 360u
#define JPV_PIXELS (JPV_WIDTH*JPV_HEIGHT)
#define JPV_BYTES (JPV_PIXELS*2u)
#define JPV_HEADER 288u
#define JPV_FRAME_MAX 14400u
#ifndef MUSIC_PV_BANK_ROOT
#define MUSIC_PV_BANK_ROOT "/sdcard/jizura/pv"
#endif
#ifndef MUSIC_PV_FAST_ROOT
#define MUSIC_PV_FAST_ROOT "/sdcard/jizura/pv-jpeg"
#endif
struct jpv_bank { int fd; uint32_t frames,fps,duration,size,cursor,index_first,index_count;bool indexed;unsigned char index[64*12]; };
struct jpv_reader { int fd; unsigned char data[32768]; unsigned char *jpeg;void *decoder;unsigned at,used,left; uint64_t io_us,check_us;bool (*cancel)(void *);void *opaque; };
static void jpv_reader_release(struct jpv_reader *r)
{free(r->jpeg);qpk_mjpeg_destroy(r->decoder);r->jpeg=NULL;r->decoder=NULL;}
static uint64_t jpv_micros(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
static int jpv_read_all(int fd,void *buffer,unsigned count)
{
  unsigned char *out=buffer;
  while(count){ssize_t n=read(fd,out,count);if(n<0&&errno==EINTR)continue;if(n<=0)return -EIO;out+=n;count-=n;}
  return 0;
}
static uint32_t jpv_u32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
#define jpv_key music_track_key
static int jpv_open(struct jpv_bank *b,const char *path,const char *name,const char *artist,unsigned style)
{
  memset(b,0,sizeof(*b));b->fd=-1;int fd=open(path,O_RDONLY);if(fd<0)return -errno;
  unsigned char h[JPV_HEADER];struct stat st;int ret=-EINVAL;
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<JPV_HEADER||st.st_size>INT32_MAX)goto fail;
  if(jpv_read_all(fd,h,sizeof(h)))goto fail;
  if(memcmp(h,"JZPV01\0\0",8)||jpv_u32(h+8)!=1||jpv_u32(h+12)!=JPV_WIDTH||jpv_u32(h+16)!=JPV_HEIGHT||jpv_u32(h+32)!=style)goto fail;
  b->fps=jpv_u32(h+20);b->frames=jpv_u32(h+24);b->duration=jpv_u32(h+28);
  if(!b->fps||b->fps>24||!b->frames||b->frames>JPV_FRAME_MAX||
     b->duration!=(uint64_t)b->frames*1000/b->fps||
     (uint64_t)JPV_HEADER+b->frames*12u>(uint64_t)st.st_size)goto fail;
  if(!memchr(h+36,0,128)||!memchr(h+164,0,96)||strcmp((char *)h+36,name)||strcmp((char *)h+164,artist))goto fail;
  b->indexed=!memcmp(h+260,"IDX2",4);
  if(!b->indexed&&jpv_u32(h+260))goto fail;
  b->fd=fd;b->size=(uint32_t)st.st_size;b->cursor=JPV_HEADER;return 0;
fail: close(fd);memset(b,0,sizeof(*b));b->fd=-1;return ret;
}
static int jpv_byte(struct jpv_reader *r)
{
  if(r->at==r->used){
    if(r->cancel&&r->cancel(r->opaque))return -1;
    unsigned n=r->left>sizeof(r->data)?sizeof(r->data):r->left;
    if(!n)return -1;
    uint64_t began=jpv_micros();int ret=jpv_read_all(r->fd,r->data,n);r->io_us+=jpv_micros()-began;
    if(ret)return -1;
    r->left-=n;r->at=0;r->used=n;
  }
  return r->data[r->at++];
}
static int jpv_word(struct jpv_reader *r)
{int a=jpv_byte(r),b=jpv_byte(r);return a<0||b<0?-1:a|(b<<8);}
static int jpv_copy(struct jpv_reader *r,unsigned char *out,unsigned count)
{
  while(count){
    if(r->at==r->used){int first=jpv_byte(r);if(first<0)return -EIO;*out++=first;count--;continue;}
    unsigned n=r->used-r->at;if(n>count)n=count;memcpy(out,r->data+r->at,n);out+=n;r->at+=n;count-=n;
  }
  return 0;
}
/* Raw LZ4 block, with every length, back-reference and terminal boundary
 * checked before touching the destination. No extra dictionary allocation. */
static JPV_HOT int jpv_lz4(struct jpv_reader *r,unsigned char *pixels,unsigned limit)
{
  unsigned out=0;
  while(r->left||r->at<r->used){
    int token=jpv_byte(r);if(token<0)return -EIO;unsigned literal=token>>4;
    if(literal==15){int n;do{n=jpv_byte(r);if(n<0||literal>limit-(unsigned)n)return -EINVAL;literal+=n;}while(n==255);}
    if(literal>limit-out)return -EINVAL;
    int ret=jpv_copy(r,pixels+out,literal);if(ret)return ret;out+=literal;
    if(!r->left&&r->at==r->used)return out==limit?0:-EINVAL;
    int offset=jpv_word(r);if(offset<=0||(unsigned)offset>out)return -EINVAL;
    unsigned match=(token&15)+4;
    if((token&15)==15){int n;do{n=jpv_byte(r);if(n<0||match>limit-(unsigned)n)return -EINVAL;match+=n;}while(n==255);}
    if(match>limit-out)return -EINVAL;
    while(match--){pixels[out]=pixels[out-offset];out++;}
  }
  return -EINVAL;
}
static uint32_t jpv_adler(const unsigned char *p,unsigned size)
{
  uint32_t a=1,b=0;
  while(size){unsigned n=size>5552?5552:size;size-=n;while(n--){a+=*p++;b+=a;}a%=65521;b%=65521;}
  return b<<16|a;
}
static JPV_HOT int jpv_frame(struct jpv_bank *b,unsigned frame,uint16_t *pixels,struct jpv_reader *r)
{
  if(!b||!pixels||!r)return -EINVAL;
  r->io_us=r->check_us=0;
  if(b->fd<0||frame>=b->frames)return -ERANGE;
  uint32_t table=JPV_HEADER+b->frames*12;
  if(!b->index_count||frame<b->index_first||frame>=b->index_first+b->index_count){
    unsigned first=frame/64*64,count=b->frames-first;if(count>64)count=64;
    unsigned at=JPV_HEADER+first*12;
    if(lseek(b->fd,at,SEEK_SET)!=(off_t)at||jpv_read_all(b->fd,b->index,count*12))return -EIO;
    b->index_first=first;b->index_count=count;b->cursor=at+count*12;
  }
  const unsigned char *index=b->index+(frame-b->index_first)*12;
  uint32_t offset=jpv_u32(index),encoded=jpv_u32(index+4),checksum=jpv_u32(index+8);
  unsigned codec=encoded>>30,length=encoded&0x3fffffffu;
  unsigned decoded=b->indexed?512+JPV_PIXELS/4:JPV_BYTES;
  if(offset<table||!length||length>decoded+64u||offset>b->size||length>b->size-offset||
     (b->indexed&&codec!=1&&codec!=2)||(codec==3&&length>131072)||(codec==2&&length!=decoded))return -EINVAL;
  if(b->cursor!=offset&&lseek(b->fd,offset,SEEK_SET)!=(off_t)offset)return -EIO;
  b->cursor=UINT32_MAX;
  r->fd=b->fd;r->at=r->used=0;r->left=length;
  if(codec==3){
    if(!r->jpeg)r->jpeg=malloc(131072);if(!r->decoder)r->decoder=qpk_mjpeg_create();
    if(!r->jpeg||!r->decoder)return -ENOMEM;
    int ret=jpv_copy(r,r->jpeg,length);if(ret)return ret;
    uint64_t began=jpv_micros();ret=jpv_adler(r->jpeg,length)==checksum?0:-EIO;r->check_us=jpv_micros()-began;
    if(ret)return ret;b->cursor=offset+length;
    return qpk_mjpeg_decode_still_fit(r->decoder,r->jpeg,length,pixels,JPV_WIDTH,JPV_HEIGHT);
  }
  if(codec==2){int ret=jpv_copy(r,(unsigned char *)pixels,decoded);if(ret)return ret;}
  else if(codec==1){int ret=jpv_lz4(r,(unsigned char *)pixels,decoded);if(ret)return ret;}
  else{
    unsigned out=0;
    while(out<JPV_PIXELS){
      int control=jpv_word(r);if(control<0)return -EIO;
      unsigned count=(control&32767)+1;if(count>JPV_PIXELS-out)return -EINVAL;
      if(control&32768){int value=jpv_word(r);if(value<0)return -EIO;while(count--)pixels[out++]=(uint16_t)value;}
      else while(count--){int value=jpv_word(r);if(value<0)return -EIO;pixels[out++]=(uint16_t)value;}
    }
    if(r->left||r->at!=r->used)return -EINVAL;
  }
  b->cursor=offset+length;
  uint64_t began=jpv_micros();int ret=jpv_adler((const unsigned char *)pixels,decoded)==checksum?0:-EIO;r->check_us=jpv_micros()-began;
  if(ret||!b->indexed)return ret;
  uint16_t palette[256];memcpy(palette,pixels,sizeof(palette));
  unsigned char *indices=(unsigned char *)pixels;
  memmove(indices,indices+512,JPV_PIXELS/4);
  /* Remove the palette prefix before backwards expansion so destination
   * writes cannot overwrite unread indices in the first source row. */
  for(unsigned remaining=JPV_HEIGHT/2;remaining>0;){
    unsigned y=--remaining;
    uint16_t *row=pixels+(size_t)y*2*JPV_WIDTH;
    for(unsigned x=JPV_WIDTH/2;x-- >0;){uint16_t v=palette[indices[y*(JPV_WIDTH/2)+x]];row[x*2]=v;row[x*2+1]=v;}
    memcpy(row+JPV_WIDTH,row,JPV_WIDTH*2);
  }
  return 0;
}
#undef JPV_HOT
#endif
