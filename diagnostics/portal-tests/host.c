#include "glass_portal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <stdatomic.h>
#include <time.h>

/* Faults and time shifts apply to the real production worker through linker
 * wrapping; the firmware has no test commands or authentication bypasses. */
static atomic_int accept_fault;
static atomic_long clock_offset;
int __real_accept(int fd,struct sockaddr *address,socklen_t *length);
int __wrap_accept(int fd,struct sockaddr *address,socklen_t *length)
{
  int error=atomic_exchange(&accept_fault,0);
  if(error) { errno=error; return -1; }
  return __real_accept(fd,address,length);
}
int __real_clock_gettime(clockid_t clock,struct timespec *ts);
int __wrap_clock_gettime(clockid_t clock,struct timespec *ts)
{
  int ret=__real_clock_gettime(clock,ts);
  if(!ret&&clock==CLOCK_MONOTONIC) ts->tv_sec+=atomic_load(&clock_offset);
  return ret;
}
int main(void) {
  mkdir("/tmp/portal-test-data",0700);
  assert(!portal_relative_path("../escape"));
  assert(!portal_relative_path("ok/../../escape"));
  assert(!portal_relative_path("/absolute"));
  assert(!portal_relative_path("folder/."));
  assert(!portal_relative_path("folder/.."));
  assert(!portal_relative_path("a\\b"));
  assert(!portal_relative_path("1/2/3/4/5/6/7/8/9/10/11"));
  assert(portal_relative_path("folder/.hidden"));
  assert(portal_relative_path("qpk/.data/@2pcom.openvela.recorder/kaudio/value"));
  assert(portal_relative_path("folder/歌曲.mp3"));
  char path[256]; assert(!portal_public_path("files",path,sizeof(path)));
  assert(!portal_public_path("data",path,sizeof(path)));
  assert(!strcmp(path,"/tmp/portal-test-data"));
  assert(!portal_public_path("data/qpk/.data",path,sizeof(path)));
  symlink("/tmp", "/tmp/portal-test-data/files/link");
  assert(portal_public_path("files/link/escape",path,sizeof(path))<0);
  cJSON *patch=cJSON_Parse("{\"api_key\":\"host-test-only\",\"model\":\"fixture\"}");
  assert(!portal_config_save("ai",patch)); cJSON_Delete(patch);
  patch=cJSON_Parse("{\"model\":\"updated\"}"); assert(!portal_config_save("ai",patch)); cJSON_Delete(patch);
  struct portal_ai_config ai; assert(!portal_ai_load(&ai)); assert(!strcmp(ai.api_key,"host-test-only"));
  cJSON *o=portal_config_get("ai"); assert(!cJSON_GetObjectItemCaseSensitive(o,"api_key")); cJSON_Delete(o);
  patch=cJSON_Parse("{\"model\":\"a\",\"model\":\"b\"}"); assert(portal_config_save("ai",patch)<0); cJSON_Delete(patch);
  patch=cJSON_Parse("{\"base_url\":\"http://bad\"}"); assert(portal_config_save("ai",patch)<0); cJSON_Delete(patch);
  assert(!portal_service_start()); char code[PORTAL_CODE_LENGTH+1]; assert(!portal_session_open(code,sizeof(code)));
  puts(code); fflush(stdout);
  char command[80];
  while(fgets(command,sizeof(command),stdin)) {
    if(!strcmp(command,"open\n")||!strcmp(command,"refresh\n")) {
      assert(!(command[0]=='o'?portal_session_open(code,sizeof(code)):portal_session_refresh(code,sizeof(code))));
      puts(code);
    } else if(!strcmp(command,"close\n")) { portal_session_close(); puts("OK"); }
    else if(!strcmp(command,"start\n")) { assert(!portal_service_start()); puts("OK"); }
    else if(!strcmp(command,"status\n")) portal_debug_status();
    else if(!strncmp(command,"fault ",6)) { atomic_store(&accept_fault,atoi(command+6)); puts("OK"); }
    else if(!strncmp(command,"advance ",8)) { atomic_fetch_add(&clock_offset,atoi(command+8)); puts("OK"); }
    else assert(0);
    fflush(stdout);
  }
  return 0;
}
