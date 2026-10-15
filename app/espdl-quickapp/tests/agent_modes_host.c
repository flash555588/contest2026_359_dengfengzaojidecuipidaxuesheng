/* SPDX-License-Identifier: Apache-2.0 */
/* Real worker + store; deterministic backend, no network or LVGL. */
#include "glass_chat_backend.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static atomic_bool hold, entered, fail_next;
static enum glass_agent_mode request_mode;
static char request_context[CHAT_CONTEXT_BYTES*2];
int glass_chat_load_settings(struct portal_ai_config *out)
{memset(out,0,sizeof(*out));strcpy(out->model,"test");return 0;}
enum chat_error glass_chat_request(const struct portal_ai_config *settings,
  const char *context,const char *prompt,uint32_t id,enum glass_agent_mode mode,
  atomic_bool *cancel,char **reply)
{
  (void)settings;(void)id;request_mode=mode;
  snprintf(request_context,sizeof(request_context),"%s",context);
  atomic_store(&entered,true);
  while(atomic_load(&hold)&&!atomic_load(cancel))usleep(1000);
  if(atomic_exchange(&fail_next,false))return CHAT_ERROR_NETWORK;
  *reply=strdup(prompt);return CHAT_ERROR_NONE;
}
static struct chat_snapshot state;
static void idle(void)
{
  for(unsigned i=0;i<5000;i++){
    glass_chat_get(&state);if(state.loaded&&state.phase==CHAT_IDLE)return;usleep(1000);
  }
  assert(!"worker did not become idle");
}
static void send_mode(const char *text,enum glass_agent_mode mode)
{
  assert(glass_chat_send_mode(text,mode)==0);idle();
  assert(state.turns[state.count-1].mode==mode&&request_mode==mode);
}
int main(void)
{
  mkdir(CHAT_DATA_ROOT,0700);mkdir(CHAT_DATA_ROOT "/chat",0700);
  unlink(CHAT_DATA_ROOT "/chat/history.json");unlink(CHAT_DATA_ROOT "/chat/history.tmp");
  /* Previous firmware wrote no mode field: retain it under creation. */
  FILE *f=fopen(CHAT_DATA_ROOT "/chat/history.json","w");assert(f);
  fputs("{\"version\":1,\"turns\":[{\"id\":1,\"state\":1,\"error\":0,\"prompt\":\"legacy-app\",\"reply\":\"legacy-draft\"}]}",f);fclose(f);
  assert(glass_chat_start()==0);idle();assert(state.count==1&&state.turns[0].mode==AGENT_MODE_CREATE);
  send_mode("casual-talk",AGENT_MODE_CHAT);assert(!strstr(request_context,"legacy"));
  send_mode("assistant-query",AGENT_MODE_ASSISTANT);assert(!strstr(request_context,"casual")&&!strstr(request_context,"legacy"));
  send_mode("continue-chat",AGENT_MODE_CHAT);
  assert(strstr(request_context,"casual-talk")&&!strstr(request_context,"assistant-query")&&!strstr(request_context,"legacy"));
  send_mode("continue-app",AGENT_MODE_CREATE);
  assert(strstr(request_context,"legacy-draft")&&!strstr(request_context,"casual-talk")&&!strstr(request_context,"assistant-query"));
  struct chat_snapshot *loaded=calloc(1,sizeof(*loaded));assert(loaded);
  assert(!glass_chat_store_load(loaded)&&loaded->count==5);
  assert(loaded->turns[1].mode==AGENT_MODE_CHAT&&loaded->turns[2].mode==AGENT_MODE_ASSISTANT);free(loaded);
  struct chat_snapshot *filtered=calloc(1,sizeof(*filtered));assert(filtered);
  assert(glass_chat_get_mode(filtered,AGENT_MODE_CHAT)&&filtered->count==2);
  assert(!glass_chat_get_mode(filtered,AGENT_MODE_CHAT));
  assert(glass_chat_get_mode(filtered,AGENT_MODE_ASSISTANT)&&filtered->count==1);
  assert(glass_chat_get_mode(filtered,AGENT_MODE_CHAT)&&filtered->count==2);free(filtered);
  assert(!glass_chat_clear_mode(AGENT_MODE_CHAT));idle();assert(state.count==3);
  for(unsigned i=0;i<state.count;i++)assert(state.turns[i].mode!=AGENT_MODE_CHAT);
  atomic_store(&fail_next,true);send_mode("retry-assistant",AGENT_MODE_ASSISTANT);
  assert(state.turns[state.count-1].state==CHAT_FAILED);
  assert(glass_chat_retry_mode(AGENT_MODE_CHAT)==-EINVAL);
  assert(!glass_chat_retry_mode(AGENT_MODE_ASSISTANT));idle();
  assert(request_mode==AGENT_MODE_ASSISTANT&&state.turns[state.count-1].state==CHAT_DONE);
  atomic_store(&fail_next,true);send_mode("interleaved-retry",AGENT_MODE_CHAT);
  assert(state.turns[state.count-1].state==CHAT_FAILED);
  send_mode("other-mode-completed",AGENT_MODE_ASSISTANT);
  assert(!glass_chat_retry_mode(AGENT_MODE_CHAT));idle();
  assert(request_mode==AGENT_MODE_CHAT&&!strcmp(state.turns[state.count-1].prompt,"interleaved-retry"));
  assert(!strstr(request_context,"other-mode-completed"));
  for(unsigned i=1;i<state.count;i++)assert(state.turns[i].id>state.turns[i-1].id);
  atomic_store(&hold,true);atomic_store(&entered,false);
  assert(!glass_chat_send_mode("frozen-chat",AGENT_MODE_CHAT));
  for(unsigned i=0;i<5000&&!atomic_load(&entered);i++)usleep(1000);
  assert(atomic_load(&entered)&&request_mode==AGENT_MODE_CHAT);
  assert(glass_chat_send_mode("cannot-switch-request",AGENT_MODE_CREATE)==-EBUSY);
  assert(glass_chat_clear_mode(AGENT_MODE_ASSISTANT)==-EBUSY);
  assert(!glass_chat_clear_mode(AGENT_MODE_CHAT));atomic_store(&hold,false);idle();
  for(unsigned i=0;i<state.count;i++)assert(state.turns[i].mode!=AGENT_MODE_CHAT);
  assert(glass_chat_send_mode("invalid",AGENT_MODE_COUNT)==-EINVAL);
  assert(glass_chat_clear_mode(AGENT_MODE_COUNT)==-EINVAL);
  puts("AGENT_MODES_OK: migration, durable mode tags, context isolation, same-mode UI history, scoped clear, retry identity, immutable queued/request mode, cancellation");
  return 0;
}
