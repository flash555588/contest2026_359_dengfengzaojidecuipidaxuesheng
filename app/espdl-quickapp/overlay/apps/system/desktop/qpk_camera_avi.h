/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RIFF AVI 1.0, RGB565 video and mono PCM16 audio, with an explicit index.
 * https://learn.microsoft.com/windows/win32/directshow/avi-riff-file-reference
 * Only the recording worker owns this writer and its already-open file. */
#define QPK_AVI_HEADER 338
#define QPK_AVI_INDEX_MAX 16384
#define QPK_AVI_LIMIT (256u * 1024u * 1024u)
struct qpk_avi_index {char id[4];uint32_t flags,offset,size;};
struct qpk_avi {
  FILE *file;
  struct qpk_avi_index *index;
  unsigned count,width,height,fps,rate,frames,audio_bytes;
};
static void qpk_avi_u16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void qpk_avi_u32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static int qpk_avi_write(FILE *f,const void *data,size_t n)
{
  const uint8_t *p=data;
  while(n){size_t part=n>16384?16384:n;if(fwrite(p,1,part,f)!=part)return errno?-errno:-EIO;p+=part;n-=part;}
  return 0;
}
static int qpk_avi_header(struct qpk_avi *a,uint32_t file_bytes,uint32_t movi_end)
{
  uint8_t h[QPK_AVI_HEADER]={0};
  unsigned frame_bytes=a->width*a->height*2;
  memcpy(h,"RIFF",4);qpk_avi_u32(h+4,file_bytes>=8?file_bytes-8:0);memcpy(h+8,"AVI LIST",8);
  qpk_avi_u32(h+16,306);memcpy(h+20,"hdrlavih",8);qpk_avi_u32(h+28,56);
  qpk_avi_u32(h+32,1000000/a->fps);qpk_avi_u32(h+36,frame_bytes*a->fps+a->rate*2);
  qpk_avi_u32(h+44,0x110);qpk_avi_u32(h+48,a->frames);qpk_avi_u32(h+56,2);
  qpk_avi_u32(h+60,frame_bytes);qpk_avi_u32(h+64,a->width);qpk_avi_u32(h+68,a->height);
  memcpy(h+88,"LIST",4);qpk_avi_u32(h+92,128);memcpy(h+96,"strlstrh",8);qpk_avi_u32(h+104,56);
  memcpy(h+108,"vidsDIB ",8);qpk_avi_u32(h+128,1);qpk_avi_u32(h+132,a->fps);
  qpk_avi_u32(h+140,a->frames);qpk_avi_u32(h+144,frame_bytes);qpk_avi_u32(h+148,UINT32_MAX);
  qpk_avi_u16(h+160,a->width);qpk_avi_u16(h+162,a->height);
  memcpy(h+164,"strf",4);qpk_avi_u32(h+168,52);qpk_avi_u32(h+172,40);
  qpk_avi_u32(h+176,a->width);qpk_avi_u32(h+180,a->height);qpk_avi_u16(h+184,1);qpk_avi_u16(h+186,16);
  qpk_avi_u32(h+188,3);qpk_avi_u32(h+192,frame_bytes);
  qpk_avi_u32(h+212,0xf800);qpk_avi_u32(h+216,0x07e0);qpk_avi_u32(h+220,0x001f);
  memcpy(h+224,"LIST",4);qpk_avi_u32(h+228,94);memcpy(h+232,"strlstrh",8);qpk_avi_u32(h+240,56);
  memcpy(h+244,"auds",4);qpk_avi_u32(h+264,2);qpk_avi_u32(h+268,a->rate*2);
  qpk_avi_u32(h+276,a->audio_bytes/2);qpk_avi_u32(h+280,4096);qpk_avi_u32(h+284,UINT32_MAX);qpk_avi_u32(h+288,2);
  memcpy(h+300,"strf",4);qpk_avi_u32(h+304,18);qpk_avi_u16(h+308,1);qpk_avi_u16(h+310,1);
  qpk_avi_u32(h+312,a->rate);qpk_avi_u32(h+316,a->rate*2);qpk_avi_u16(h+320,2);qpk_avi_u16(h+322,16);
  memcpy(h+326,"LIST",4);qpk_avi_u32(h+330,movi_end>=334?movi_end-334:4);memcpy(h+334,"movi",4);
  if(fseek(a->file,0,SEEK_SET))return -EIO;
  return qpk_avi_write(a->file,h,sizeof(h));
}
static int qpk_avi_begin(struct qpk_avi *a,FILE *file,unsigned w,unsigned h,unsigned fps,unsigned rate)
{
  if(!a||!file||!w||!h||w>1024||h>600||!fps||fps>30||rate<8000||rate>48000)return -EINVAL;
  memset(a,0,sizeof(*a));a->file=file;a->width=w;a->height=h;a->fps=fps;a->rate=rate;
  a->index=calloc(QPK_AVI_INDEX_MAX,sizeof(*a->index));if(!a->index)return -ENOMEM;
  int ret=qpk_avi_header(a,QPK_AVI_HEADER,QPK_AVI_HEADER);
  if(ret){free(a->index);a->index=NULL;}return ret;
}
static int qpk_avi_chunk(struct qpk_avi *a,const void *data,unsigned size,bool video)
{
  if(!a||!a->index||!data||!size||(video?size!=a->width*a->height*2:size%2))return -EINVAL;
  long pos=ftell(a->file);if(pos<QPK_AVI_HEADER)return -EIO;
  if(a->count==QPK_AVI_INDEX_MAX||(uint64_t)pos+size+8+(a->count+1)*16+8>QPK_AVI_LIMIT)return -EFBIG;
  uint8_t h[8];memcpy(h,video?"00db":"01wb",4);qpk_avi_u32(h+4,size);
  int ret=qpk_avi_write(a->file,h,8);if(!ret)ret=qpk_avi_write(a->file,data,size);
  if(!ret&&(size&1)){const uint8_t zero=0;ret=qpk_avi_write(a->file,&zero,1);}
  if(ret)return ret;
  struct qpk_avi_index *i=&a->index[a->count++];memcpy(i->id,h,4);i->flags=video?0x10:0;i->offset=pos-334;i->size=size;
  if(video)a->frames++;else a->audio_bytes+=size;return 0;
}
static int qpk_avi_finish(struct qpk_avi *a)
{
  if(!a||!a->index)return -EINVAL;
  long movi_end=ftell(a->file);int ret=movi_end<0?-EIO:0;
  uint8_t h[16];memcpy(h,"idx1",4);qpk_avi_u32(h+4,a->count*16);
  if(!ret)ret=qpk_avi_write(a->file,h,8);
  for(unsigned n=0;!ret&&n<a->count;n++){
    struct qpk_avi_index *i=&a->index[n];memcpy(h,i->id,4);qpk_avi_u32(h+4,i->flags);qpk_avi_u32(h+8,i->offset);qpk_avi_u32(h+12,i->size);
    ret=qpk_avi_write(a->file,h,16);
  }
  long end=ftell(a->file);if(end<0&&!ret)ret=-EIO;
  if(!ret)ret=qpk_avi_header(a,end,movi_end);
  if(!ret&&fflush(a->file))ret=-EIO;
  free(a->index);a->index=NULL;return ret;
}
