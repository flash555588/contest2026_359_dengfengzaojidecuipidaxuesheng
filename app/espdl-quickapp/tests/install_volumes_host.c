/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "glass_portal_files.c"
#include "glass_qpk_builder.c"
static void put(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static void install(const char *package,const char *volume)
{
  char json[256],error[128]={0},path[256];
  snprintf(json,sizeof(json),"{\"name\":\"Test\",\"package\":\"%s\",\"storage\":\"%s\"}",package,volume);
  cJSON *m=cJSON_Parse(json);assert(m);assert(!portal_install_begin(m,error,sizeof(error)));cJSON_Delete(m);
  assert(portal_install_target("../escape",path,sizeof(path))==-EINVAL);
  assert(!portal_install_target("app.js",path,sizeof(path)));put(path,"ui.text('ok');");
  assert(!portal_install_commit());
}
int main(void)
{
  assert(!mkdir(PORTAL_DATA_ROOT,0700));
  char sd_mount[256];strcpy(sd_mount,QPK_VOLUME_SD_ROOT);*strrchr(sd_mount,'/')=0;
  cJSON *m=cJSON_Parse("{\"name\":\"Missing\",\"package\":\"missing\",\"storage\":\"sdcard\"}");char error[128]={0};
  assert(portal_install_begin(m,error,sizeof(error))<0);cJSON_Delete(m);
  struct stat st;assert(lstat(QPK_VOLUME_INTERNAL_ROOT "/missing",&st)<0);
  assert(!mkdir(sd_mount,0700));
  install("first","internal");install("second","sdcard");
  assert(!stat(QPK_VOLUME_INTERNAL_ROOT "/first/app.js",&st));assert(!stat(QPK_VOLUME_SD_ROOT "/second/app.js",&st));
  m=cJSON_Parse("{\"name\":\"Dup\",\"package\":\"first\",\"storage\":\"sdcard\"}");assert(portal_install_begin(m,error,sizeof(error))==-EEXIST);cJSON_Delete(m);
  m=cJSON_Parse("{\"name\":\"Bad\",\"package\":\"evil\",\"storage\":\"../data\"}");assert(portal_install_begin(m,error,sizeof(error))==-EINVAL);cJSON_Delete(m);
  m=cJSON_Parse("{\"name\":\"Removed\",\"package\":\"removed\",\"storage\":\"sdcard\"}");assert(!portal_install_begin(m,error,sizeof(error)));cJSON_Delete(m);
  char retired[256];snprintf(retired,sizeof(retired),"%s-old",sd_mount);assert(!rename(sd_mount,retired));assert(portal_install_commit()<0);assert(!rename(retired,sd_mount));portal_install_abort();
  assert(glass_qpk_initialize()==0);
  char *draft=glass_qpk_write_draft("{\"name\":\"SD draft\",\"slug\":\"voltest\",\"source\":\"ui.text('hi');\",\"base_revision\":0}",NULL);assert(draft);cJSON *r=cJSON_Parse(draft);assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r,"ok")));free(draft);cJSON_Delete(r);
  struct qpk_draft_info current;glass_qpk_get(&current);assert(current.revision);
  glass_qpk_preview_result(current.revision,"");
  assert(!glass_qpk_save_to(current.revision,QPK_VOLUME_SD));assert(!stat(QPK_VOLUME_SD_ROOT "/ai.voltest/app.js",&st));
  assert(glass_qpk_save_to(current.revision,QPK_VOLUME_INTERNAL)==-EEXIST);
  assert(!qpk_storage_write(QPK_VOLUME_SD_ROOT "/.data","ai.voltest","state","SD data",7));
  assert(!qpk_storage_write(QPK_VOLUME_INTERNAL_ROOT "/.data","keep","state","keep",4));
  assert(!remove_installed("ai.voltest","ai.voltest",QPK_VOLUME_SD));
  assert(stat(QPK_VOLUME_SD_ROOT "/ai.voltest",&st)<0);
  char value[QPK_STORAGE_VALUE_MAX+1];size_t bytes;
  assert(qpk_storage_read(QPK_VOLUME_SD_ROOT "/.data","ai.voltest","state",value,sizeof(value),&bytes)==-ENOENT);
  assert(!qpk_storage_read(QPK_VOLUME_INTERNAL_ROOT "/.data","keep","state",value,sizeof(value),&bytes)&&bytes==4);
  assert(qpk_volume_from_filename(QPK_VOLUME_SD_ROOT "/second/app.js")==QPK_VOLUME_SD);
  assert(qpk_volume_from_filename(QPK_VOLUME_SD_ROOT "-other/app.js")==QPK_VOLUME_INTERNAL);
  assert(qpk_volume_from_filename("builtin:/camera/app.js")==QPK_VOLUME_INTERNAL);
  puts("INSTALL_VOLUMES_HOST_OK: internal/SD install and real QuickJS draft save, cross-volume duplicate rejection, missing/removed SD, invalid selection, traversal, per-volume data and uninstall isolation");
}
