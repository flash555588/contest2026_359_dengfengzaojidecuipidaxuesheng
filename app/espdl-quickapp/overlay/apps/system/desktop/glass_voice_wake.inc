/* SPDX-License-Identifier: Apache-2.0 */
/* A read-only observer of the PCM already owned by ASR. No second capture,
 * network request or UI calls. Missing models must never disable dictation. */
#ifdef GLASS_LOCAL_SPEECH
#include "glass_custom_wake.h"
#include "glass_speech.h"
#ifndef VOICE_WAKE_ASSET_ROOT
#define VOICE_WAKE_ASSET_ROOT "/sdcard/speech"
#endif
static struct glass_custom_wake *voice_wake_open(void)
{
  struct glass_custom_wake *wake=NULL;
  int ret=glass_custom_wake_create(VOICE_WAKE_ASSET_ROOT,&wake);
  if(ret)printf("[voice] wake observer unavailable result=%d\n",ret);
  return wake;
}
static void voice_wake_feed(struct glass_custom_wake **wake,const int16_t *pcm,size_t frames)
{
  if(!*wake)return;
  int ret=glass_custom_wake_feed(*wake,pcm,frames);
  if(ret>0)glass_speech_capture_woke();
  else if(ret<0){
    printf("[voice] wake observer disabled result=%d\n",ret);
    glass_custom_wake_destroy(*wake);*wake=NULL;
  }
}
static void voice_wake_close(struct glass_custom_wake *wake){glass_custom_wake_destroy(wake);}
#endif
