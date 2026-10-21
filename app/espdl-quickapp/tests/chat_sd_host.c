/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_chat.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *legacy="{\"version\":1,\"turns\":[{\"id\":1,\"state\":1,\"error\":0,\"prompt\":\"legacy\",\"reply\":\"read only\"}]}";
static void put(const char *path,const char *value)
{FILE *f=fopen(path,"w");assert(f);assert(fwrite(value,1,strlen(value),f)==strlen(value));assert(!fclose(f));}
static void unchanged(void)
{FILE *f=fopen(CHAT_DATA_ROOT "/chat/history.json","r");assert(f);char text[512]={0};assert(fread(text,1,sizeof(text),f)==strlen(legacy));fclose(f);assert(!strcmp(text,legacy));
 assert(access(CHAT_DATA_ROOT "/chat/history.tmp",F_OK)&&errno==ENOENT);}
int main(void)
{
 put(CHAT_DATA_ROOT "/chat/history.json",legacy);
 struct chat_snapshot *state=calloc(1,sizeof(*state));assert(state);
 assert(!glass_chat_store_load(state)&&state->count==1);
 state->turns[0].id=2;strcpy(state->turns[0].prompt,"SD new history");
 assert(glass_chat_store_save(state)<0);unchanged();
 char mount[128],away[160];snprintf(mount,sizeof(mount),"%s",CHAT_SD_HISTORY_ROOT);*strrchr(mount,'/')=0;
 assert(!mkdir(mount,0700));assert(!glass_chat_store_save(state));unchanged();
 memset(state,0,sizeof(*state));assert(!glass_chat_store_load(state)&&state->count==1&&state->turns[0].id==2);
 state->count=0;assert(!glass_chat_store_save(state));
 assert(!glass_chat_store_load(state)&&state->count==0); /* No legacy resurrection after clear. */
 char name[48];uint32_t bytes;assert(!glass_chat_archive("SD answer",3,name,&bytes)&&!strncmp(name,"sd-reply-",9)&&bytes==9);
 put(CHAT_SD_HISTORY_ROOT "/history.json","broken");put(CHAT_SD_HISTORY_ROOT "/history.tmp","broken");
 assert(glass_chat_store_load(state)<0&&!state->count);unchanged();
 snprintf(away,sizeof(away),"%s-away",CHAT_SD_HISTORY_ROOT);assert(!rename(CHAT_SD_HISTORY_ROOT,away));
 assert(!symlink(CHAT_DATA_ROOT "/chat",CHAT_SD_HISTORY_ROOT));
 assert(glass_chat_store_save(state)==-EPERM&&glass_chat_store_load(state)==-EPERM);unchanged();
 assert(!unlink(CHAT_SD_HISTORY_ROOT)&&!rename(away,CHAT_SD_HISTORY_ROOT));
 snprintf(away,sizeof(away),"%s-away",mount);assert(!rename(mount,away));
 assert(!glass_chat_store_load(state)&&state->count==1&&state->turns[0].id==1);
 assert(glass_chat_store_save(state)<0&&glass_chat_archive("must not spill",4,name,&bytes)<0);unchanged();
 free(state);puts("CHAT_SD_OK: SD-only history/answers, legacy read-only, no-SD session survives, clear/corruption do not resurrect Flash, SD faults never spill");return 0;
}
