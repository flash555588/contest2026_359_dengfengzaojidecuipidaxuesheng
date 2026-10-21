/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_camera_observation.h"
#include "glass_chat.h"
#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qpk_photo_storage.h"
#ifndef CAMERA_OBSERVATION_ROOT
#define CAMERA_OBSERVATION_ROOT "/sdcard/espclaw-observations"
#endif
#ifndef CAMERA_RECORDING_ROOT
#define CAMERA_RECORDING_ROOT "/sdcard/espclaw-recordings"
#endif
int glass_camera_recording_check(void)
{
  int ret=qpk_volume_mounted(QPK_VOLUME_SD);char directory[128];
  return ret?ret:qpk_photo_directory(directory,sizeof(directory),
    QPK_CLIP_FRAMES*QPK_CLIP_FRAME_LIMIT+8192,CAMERA_RECORDING_ROOT);
}
static int recording_write(const char *path,const void *data,size_t size,
  atomic_bool *cancel,bool *owned)
{
  *owned=false;
  int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return -errno;
  *owned=true;int ret=0;
  for(size_t offset=0;offset<size;){
    if(atomic_load(cancel)){ret=-ECANCELED;break;}
    ssize_t n=write(fd,(const char *)data+offset,size-offset);
    if(n<0&&errno==EINTR)continue;
    if(n<=0){ret=n<0?-errno:-EIO;break;}offset+=n;
  }
  if(!ret&&fsync(fd))ret=-errno;
  if(close(fd)&&!ret)ret=-errno;
  if(atomic_load(cancel))ret=-ECANCELED;
  return ret;
}
int glass_camera_recording_save(uint32_t id,const char *question,
  const struct qpk_camera_clip *clip,atomic_bool *cancel,char path[128])
{
  if(!path)return -EINVAL;
  path[0]=0;
  if(!id||!cancel||!clip||clip->count!=QPK_CLIP_FRAMES||
     clip->duration_ms<5000||clip->duration_ms>6000||
     !glass_chat_utf8(question,CHAT_PROMPT_BYTES,true))return -EINVAL;
  size_t bytes=8192;
  for(unsigned i=0;i<clip->count;i++){
    const struct qpk_clip_frame *f=&clip->frames[i];
    if(!f->jpeg||f->size<4||f->size>QPK_CLIP_FRAME_LIMIT||
       f->jpeg[0]!=0xff||f->jpeg[1]!=0xd8||f->jpeg[f->size-2]!=0xff||f->jpeg[f->size-1]!=0xd9||
       f->offset_ms>clip->duration_ms||(i&&f->offset_ms<=clip->frames[i-1].offset_ms))return -EINVAL;
    bytes+=f->size;
  }
  if(atomic_load(cancel))return -ECANCELED;
  int ret=qpk_volume_mounted(QPK_VOLUME_SD);char root[128],directory[96]={0},file[128];
  if(ret)return ret;
  ret=qpk_photo_directory(root,sizeof(root),bytes,CAMERA_RECORDING_ROOT);if(ret)return ret;
  bool directory_owned=false,owned[QPK_CLIP_FRAMES+1]={false};
  for(unsigned n=0;n<1000;n++){
    int length=snprintf(directory,sizeof(directory),"%s/record-%lu-%u",root,(unsigned long)id,n);
    if(length<0||length+16>=(int)sizeof(directory))return -ENAMETOOLONG;
    if(!mkdir(directory,0700)){directory_owned=true;break;}
    if(errno!=EEXIST)return -errno;
  }
  if(!directory_owned)return -EEXIST;
  cJSON *manifest=cJSON_CreateObject(),*frames=cJSON_AddArrayToObject(manifest,"frames");
  bool complete=manifest&&frames&&cJSON_AddStringToObject(manifest,"format","espclaw-temporal-jpeg-v1")&&
    cJSON_AddNumberToObject(manifest,"request_id",id)&&cJSON_AddNumberToObject(manifest,"duration_ms",clip->duration_ms)&&
    cJSON_AddStringToObject(manifest,"question",question);
  char *text=NULL;
  for(unsigned i=0;complete&&i<clip->count;i++){
    char name[24];snprintf(name,sizeof(name),"frame-%u.jpg",i+1);
    cJSON *frame=cJSON_CreateObject();if(!frame){complete=false;break;}
    cJSON_AddItemToArray(frames,frame);
    complete=cJSON_AddStringToObject(frame,"file",name)&&
      cJSON_AddNumberToObject(frame,"offset_ms",clip->frames[i].offset_ms)&&
      cJSON_AddNumberToObject(frame,"bytes",clip->frames[i].size);
  }
  text=complete?cJSON_PrintUnformatted(manifest):NULL;cJSON_Delete(manifest);
  if(!text){ret=-ENOMEM;goto done_recording;}
  for(unsigned i=0;i<clip->count;i++){
    snprintf(file,sizeof(file),"%s/frame-%u.jpg",directory,i+1);
    ret=recording_write(file,clip->frames[i].jpeg,clip->frames[i].size,cancel,&owned[i]);
    if(ret)goto done_recording;
  }
  /* Publish the manifest last. Its presence identifies a completed recording. */
  snprintf(file,sizeof(file),"%s/record.json",directory);
  ret=recording_write(file,text,strlen(text),cancel,&owned[QPK_CLIP_FRAMES]);
  if(!ret)snprintf(path,128,"%s",file);
done_recording:
  free(text);
  if(ret){
    for(unsigned i=0;i<QPK_CLIP_FRAMES;i++)if(owned[i]){
      snprintf(file,sizeof(file),"%s/frame-%u.jpg",directory,i+1);unlink(file);
    }
    if(owned[QPK_CLIP_FRAMES]){snprintf(file,sizeof(file),"%s/record.json",directory);unlink(file);}
    rmdir(directory);path[0]=0;
  }
  return ret;
}
int glass_camera_observation_save(uint32_t id,enum glass_agent_mode mode,
  const char *question,const char *answer,const struct qpk_camera_clip *clip,
  atomic_bool *cancel,char path[128])
{
  if(!path)return -EINVAL;
  path[0]=0;
  if(!id||!glass_agent_mode_valid(mode)||(clip&&clip->count!=3)||!cancel||
     !glass_chat_utf8(question,CHAT_PROMPT_BYTES,true)||!glass_chat_utf8(answer,clip?4096:2*1024*1024,true))return -EINVAL;
  if(atomic_load(cancel))return -ECANCELED;
  int mounted=qpk_volume_mounted(QPK_VOLUME_SD);if(mounted)return mounted;
  cJSON *root=cJSON_CreateObject(),*offsets=cJSON_AddArrayToObject(root,"offsets_ms");
  bool complete=root&&offsets&&cJSON_AddNumberToObject(root,"request_id",id)&&
    cJSON_AddNumberToObject(root,"mode",mode)&&cJSON_AddStringToObject(root,"scope",clip?"cloud_temporal_samples":"bot_conversation")&&
    cJSON_AddNumberToObject(root,"duration_ms",clip?clip->duration_ms:0)&&
    cJSON_AddStringToObject(root,"question",question)&&cJSON_AddStringToObject(root,"answer",answer);
  for(unsigned i=0;complete&&clip&&i<clip->count;i++){
    cJSON *n=cJSON_CreateNumber(clip->frames[i].offset_ms);if(!n)complete=false;else cJSON_AddItemToArray(offsets,n);
  }
  char *text=complete?cJSON_PrintUnformatted(root):NULL;cJSON_Delete(root);
  if(!text)return -ENOMEM;
  size_t size=strlen(text);char directory[128];
  int ret=qpk_photo_directory(directory,sizeof(directory),size,CAMERA_OBSERVATION_ROOT);
  int fd=-1;bool owned=false;
  if(ret)goto done;
  for(unsigned i=0;i<1000;i++){
    int n=snprintf(path,128,"%s/%s-%lu-%u.json",directory,clip?"camera":"dialog",(unsigned long)id,i);
    if(n<0||n>=128){ret=-ENAMETOOLONG;goto done;}
    fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd>=0){owned=true;break;}
    if(errno!=EEXIST){ret=-errno;goto done;}
  }
  if(fd<0){ret=-EEXIST;goto done;}
  for(size_t offset=0;offset<size;){
    if(atomic_load(cancel)){ret=-ECANCELED;break;}
    ssize_t n=write(fd,text+offset,size-offset);
    if(n<0&&errno==EINTR)continue;
    if(n<=0){ret=n<0?-errno:-EIO;break;}offset+=n;
  }
  if(!ret&&fsync(fd))ret=-errno;
  if(close(fd)&&!ret)ret=-errno;
  fd=-1;
  if(atomic_load(cancel))ret=-ECANCELED;
done:
  if(fd>=0)close(fd);
  if(ret){if(owned)unlink(path);path[0]=0;}
  free(text);return ret;
}
