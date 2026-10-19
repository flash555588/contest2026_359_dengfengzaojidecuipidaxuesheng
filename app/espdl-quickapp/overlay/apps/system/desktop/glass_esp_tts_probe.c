/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_esp_tts_probe.h"
#include "glass_esp_tts.h"
#include "glass_speech.h"
#include "glass_music_pcm.h"
#include "esp_tts.h"
#include "esp_tts_voice_template.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
extern void qpk_dl_sha256(const void *,size_t,uint8_t[32]);
static uint64_t esp_now(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int glass_esp_tts_probe(int mode,bool (*cancel)(void *),void *user)
{
  static const char *const texts[]={"你好，语音合成测试。","如果明天阳光很好，我们就去公园散步。","晚饭后，我们沿着河边散步，然后回家休息。"};
  if(mode<0||mode>2)return -EINVAL;
  unsigned speed=glass_esp_tts_speed();
  int ret=0;uint64_t begin=esp_now();
  /* The vendor template's syllable-pointer table is placed in flash by the
   * diagnostic builder. Give the runtime a private writable copy. */
  esp_tts_voice_t template=esp_tts_voice_template;
  if(template.syll_num<1||template.syll_num>4096||!template.sylls)return -EINVAL;
  const unsigned char *voice_data;void *voice_owned;
  ret=glass_esp_tts_voice_load(&voice_data,&voice_owned,cancel,user);if(ret)return ret;
  char **sylls=malloc((size_t)template.syll_num*sizeof(*sylls));
  if(!sylls){glass_esp_tts_voice_release(voice_owned);return -ENOMEM;}
  memcpy(sylls,template.sylls,(size_t)template.syll_num*sizeof(*sylls));template.sylls=sylls;
  esp_tts_voice_t *voice=esp_tts_voice_set_init(&template,(void *)voice_data);
  if(!voice){free(sylls);glass_esp_tts_voice_release(voice_owned);return -ENOMEM;}
  esp_tts_handle_t tts=esp_tts_create(voice);
  if(!tts){esp_tts_voice_set_free(voice);free(sylls);glass_esp_tts_voice_release(voice_owned);return -ENOMEM;}
  uint64_t init_us=esp_now()-begin;
  const size_t capacity=16000u*20u;
  int16_t *pcm=malloc(capacity*sizeof(*pcm));
  if(!pcm){ret=-ENOMEM;goto done;}
  printf("[esptts] voice=%s rate=%d bits=%d init_us=%llu speed=%u\n",voice->voice_name,voice->sample_rate,voice->bit_width,(unsigned long long)init_us,speed);fflush(stdout);usleep(50000);
  for(unsigned test=0;test<sizeof(texts)/sizeof(texts[0]);test++){
    if(cancel&&cancel(user)){ret=-ECANCELED;break;}
    printf("[esptts-stage] case=%u parse_begin\n",test);fflush(stdout);
    size_t count=0;bool got_first=false;uint64_t first=0,compute=0,started=esp_now();
    esp_tts_stream_reset(tts);
    uint64_t at=esp_now();int parsed=esp_tts_parse_chinese(tts,texts[test]);compute+=esp_now()-at;
    printf("[esptts-stage] case=%u parsed=%d\n",test,parsed);fflush(stdout);
    if(!parsed){ret=-EBADMSG;break;}
    struct music_pcm player;bool opened=false;
    for(unsigned calls=0;calls<10000;calls++){
      if(cancel&&cancel(user)){ret=-ECANCELED;break;}
      int length=0;at=esp_now();short *data=esp_tts_stream_play(tts,&length,speed);compute+=esp_now()-at;
      if(!calls){printf("[esptts-stage] case=%u first_chunk=%d\n",test,length);fflush(stdout);}
      if(length<0||(!data&&length)){ret=-EBADMSG;break;}
      if(!length)break;
      if((size_t)length>capacity-count){ret=-EFBIG;break;}
      if(!got_first){first=esp_now()-started;got_first=true;}
      memcpy(pcm+count,data,(size_t)length*sizeof(*pcm));count+=length;
      if(mode==1){
        if(!opened){ret=music_pcm_try_open(&player,16000,glass_speech_volume());if(ret)break;opened=true;}
        ret=music_pcm_write(&player,data,length,1);if(ret)break;
      }
      if(calls==9999)ret=-ELOOP;
    }
    if(opened){if(!ret&&!(cancel&&cancel(user)))ret=music_pcm_drain(&player);music_pcm_close(&player);}
    if(ret)break;
    if(!count){ret=-ENODATA;break;}
    uint8_t digest[32];char hash[65];qpk_dl_sha256(pcm,count*sizeof(*pcm),digest);
    for(unsigned j=0;j<32;j++)snprintf(hash+j*2,3,"%02x",digest[j]);
    printf("[esptts] case=%u samples=%u first_pcm_us=%llu compute_us=%llu\n",test,(unsigned)count,(unsigned long long)first,(unsigned long long)compute);fflush(stdout);usleep(50000);
    printf("[esptts-hash] case=%u sha256=%s\n",test,hash);fflush(stdout);usleep(50000);
    if(mode==2){
      char path[96],temporary[100];
      snprintf(path,sizeof(path),"/sdcard/speech/test/esp-tts-s%u-c%u.pcm",speed,test);
      snprintf(temporary,sizeof(temporary),"%s.tmp",path);
      FILE *file=fopen(temporary,"wb");
      if(!file){ret=-errno;break;}
      bool ok=fwrite(pcm,sizeof(*pcm),count,file)==count;
      if(fclose(file))ok=false;
      if(!ok){remove(temporary);ret=-EIO;break;}
      if(rename(temporary,path)){ret=-errno;remove(temporary);break;}
      printf("[esptts-file] case=%u speed=%u bytes=%u path=%s\n",test,speed,(unsigned)(count*2),path);
      continue;
    }
    /* Diagnostic transport only: export original PCM after inference timing.
     * Short throttled lines fit the USB console buffer and permit cancellation. */
    const unsigned char *bytes=(const unsigned char *)pcm;size_t total=count*sizeof(*pcm);
    for(size_t offset=0;offset<total;offset+=32){
      if(cancel&&cancel(user)){ret=-ECANCELED;break;}
      size_t n=total-offset;if(n>32)n=32;char hex[65];
      for(size_t j=0;j<n;j++)snprintf(hex+j*2,3,"%02x",bytes[offset+j]);
      printf("[esptts-pcm] case=%u offset=%u data=%s\n",test,(unsigned)offset,hex);fflush(stdout);usleep(5000);
    }
    if(ret)break;
    printf("[esptts] case=%u exported=%u\n",test,(unsigned)total);fflush(stdout);usleep(50000);
  }
  free(pcm);
done:
  esp_tts_destroy(tts);esp_tts_voice_set_free(voice);free(sylls);glass_esp_tts_voice_release(voice_owned);
  printf("[esptts] done result=%d\n",ret);fflush(stdout);return ret;
}
