/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "glass_speech_assets.c"
static bool cancelled(void *unused){(void)unused;return true;}
static bool cancel_during_read(void *opaque){unsigned *calls=opaque;return ++*calls>=3;}
int main(int argc,char **argv)
{
  assert(argc==3);size_t n=0;void *p=glass_speech_asset(argv[1],"sanotts-zh/phonemes.json",&n);assert(p&&n);free(p);
  assert(!glass_speech_asset(argv[1],"../manifest.json",&n));
  assert(!glass_speech_asset(argv[2],"sanotts-zh/phonemes.json",&n));
  assert(!glass_speech_asset("/does-not-exist","sanotts-zh/phonemes.json",&n));
  assert(!glass_speech_asset_cancelable(argv[1],"sanotts-zh/front.bin",&n,cancelled,NULL));
  unsigned calls=0;
  assert(!glass_speech_asset_cancelable(argv[1],"sanotts-zh/front.bin",&n,cancel_during_read,&calls)&&calls==3);
  p=glass_speech_asset(argv[1],"sanotts-zh/phonemes.json",&n);assert(p&&n);free(p);
  puts("SPEECH_ASSETS_HOST_OK: valid asset, modified same-size asset, unknown path and missing file");
}
