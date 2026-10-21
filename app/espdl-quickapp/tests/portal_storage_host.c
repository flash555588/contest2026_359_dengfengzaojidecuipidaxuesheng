/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
/* Exercise the production filesystem implementation; intercept only the
 * transaction boundary to count writes and inject a failed medium. */
#define qpk_storage_write fixture_storage_write
#include "../overlay/apps/system/desktop/qpk_storage.c"
#undef qpk_storage_write
int qpk_storage_write(const char *,const char *,const char *,const char *,size_t);
#include "../overlay/apps/system/desktop/glass_portal_config.c"
static unsigned flash_writes;
static bool fail_write;
int qpk_storage_write(const char *root,const char *package,const char *key,const char *data,size_t length)
{
  assert(!strcmp(root,PORTAL_CONFIG_ROOT)&&!strcmp(package,"portal"));
  if(fail_write)return -ENOSPC;
  int ret=fixture_storage_write(root,package,key,data,length);
  if(!ret)flash_writes++;
  return ret;
}
static int patch(const char *text)
{cJSON *o=cJSON_Parse(text);assert(o);int ret=portal_config_save("ai",o);cJSON_Delete(o);return ret;}
static void check(const char *model)
{
  struct portal_ai_config config={0};assert(!portal_ai_load(&config));
  assert(!strcmp(config.model,model)&&!strcmp(config.api_key,"fixture-secret"));
}
int main(int argc,char **argv)
{
  if(argc==2){check(argv[1]);return 0;}
  assert(!patch("{\"model\":\"offline-model\",\"api_key\":\"fixture-secret\",\"base_url\":\"https://model.invalid/v1\"}"));
  assert(flash_writes==1);check("offline-model");
  assert(!patch("{}")&&!patch("{\"model\":\"offline-model\"}")&&flash_writes==1);
  /* A stale or absent SD configuration must never select a different model.
   * No SD volume API is linked into this test. */
  assert(!mkdir(PORTAL_TEST_SD_ROOT,0700));
  FILE *file=fopen(PORTAL_TEST_SD_ROOT "/ai0.json","w");assert(file);
  assert(fputs("{\"model\":\"stale-sd-model\"}",file)>=0&&!fclose(file));
  assert(!patch("{\"model\":\"latest-model\"}")&&flash_writes==2);check("latest-model");
  assert(!unlink(PORTAL_TEST_SD_ROOT "/ai0.json")&&!rmdir(PORTAL_TEST_SD_ROOT));
  check("latest-model");
  pid_t child=fork();assert(child>=0);
  if(!child){execl(argv[0],argv[0],"latest-model",(char *)NULL);_exit(127);}
  int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
  cJSON *redacted=portal_config_get("ai");char *wire=cJSON_PrintUnformatted(redacted);
  assert(wire&&!strstr(wire,"fixture-secret")&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(redacted,"api_key_set")));
  free(wire);cJSON_Delete(redacted);
  fail_write=true;assert(patch("{\"model\":\"must-fail\"}")==-ENOSPC);fail_write=false;
  check("latest-model");assert(flash_writes==2);
  assert(patch("{\"unknown\":1}")==-EINVAL&&patch("{\"model\":\"a\",\"model\":\"b\"}")==-EINVAL);
  assert(portal_config_get("../../data/private")==NULL&&portal_config_get(NULL)==NULL);
  assert(!patch("{}")&&flash_writes==2);
  puts("PORTAL_STORAGE_OK: Flash authoritative with/without SD; fresh-process persistence; identical saves skip writes; failed save retains old config; secrets and validation preserved");
  return 0;
}
