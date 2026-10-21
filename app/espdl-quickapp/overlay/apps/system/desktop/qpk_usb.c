/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/usb/board_usb.h>
#include "qpk_hardware.h"
#include "glass_portal.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef QPK_USB_MOUNT_ROOT
#define QPK_USB_MOUNT_ROOT "/mnt"
#endif
struct qpk_usb {int fd;bool mounted[BOARD_USB_SLOTS],writable[BOARD_USB_SLOTS];uint32_t generation[BOARD_USB_SLOTS];};
static const cJSON *get(const cJSON *o,const char *key){return cJSON_GetObjectItemCaseSensitive(o,key);}
static int number(const cJSON *a,const char *key,int fallback,int low,int high)
{const cJSON *v=get(a,key);if(!v)return fallback;return cJSON_IsNumber(v)&&v->valuedouble>=low&&v->valuedouble<=high&&v->valuedouble==v->valueint?v->valueint:-1;}
static const char *string(const cJSON *a,const char *key)
{const cJSON *v=get(a,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static void item(cJSON *o,const struct board_usb_info *v)
{
  cJSON_AddNumberToObject(o,"id",v->id);cJSON_AddNumberToObject(o,"generation",v->generation);
  cJSON_AddStringToObject(o,"kind",v->kind==BOARD_USB_MSC?"storage":v->kind==BOARD_USB_HID?"hid":"audio");
  cJSON_AddNumberToObject(o,"vid",v->vid);cJSON_AddNumberToObject(o,"pid",v->pid);cJSON_AddStringToObject(o,"endpoint",v->path);
  cJSON_AddBoolToObject(o,"connected",v->connected);cJSON_AddBoolToObject(o,"running",v->running);cJSON_AddNumberToObject(o,"error",v->error);
  cJSON_AddNumberToObject(o,"blocks",v->blocks);cJSON_AddNumberToObject(o,"blockSize",v->block_size);cJSON_AddNumberToObject(o,"protocol",v->protocol);
  cJSON_AddNumberToObject(o,"packets",v->packets);cJSON_AddNumberToObject(o,"bytes",v->bytes);cJSON_AddNumberToObject(o,"dropped",v->dropped);
  cJSON *formats=cJSON_AddArrayToObject(o,"formats");for(unsigned i=0;i<v->formats&&i<BOARD_USB_FORMATS;i++){
    const struct board_usb_format *f=v->format+i;cJSON *row=cJSON_CreateObject();cJSON_AddNumberToObject(row,"mode",i);
    cJSON_AddNumberToObject(row,"rate",f->rate);cJSON_AddNumberToObject(row,"channels",f->channels);cJSON_AddNumberToObject(row,"bits",f->bits);
    cJSON_AddStringToObject(row,"direction",f->input?"input":"output");cJSON_AddItemToArray(formats,row);}
}
static void mount_path(unsigned id,char *out,size_t bytes){snprintf(out,bytes,QPK_USB_MOUNT_ROOT "/usb%u",id);}
void qpk_usb_destroy(void *opaque)
{
  struct qpk_usb *s=opaque;if(!s)return;
  for(unsigned i=0;i<BOARD_USB_SLOTS;i++)if(s->mounted[i]){char path[32];mount_path(i,path,sizeof(path));(void)umount(path);}
  close(s->fd);free(s);
}
static int transfer(struct qpk_usb *s,const cJSON *a,struct board_usb_transfer *r,unsigned operation)
{
  int id=number(a,"id",-1,0,BOARD_USB_SLOTS-1),generation=number(a,"generation",-1,1,INT_MAX);
  if(id<0||generation<0)return -EINVAL;r->id=id;r->generation=generation;r->operation=operation;
  return ioctl(s->fd,BOARDUSBIOC_TRANSFER,(unsigned long)r)<0?-errno:0;
}
static int storage(struct qpk_usb *s,const char *op,const cJSON *a,cJSON *out,struct board_usb_transfer *r)
{
  int parsed_id=number(a,"id",-1,0,BOARD_USB_SLOTS-1),generation=number(a,"generation",-1,1,INT_MAX);
  if(parsed_id<0||generation<0)return -EINVAL;
  unsigned id=parsed_id;char root[32];mount_path(id,root,sizeof(root));
  if(!strcmp(op,"unmount")){
    if(!s->mounted[id]||s->generation[id]!=(uint32_t)generation)return -EBADF;
    if(umount(root))return -errno;s->mounted[id]=false;return 0;
  }
  int ret=transfer(s,a,r,BOARD_USB_INFO);if(ret)return ret;if(r->info.kind!=BOARD_USB_MSC)return -EINVAL;
  if(!strcmp(op,"mount")){
    if(s->mounted[id])return -EBUSY;const cJSON *write=get(a,"writable");if(write&&!cJSON_IsBool(write))return -EINVAL;
    bool writable=cJSON_IsTrue(write);if(mkdir(QPK_USB_MOUNT_ROOT,0755)&&errno!=EEXIST)return -errno;
    if(mkdir(root,0755)&&errno!=EEXIST)return -errno;
    if(mount(r->info.path,root,"vfat",writable?0:MS_RDONLY,NULL))return -errno;
    s->mounted[id]=true;s->writable[id]=writable;s->generation[id]=r->generation;
    cJSON_AddStringToObject(out,"path",root);cJSON_AddBoolToObject(out,"writable",writable);return 0;
  }
  if(!s->mounted[id]||s->generation[id]!=r->generation)return -EBADF;
  const char *relative=string(a,"path");if(!relative||strlen(relative)>192)return -EINVAL;
  char path[256];ret=portal_safe_path(root,relative,path,sizeof(path),false);if(ret)return ret;
  if(!strcmp(op,"files")){
    int offset=number(a,"offset",0,0,INT_MAX);if(offset<0)return -EINVAL;
    DIR *dir=opendir(path);if(!dir)return -errno;cJSON *items=cJSON_AddArrayToObject(out,"items");
    struct dirent *entry;unsigned seen=0,kept=0;bool more=false;
    while((entry=readdir(dir))){if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
      if(seen++<(unsigned)offset)continue;if(kept==16){more=true;break;}
      cJSON *row=cJSON_CreateObject();cJSON_AddStringToObject(row,"name",entry->d_name);cJSON_AddNumberToObject(row,"type",entry->d_type);cJSON_AddItemToArray(items,row);kept++;}
    closedir(dir);cJSON_AddBoolToObject(out,"more",more);cJSON_AddNumberToObject(out,"nextOffset",offset+kept);return 0;
  }
  if(!strcmp(op,"readFile")){
    int offset=number(a,"offset",0,0,INT_MAX),length=number(a,"length",512,1,BOARD_USB_BYTES);if(offset<0||length<0)return -EINVAL;
    int fd=open(path,O_RDONLY);if(fd<0)return -errno;struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return -EINVAL;}
    if(lseek(fd,offset,SEEK_SET)<0){ret=-errno;close(fd);return ret;}
    ssize_t n=read(fd,r->data,length);ret=n<0?-errno:0;close(fd);if(ret)return ret;
    cJSON *data=cJSON_AddArrayToObject(out,"data");for(ssize_t i=0;i<n;i++)cJSON_AddItemToArray(data,cJSON_CreateNumber(r->data[i]));
    cJSON_AddNumberToObject(out,"bytes",n);cJSON_AddNumberToObject(out,"fileBytes",(double)st.st_size);return 0;
  }
  if(!strcmp(op,"writeFile")){
    if(!s->writable[id])return -EROFS;
    const cJSON *data=get(a,"data"),*replace=get(a,"replace"),*v;
    if(!cJSON_IsArray(data)||cJSON_GetArraySize(data)>BOARD_USB_BYTES||(replace&&!cJSON_IsBool(replace)))return -EINVAL;
    int offset=number(a,"offset",0,0,INT_MAX);if(offset<0)return -EINVAL;
    unsigned size=0;cJSON_ArrayForEach(v,data){if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>255||v->valuedouble!=v->valueint)return -EINVAL;r->data[size++]=v->valueint;}
    int flags=O_WRONLY|(offset?0:O_CREAT|(cJSON_IsTrue(replace)?O_TRUNC:O_EXCL));
    int fd=open(path,flags,0600);if(fd<0)return -errno;
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||offset>st.st_size){close(fd);return -EINVAL;}
    if(lseek(fd,offset,SEEK_SET)<0){ret=-errno;close(fd);return ret;}
    ssize_t n=write(fd,r->data,size);ret=n==(ssize_t)size?0:n<0?-errno:-EIO;
    if(!ret&&fsync(fd))ret=-errno;if(close(fd)&&!ret)ret=-errno;
    if(!ret){cJSON_AddNumberToObject(out,"bytes",size);cJSON_AddStringToObject(out,"scope","written_not_application_readback");}return ret;
  }
  return -ENOSYS;
}
int qpk_usb_execute(void **opaque,const char *op,const cJSON *a,cJSON *out,struct qpk_hw_progress *progress)
{
  if(progress&&atomic_load(&progress->cancel))return -ECANCELED;
  if(!cJSON_IsObject(a))return -EINVAL;
  const char *allowed=NULL;
  if(!strcmp(op,"list"))allowed="";
  else if(!strcmp(op,"mode"))allowed=" port role ";
  else if(!strcmp(op,"info")||!strcmp(op,"hidRead")||!strcmp(op,"audioRead")||!strcmp(op,"audioStop")||!strcmp(op,"unmount"))allowed=" id generation ";
  else if(!strcmp(op,"hidReport"))allowed=" id generation reportType reportId data ";
  else if(!strcmp(op,"audioStart"))allowed=" id generation mode ";
  else if(!strcmp(op,"audioWrite"))allowed=" id generation data ";
  else if(!strcmp(op,"cdcRead"))allowed=" port ";
  else if(!strcmp(op,"cdcWrite"))allowed=" port data ";
  else if(!strcmp(op,"mount"))allowed=" id generation writable ";
  else if(!strcmp(op,"files"))allowed=" id generation path offset ";
  else if(!strcmp(op,"readFile"))allowed=" id generation path offset length ";
  else if(!strcmp(op,"writeFile"))allowed=" id generation path offset data replace ";
  if(!allowed)return -ENOSYS;
  const cJSON *field;cJSON_ArrayForEach(field,a){char key[48];
    if(!field->string||strspn(field->string,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")!=strlen(field->string)||
       snprintf(key,sizeof(key)," %s ",field->string)>=(int)sizeof(key)||!strstr(allowed,key))return -EINVAL;
  }
  struct qpk_usb *s=*opaque;
  if(!s){s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;s->fd=open("/dev/boardusb",O_RDWR);
    if(s->fd<0){int ret=-errno;free(s);return ret;}*opaque=s;}
  if(!strcmp(op,"list")){
    struct board_usb_list *list=calloc(1,sizeof(*list));if(!list)return -ENOMEM;
    int ret=ioctl(s->fd,BOARDUSBIOC_LIST,(unsigned long)list)<0?-errno:0;
    if(!ret){cJSON_AddNumberToObject(out,"version",list->version);cJSON_AddNumberToObject(out,"hsRole",list->hs_role);cJSON_AddNumberToObject(out,"fsRole",list->fs_role);
      cJSON_AddNumberToObject(out,"fsError",list->fs_error);cJSON *items=cJSON_AddArrayToObject(out,"items");
      for(unsigned i=0;i<list->count&&i<BOARD_USB_SLOTS;i++){cJSON *row=cJSON_CreateObject();item(row,list->items+i);cJSON_AddItemToArray(items,row);}}
    free(list);return ret;
  }
  if(!strcmp(op,"mode")){
    int port=number(a,"port",-1,0,1),role=number(a,"role",-1,0,2);if(port<0||role<0)return -EINVAL;
    struct board_usb_mode mode={.port=port,.role=role};return ioctl(s->fd,BOARDUSBIOC_MODE,(unsigned long)&mode)<0?-errno:0;
  }
  struct board_usb_transfer *r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;int ret=-ENOSYS;
  const char *names[]={"info","hidRead","hidReport","audioStart","audioRead","audioWrite","audioStop","cdcRead","cdcWrite"};
  int operation=-1;for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(op,names[i]))operation=i;
  if(operation<0){ret=storage(s,op,a,out,r);goto done;}
  bool write=operation==BOARD_USB_HID_REPORT||operation==BOARD_USB_AUDIO_WRITE||operation==BOARD_USB_CDC_WRITE;
  if(write){const cJSON *data=get(a,"data"),*value;if(!cJSON_IsArray(data)||cJSON_GetArraySize(data)>BOARD_USB_BYTES){ret=-EINVAL;goto done;}
    cJSON_ArrayForEach(value,data){if(!cJSON_IsNumber(value)||value->valuedouble<0||value->valuedouble>255||value->valuedouble!=value->valueint){ret=-EINVAL;goto done;}r->data[r->length++]=value->valueint;}
  }else r->length=BOARD_USB_BYTES;
  if(operation==BOARD_USB_HID_REPORT){int type=number(a,"reportType",-1,1,3),id=number(a,"reportId",0,0,255);if(type<0||id<0){ret=-EINVAL;goto done;}r->report_type=type;r->report_id=id;}
  if(operation==BOARD_USB_AUDIO_START){int mode=number(a,"mode",-1,0,BOARD_USB_FORMATS-1);if(mode<0){ret=-EINVAL;goto done;}r->mode=mode;}
  if(operation==BOARD_USB_CDC_READ||operation==BOARD_USB_CDC_WRITE){int port=number(a,"port",-1,0,1);if(port<0){ret=-EINVAL;goto done;}r->id=port;r->operation=operation;ret=ioctl(s->fd,BOARDUSBIOC_TRANSFER,(unsigned long)r)<0?-errno:0;}
  else ret=transfer(s,a,r,operation);
  if(!ret){if(operation==BOARD_USB_INFO)item(out,&r->info);
    else{cJSON_AddNumberToObject(out,"bytes",r->actual);if(!write){cJSON *data=cJSON_AddArrayToObject(out,"data");for(unsigned i=0;i<r->actual&&i<BOARD_USB_BYTES;i++)cJSON_AddItemToArray(data,cJSON_CreateNumber(r->data[i]));}}
    if(write)cJSON_AddStringToObject(out,"scope","submitted_not_application_readback");}
done:free(r);return ret;
}
