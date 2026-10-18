/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../overlay/apps/system/desktop/glass_music_pv_assets.h"
static void store(const char *path,const char *text)
{FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
int main(int argc,char **argv)
{
  assert(argc==3);struct music_pv_assets assets;
  assert(music_pv_assets_load(&assets,argv[1])&&assets.count==27);
  assert(assets.styles[0].dark.background==0x060607);
  assert(assets.styles[1].light.foreground!=assets.styles[1].light.background);
  const char *invalid[]={"{}","[]","null","{\"version\":2,\"styles\":[]}",
    "{\"version\":1,\"styles\":[{}]}","{\"version\":1,\"styles\":[{\"name\":\"x\",\"light\":{}}]}",
    "{\"version\":1,\"styles\":[{\"name\":\"\\n\"}]}","[[[[[[[[]]]]]]]]","{}trailing"};
  for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){store(argv[2],invalid[i]);assert(!music_pv_assets_load(&assets,argv[2])&&!assets.count);}
  FILE *f=fopen(argv[2],"wb");assert(f);for(unsigned i=0;i<32769;i++)fputc(' ',f);fclose(f);
  assert(!music_pv_assets_load(&assets,argv[2])&&!assets.count);
  f=fopen(argv[2],"wb");assert(f);fwrite("{}\0{}",1,5,f);fclose(f);
  assert(!music_pv_assets_load(&assets,argv[2])&&!assets.count);
  assert(music_pv_assets_load(&assets,argv[1])&&assets.count==27);
  puts("MUSIC_PV_SD_ASSETS_PASS 27 styles; malformed, deep, oversized and NUL inputs rejected");return 0;
}
