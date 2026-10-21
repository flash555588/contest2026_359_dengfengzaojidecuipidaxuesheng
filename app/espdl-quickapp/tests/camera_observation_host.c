/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool write_fails,cancel_on_sync;
static unsigned write_calls,fail_write_call;
static atomic_bool cancel;
static ssize_t fixture_write(int fd,const void *data,size_t size)
{write_calls++;if(write_fails||(fail_write_call&&write_calls==fail_write_call)){errno=ENOSPC;return -1;}return write(fd,data,size);}
static int fixture_fsync(int fd)
{int ret=fsync(fd);if(cancel_on_sync)atomic_store(&cancel,true);return ret;}
#define write fixture_write
#define fsync fixture_fsync
#include "../overlay/apps/system/desktop/glass_camera_observation.c"
#undef write
#undef fsync
static unsigned count_files(void)
{DIR *d=opendir(CAMERA_OBSERVATION_ROOT);assert(d);unsigned n=0;struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.')n++;closedir(d);return n;}
static unsigned count_recordings(void)
{DIR *d=opendir(CAMERA_RECORDING_ROOT);assert(d);unsigned n=0;struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.')n++;closedir(d);return n;}
int main(void)
{
 struct qpk_camera_clip clip={.count=3,.duration_ms=5010};for(unsigned i=0;i<3;i++)clip.frames[i].offset_ms=i*2000;
 char first[128],second[128],path[128];
 assert(!glass_camera_observation_save(7,AGENT_MODE_CHAT,"question","first answer",&clip,&cancel,first));
 assert(!glass_camera_observation_save(7,AGENT_MODE_CHAT,"question","second answer",&clip,&cancel,second));
 assert(strcmp(first,second)&&count_files()==2);
 FILE *f=fopen(first,"r");assert(f);char text[8192];size_t n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
 cJSON *o=cJSON_Parse(text);assert(o&&!strcmp(cJSON_GetObjectItemCaseSensitive(o,"answer")->valuestring,"first answer"));
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o,"offsets_ms"))==3&&!strstr(text,"base64")&&!strstr(text,"jpeg"));cJSON_Delete(o);
 write_fails=true;assert(glass_camera_observation_save(8,AGENT_MODE_CHAT,"q","a",&clip,&cancel,path)==-ENOSPC&&!path[0]&&count_files()==2);write_fails=false;
 cancel_on_sync=true;assert(glass_camera_observation_save(8,AGENT_MODE_CHAT,"q","a",&clip,&cancel,path)==-ECANCELED&&!path[0]&&count_files()==2);cancel_on_sync=false;
 assert(glass_camera_observation_save(8,AGENT_MODE_CHAT,"q","a",&clip,&cancel,path)==-ECANCELED&&!path[0]);atomic_store(&cancel,false);
 char moved[128];snprintf(moved,sizeof(moved),"%s.saved",CAMERA_OBSERVATION_ROOT);
 assert(!rename(CAMERA_OBSERVATION_ROOT,moved)&&!symlink(moved,CAMERA_OBSERVATION_ROOT));
 assert(glass_camera_observation_save(8,AGENT_MODE_CHAT,"q","a",&clip,&cancel,path)==-EPERM&&!path[0]);
 assert(!unlink(CAMERA_OBSERVATION_ROOT)&&!rename(moved,CAMERA_OBSERVATION_ROOT));
 assert(!glass_camera_observation_save(9,AGENT_MODE_ASSISTANT,"voice question","voice answer",NULL,&cancel,path));
 f=fopen(path,"r");assert(f);n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
 o=cJSON_Parse(text);assert(o&&!strcmp(cJSON_GetObjectItemCaseSensitive(o,"scope")->valuestring,"bot_conversation"));
 assert(!cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o,"offsets_ms")));cJSON_Delete(o);
 char reply[CHAT_PAGE_BYTES*3];memset(reply,'x',sizeof(reply)-1);reply[sizeof(reply)-1]=0;
 char name[48];uint32_t bytes;assert(!glass_chat_archive_session(reply,9,name,&bytes)&&bytes==strlen(reply));
 assert(!strncmp(name,"sd-reply-",9)&&!glass_chat_page_request(name,1));
 struct chat_page page={0};for(unsigned i=0;i<5000;i++){glass_chat_page_get(&page);if(!page.busy)break;usleep(1000);}
 assert(!page.busy&&!page.error&&page.page==1&&page.pages>=3&&page.text[0]=='x');
 char archive_path[256];snprintf(archive_path,sizeof(archive_path),"%s/%s",CAMERA_OBSERVATION_ROOT,name);
 assert(!unlink(archive_path)&&!symlink(first,archive_path));
 assert(!glass_chat_page_request(name,0));
 for(unsigned i=0;i<5000;i++){glass_chat_page_get(&page);if(!page.busy)break;usleep(1000);}
 assert(!page.busy&&page.error==-EPERM&&!page.text[0]);
 unsigned char jpeg[]={255,216,'x',255,217};
 for(unsigned i=0;i<3;i++){clip.frames[i].jpeg=jpeg;clip.frames[i].size=sizeof(jpeg);}
 assert(!glass_camera_recording_check());
 assert(!glass_camera_recording_save(10,"what is on the desk?",&clip,&cancel,first));
 assert(!glass_camera_recording_save(10,"another question",&clip,&cancel,second));
 assert(strcmp(first,second)&&count_recordings()==2);
 f=fopen(first,"r");assert(f);n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
 o=cJSON_Parse(text);assert(o&&!strcmp(cJSON_GetObjectItemCaseSensitive(o,"question")->valuestring,"what is on the desk?"));
 assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o,"frames"))==3);cJSON_Delete(o);
 char frame_path[128];snprintf(frame_path,sizeof(frame_path),"%.*s/frame-1.jpg",(int)(strrchr(first,'/')-first),first);
 f=fopen(frame_path,"rb");assert(f);unsigned char frame_bytes[8]={0};assert(fread(frame_bytes,1,sizeof(frame_bytes),f)==sizeof(jpeg));fclose(f);assert(!memcmp(frame_bytes,jpeg,sizeof(jpeg)));
 for(unsigned i=1;i<=4;i++){
   write_calls=0;fail_write_call=i;
   assert(glass_camera_recording_save(11,"q",&clip,&cancel,path)==-ENOSPC&&!path[0]&&count_recordings()==2);
 }
 fail_write_call=0;cancel_on_sync=true;
 assert(glass_camera_recording_save(11,"q",&clip,&cancel,path)==-ECANCELED&&!path[0]&&count_recordings()==2);
 cancel_on_sync=false;atomic_store(&cancel,false);
 snprintf(moved,sizeof(moved),"%s.saved",CAMERA_RECORDING_ROOT);
 assert(!rename(CAMERA_RECORDING_ROOT,moved)&&!symlink(moved,CAMERA_RECORDING_ROOT));
 assert(glass_camera_recording_check()==-EPERM);
 assert(!unlink(CAMERA_RECORDING_ROOT)&&!rename(moved,CAMERA_RECORDING_ROOT));
 puts("CAMERA_RECORDING_OK: three saved JPEGs and manifest, unique bundles, every write failure and cancellation removes only owned partials, symlink rejection");
 puts("CAMERA_OBSERVATION_OK: camera/voice SD receipts, unique files, timestamps, ENOSPC/cancel cleanup, full SD answer paging, no Flash fallback");return 0;
}
