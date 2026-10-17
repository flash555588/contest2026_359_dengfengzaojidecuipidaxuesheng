/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_assets.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "speech_assets.h"
extern void qpk_dl_sha256(const void *data,size_t length,uint8_t digest[32]);
static unsigned long speech_asset_ms(void)
{
  struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
  return (unsigned long)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
void *glass_speech_asset_cancelable(const char *root,const char *relative,size_t *bytes,
                                   bool (*cancel)(void *),void *user)
{
  if(!root||!relative||!bytes)return NULL;
  for(unsigned i=0;i<sizeof(speech_assets)/sizeof(*speech_assets);i++)
    if(!strcmp(relative,speech_assets[i].path))
      {
        char path[256];struct stat st;
        if(snprintf(path,sizeof(path),"%s/%s",root,relative)>=(int)sizeof(path))return NULL;
        FILE *file=fopen(path,"rb");if(!file)return NULL;
        size_t size=speech_assets[i].bytes;
        if(fstat(fileno(file),&st)||st.st_size!=(off_t)size){fclose(file);return NULL;}
        if(cancel&&cancel(user)){fclose(file);return NULL;}
        uint8_t *data=memalign(64,size+1);if(!data){fclose(file);return NULL;}
        bool ok=true;size_t offset=0;unsigned long started=speech_asset_ms();
        while(offset<size){
          if(cancel&&cancel(user)){ok=false;break;}
          size_t n=size-offset;if(n>65536)n=65536;
          size_t got=fread(data+offset,1,n,file);if(got!=n){ok=false;break;}offset+=got;
        }
        fclose(file);
        unsigned long read_ms=speech_asset_ms()-started,hash_started=speech_asset_ms();
        uint8_t digest[32];if(ok)qpk_dl_sha256(data,size,digest);
        if(!ok||(cancel&&cancel(user))||memcmp(digest,speech_assets[i].sha,32)){free(data);return NULL;}
        if(!strncmp(relative,"sanotts-zh/",11))
          printf("[speech] tts asset=%s read_ms=%lu hash_ms=%lu\n",relative,read_ms,speech_asset_ms()-hash_started);
        data[size]=0;*bytes=size;return data;
      }
  return NULL;
}
void *glass_speech_asset(const char *root,const char *relative,size_t *bytes)
{return glass_speech_asset_cancelable(root,relative,bytes,NULL,NULL);}
