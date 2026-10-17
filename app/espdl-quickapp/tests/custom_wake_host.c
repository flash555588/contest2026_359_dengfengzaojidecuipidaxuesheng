/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_custom_wake.h"
#include "glass_wake_features.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static void run(const char *root,const char *path,unsigned expected)
{
  struct glass_custom_wake *s=NULL;assert(!glass_custom_wake_create(root,&s));
  FILE *f=fopen(path,"rb");assert(f);int16_t pcm[1023];size_t n;unsigned events=0;
  while((n=fread(pcm,2,1023,f))){int ret=glass_custom_wake_feed(s,pcm,n);assert(ret>=0);events+=ret;}
  assert(!ferror(f));fclose(f);memset(pcm,0,sizeof(pcm));
  for(unsigned i=0;i<20;i++){int ret=glass_custom_wake_feed(s,pcm,1023);assert(ret>=0);events+=ret;}
  printf("CUSTOM_WAKE_STREAM events=%u expected=%u evaluations=%u score=%.5f\n",events,expected,
    glass_custom_wake_evaluations(s),(double)glass_custom_wake_score(s));
  assert(events==expected);glass_custom_wake_reset(s);
  assert(glass_custom_wake_feed(s,pcm,4097)==-EINVAL);
  glass_custom_wake_destroy(s);
}
int main(int argc,char **argv)
{
  assert(argc==5);run(argv[1],argv[2],1);run(argv[1],argv[3],0);
  struct glass_custom_wake *s=(void *)1;assert(glass_custom_wake_create(argv[4],&s)==-EBADMSG&&s==NULL);
  int16_t silence[16000]={0};float features[GLASS_WAKE_INPUTS];
  assert(glass_wake_extract(silence,16000,features)==-ENODATA);
  assert(glass_wake_extract(silence,0,features)==-EINVAL);
  puts("CUSTOM_WAKE_HOST_OK: production model loader/hash, real streaming inference, positive/negative, corruption, silence, bounds; ASan/UBSan");
}
