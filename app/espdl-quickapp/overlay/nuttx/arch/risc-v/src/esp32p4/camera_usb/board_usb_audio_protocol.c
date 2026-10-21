/* SPDX-License-Identifier: Apache-2.0 */
#include "board_usb_audio_protocol.h"
#include <errno.h>
#include <string.h>
static unsigned le16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t le24(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16;}
/* UAC1 PCM16, mono/stereo, bounded descriptor walk. Device-provided lengths,
 * interface numbers, frequency counts and endpoints are checked before use. */
int board_usb_audio_parse(const uint8_t *data,size_t bytes,unsigned control,struct board_usb_info *out)
{
  if(!data||!out||bytes<9||data[0]<9||data[1]!=2||le16(data+2)!=bytes)return -EINVAL;
  unsigned current=255,alternate=0,subclass=0,channels=0,count=0;
  bool collection[256]={0},pcm=false,rate_control=false,header=false;
  uint32_t rates[16];out->formats=0;
  for(size_t offset=0;offset<bytes;){
    if(bytes-offset<2)return -EINVAL;const uint8_t *p=data+offset;unsigned n=p[0];
    if(n<2||n>bytes-offset)return -EINVAL;
    if(p[1]==4){
      if(n<9)return -EINVAL;current=p[2];alternate=p[3];subclass=p[5]==1?p[6]:0;
      pcm=false;channels=count=0;rate_control=false;
    }else if(p[1]==0x24){
      if(n<3)return -EINVAL;
      if(current==control&&subclass==1&&p[2]==1){
        if(n<8||le16(p+3)!=0x100||p[7]>n-8)return -ENOTSUP;
        for(unsigned i=0;i<p[7];i++)collection[p[8+i]]=true;header=true;
      }else if(collection[current]&&subclass==2){
        if(p[2]==1){if(n<7)return -EINVAL;pcm=le16(p+5)==1;}
        else if(p[2]==2){
          if(n<8)return -EINVAL;channels=p[4];count=0;
          if(p[3]!=1||!channels||channels>2||p[5]!=2||p[6]!=16){channels=0;offset+=n;continue;}
          if(p[7]){if(p[7]>16||n<8+3*p[7])return -EINVAL;
            for(unsigned i=0;i<p[7];i++){uint32_t rate=le24(p+8+i*3);if(rate>=8000&&rate<=48000)rates[count++]=rate;}}
          else{if(n<14)return -EINVAL;uint32_t low=le24(p+8),high=le24(p+11);
            if(!low||high<low)return -EINVAL;const uint32_t choices[]={8000,16000,32000,44100,48000};
            for(unsigned i=0;i<5;i++)if(choices[i]>=low&&choices[i]<=high)rates[count++]=choices[i];}
        }
      }
    }else if(p[1]==5&&collection[current]&&subclass==2&&pcm&&channels&&alternate&&count){
      if(n<7)return -EINVAL;unsigned packet=le16(p+4)&0x7ff;
      if((p[3]&3)!=1||(le16(p+4)&0xf800)||!packet||packet>BOARD_USB_BYTES||p[6]<1||p[6]>16)return -ENOTSUP;
      /* Asynchronous speaker feedback needs its own clock-recovery engine. */
      if(!(p[2]&0x80)&&((p[3]>>2)&3)==1)return -ENOTSUP;
      for(unsigned i=0;i<count;i++){
        if(out->formats==BOARD_USB_FORMATS)return -E2BIG;
        struct board_usb_format *f=out->format+out->formats++;memset(f,0,sizeof(*f));
        f->rate=rates[i];f->channels=channels;f->bits=16;f->input=!!(p[2]&0x80);f->packet_bytes=packet;
        f->interface=current;f->alternate=alternate;f->endpoint=p[2];f->interval=p[6];f->rate_control=0;
      }
    }else if(p[1]==0x25&&collection[current]&&subclass==2){
      if(n<7)return -EINVAL;rate_control=!!(p[3]&1);
      for(unsigned i=0;i<out->formats;i++)if(out->format[i].interface==current&&out->format[i].alternate==alternate)out->format[i].rate_control=rate_control;
    }
    offset+=n;
  }
  return header&&out->formats?0:-ENOTSUP;
}
