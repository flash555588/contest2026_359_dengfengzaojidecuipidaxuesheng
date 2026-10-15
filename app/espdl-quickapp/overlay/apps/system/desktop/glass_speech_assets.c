/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_assets.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include "speech_assets.h"
extern void qpk_dl_sha256(const void *data,size_t length,uint8_t digest[32]);
void *glass_speech_asset(const char *root,const char *relative,size_t *bytes)
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
        uint8_t *data=memalign(64,size+1);if(!data){fclose(file);return NULL;}
        bool ok=fread(data,1,size,file)==size;fclose(file);
        uint8_t digest[32];if(ok)qpk_dl_sha256(data,size,digest);
        if(!ok||memcmp(digest,speech_assets[i].sha,32)){free(data);return NULL;}
        data[size]=0;*bytes=size;return data;
      }
  return NULL;
}
