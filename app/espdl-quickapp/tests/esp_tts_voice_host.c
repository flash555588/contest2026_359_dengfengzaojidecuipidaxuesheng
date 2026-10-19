/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_esp_tts.h"
#include "esp_tts_voice_config.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,stop_at;
static bool cancel(void *user){assert(user==&checks);return stop_at&&++checks>=stop_at;}
static void run(int expected)
{
  const unsigned char *data=(void *)1;void *owned=(void *)1;checks=0;
  assert(glass_esp_tts_voice_load(&data,&owned,cancel,&checks)==expected);
  if(expected)assert(!data&&!owned);else assert(data&&owned==data);
  glass_esp_tts_voice_release(owned);
}
int main(void)
{
  assert(!strcmp(glass_esp_tts_voice_name(),"xiaoxin"));
  assert(glass_esp_tts_voice_select("unknown")==-EINVAL);
  for(unsigned i=0;i<3;i++){
    assert(!glass_esp_tts_voice_select(glass_esp_tts_voices[i].name));run(0);
  }
  assert(!glass_esp_tts_voice_select("xiaoxin"));
  const unsigned char *borrowed;void *owner;
  assert(!glass_esp_tts_voice_load(&borrowed,&owner,NULL,NULL));
  assert(glass_esp_tts_voice_select("xiaole")==-EBUSY);
  glass_esp_tts_cache_clear();glass_esp_tts_voice_release(owner);
  run(0);
  assert(!rename(GLASS_ESP_TTS_VOICE_PATH,GLASS_ESP_TTS_VOICE_PATH ".saved"));
  run(0); /* Warm cache does not reopen or reread the SD file. */
  assert(!rename(GLASS_ESP_TTS_VOICE_PATH ".saved",GLASS_ESP_TTS_VOICE_PATH));
  glass_esp_tts_cache_clear();stop_at=1;run(-ECANCELED);stop_at=5;run(-ECANCELED);stop_at=0;
  FILE *file=fopen(GLASS_ESP_TTS_VOICE_PATH,"r+b");assert(file);
  int first=fgetc(file);rewind(file);fputc(first^1,file);fclose(file);run(-EBADMSG);
  file=fopen(GLASS_ESP_TTS_VOICE_PATH,"r+b");assert(file);fputc(first,file);fclose(file);run(0);glass_esp_tts_cache_clear();
  assert(!remove(GLASS_ESP_TTS_VOICE_PATH));run(-ENOENT);
  file=fopen(GLASS_ESP_TTS_VOICE_PATH,"wb");assert(file);fputs("short",file);fclose(file);run(-EBADMSG);
  puts("ESP_TTS_VOICE_HOST_OK: official SHA256; load/release; early/mid-read cancel; corruption; missing and truncated data; ASan/UBSan");
}
