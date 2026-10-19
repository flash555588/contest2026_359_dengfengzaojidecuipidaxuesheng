/* SPDX-License-Identifier: Apache-2.0 */
/* Production adapter, fake vendor engine: lifecycle and stream contracts. */
#include "glass_esp_tts.h"
#include "esp_tts.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const unsigned char glass_esp_voice_data[] = {0};
static char *syllables[] = {"ni3", "hao3"};
const esp_tts_voice_t esp_tts_voice_template = {.syll_num=2,.sylls=syllables};
static unsigned mode, calls, writes, samples, voices, handles;
static unsigned expected_speed=3;
static bool stopped;
static short pcm[600];
esp_tts_voice_t *esp_tts_voice_set_init(const esp_tts_voice_t *template, void *data)
{
  assert(template->sylls!=syllables && !memcmp(template->sylls,syllables,sizeof(syllables)));
  assert(data==glass_esp_voice_data);
  if(mode==1)return NULL;
  esp_tts_voice_t *v=calloc(1,sizeof(*v));assert(v);
  *v=*template;v->sample_rate=mode==2?22050:16000;v->bit_width=16;voices++;return v;
}
void esp_tts_voice_set_free(esp_tts_voice_t *voice){assert(voices==1&&!handles);voices--;free(voice);}
esp_tts_handle_t esp_tts_create(esp_tts_voice_t *voice)
{assert(voice&&voices==1);if(mode==3)return NULL;handles++;return malloc(1);}
void esp_tts_destroy(esp_tts_handle_t tts){assert(handles==1);handles--;free(tts);}
int esp_tts_parse_chinese(esp_tts_handle_t tts,const char *text)
{assert(tts&&text[0]);if(mode==4)return 0;if(mode==5)stopped=true;return 1;}
short *esp_tts_stream_play(esp_tts_handle_t tts,int *len,unsigned speed)
{
  assert(tts&&speed==expected_speed);calls++;
  if(mode==6){*len=-1;return pcm;}
  if(mode==7){*len=1;return NULL;}
  if(mode==8){*len=0;return NULL;}
  if(mode==10){*len=1;return pcm;}
  if(mode==11){*len=16000*180+1;return pcm;}
  const int lengths[]={8,542,1,0};*len=lengths[calls-1];return pcm;
}
static bool cancel(void *opaque){assert(opaque==&samples);return stopped;}
static int write_pcm(void *opaque,const int16_t *data,size_t frames)
{
  assert(opaque==&samples&&data==pcm&&frames);writes++;samples+=frames;
  if(mode==9)stopped=true;
  if(mode==13)assert(!glass_esp_tts_set_speed(5));
  return mode==12?-EIO:0;
}
static void run(unsigned kind,int expected)
{
  assert(!voices&&!handles);mode=kind;calls=writes=samples=0;stopped=false;
  assert(glass_esp_synthesize("你好，散步测试。",write_pcm,cancel,&samples)==expected);
  assert(!voices&&!handles);
}
int main(void)
{
  assert(glass_esp_tts_speed()==3);
  assert(glass_esp_tts_set_speed(6)==-EINVAL&&glass_esp_tts_speed()==3);
  for(unsigned speed=0;speed<=5;speed++){
    assert(!glass_esp_tts_set_speed(speed));expected_speed=speed;
    run(0,0);assert(samples==551);
  }
  expected_speed=3;assert(!glass_esp_tts_set_speed(3));
  run(13,0);assert(glass_esp_tts_speed()==5&&samples==551);
  assert(!glass_esp_tts_set_speed(3));
  run(0,0);assert(samples==551&&writes==3&&calls==4);
  run(1,-ENOMEM);run(2,-EBADMSG);run(3,-ENOMEM);run(4,-EBADMSG);
  run(5,-ECANCELED);assert(!writes&&!calls);
  run(6,-EBADMSG);run(7,-EBADMSG);run(8,-ENODATA);
  run(9,-ECANCELED);assert(writes==1&&samples==8);
  run(10,-ELOOP);assert(calls==10000);
  run(11,-EFBIG);assert(!writes);
  run(12,-EIO);assert(writes==1);
  stopped=true;assert(glass_esp_synthesize("你好",write_pcm,cancel,&samples)==-ECANCELED);
  assert(glass_esp_synthesize(NULL,write_pcm,NULL,NULL)==-EINVAL);
  assert(glass_esp_synthesize("",write_pcm,NULL,NULL)==-EINVAL);
  assert(glass_esp_synthesize("你好",NULL,NULL,NULL)==-EINVAL);
  char long_text[1026];memset(long_text,'1',sizeof(long_text)-1);long_text[1025]=0;
  assert(glass_esp_synthesize(long_text,write_pcm,NULL,NULL)==-EINVAL);
  run(0,0);assert(samples==551);
  puts("ESP_TTS_HOST_OK: stream order, rate, allocation/parse errors, cancel, callback error, malformed streams, limits and repeated cleanup; fake vendor engine");
}
