/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *asset_root;
static unsigned events;
void glass_speech_capture_woke(void){events++;}
#define GLASS_LOCAL_SPEECH 1
#define VOICE_WAKE_ASSET_ROOT asset_root
#include "glass_voice_wake.inc"
static void replay(const char *path,unsigned expected)
{
  struct glass_custom_wake *wake=voice_wake_open();assert(wake);
  FILE *file=fopen(path,"rb");assert(file);int16_t pcm[496],original[496];size_t frames;unsigned before=events;
  while((frames=fread(pcm,sizeof(*pcm),496,file))>0){
    memcpy(original,pcm,frames*sizeof(*pcm));voice_wake_feed(&wake,pcm,frames);
    assert(!memcmp(original,pcm,frames*sizeof(*pcm))&&wake);
  }
  assert(!ferror(file));fclose(file);voice_wake_close(wake);assert(events-before==expected);
}
int main(int argc,char **argv)
{
  assert(argc==4);asset_root=argv[1];replay(argv[2],1);replay(argv[3],0);
  struct glass_custom_wake *wake=voice_wake_open();assert(wake);int16_t pcm[496]={0};unsigned before=events;
  voice_wake_feed(&wake,pcm,4097);assert(!wake&&events==before);
  voice_wake_feed(&wake,pcm,496);voice_wake_close(wake);
  asset_root="/no-such-wake-resources";wake=voice_wake_open();assert(!wake);
  voice_wake_feed(&wake,pcm,496);voice_wake_close(wake);assert(events==before);
  puts("VOICE_WAKE_OBSERVER_OK real detector; positive/negative; ASR PCM unchanged; missing model and inference error remain optional; no second capture");
  return 0;
}
