/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_MUSIC_SD_H
#define GLASS_MUSIC_SD_H
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifndef MUSIC_SD_ROOT
#define MUSIC_SD_ROOT "/sdcard/music/"
#endif
static bool music_sd_path(const char *path)
{
  const size_t prefix=sizeof(MUSIC_SD_ROOT)-1;
  if(strncmp(path,MUSIC_SD_ROOT,prefix))return false;
  const char *name=path+prefix;size_t n=strlen(name);
  if(n<5||n>180||strchr(name,'/')||strchr(name,'\\')||strstr(name,"..")||strcmp(name+n-4,".mp3"))return false;
  for(const unsigned char *p=(const unsigned char *)name;*p;p++)if(*p<32||*p==127)return false;
  return true;
}
static int music_sd_stream(const char *path,music_data_cb sink,void *ctx,music_cancel_cb cancel)
{
  if(!music_sd_path(path)||!sink||!cancel)return -EINVAL;
  FILE *f=fopen(path,"rb");if(!f)return -errno;
  struct stat st;int ret=0;
  if(fstat(fileno(f),&st)||!S_ISREG(st.st_mode))ret=-EINVAL;
  /* Stack is bounded independently of track size. Cancellation passes through
   * the existing ring's back-pressure and generation checks. */
  unsigned char data[4096];
  while(!ret){
    if(cancel(ctx)){ret=-ECANCELED;break;}
    size_t got=fread(data,1,sizeof(data),f);
    if(got)ret=sink(ctx,data,got);
    if(got<sizeof(data)){if(ferror(f))ret=-EIO;break;}
  }
  fclose(f);return ret;
}
#endif
