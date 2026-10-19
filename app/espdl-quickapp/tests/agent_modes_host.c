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
  char long_prompt[CHAT_PROMPT_BYTES+1]="";
  for(unsigned i=0;i<50;i++)strcat(long_prompt,"这是完整的长句，");
  strcat(long_prompt,"最后只查询床头灯状态，不要开灯，不要调亮度，也不要改颜色。");
  /* Test the real queue/store/backend path without altering fixture counts. */
  send_mode(long_prompt,AGENT_MODE_ASSISTANT);
  assert(!strcmp(state.turns[state.count-1].prompt,long_prompt));
  assert(!strcmp(state.turns[state.count-1].reply,long_prompt));
  assert(!glass_chat_clear_mode(AGENT_MODE_ASSISTANT));idle();
  send_mode("assistant-query",AGENT_MODE_ASSISTANT);
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
  /* Compact result views are scoped to one turn/mode and preserve trusted
   * receipt confirmation, UTF-8 boundaries and exact continuation offsets. */
  atomic_store(&hold,true);atomic_store(&entered,false);
  char chinese[700]="";for(unsigned i=0;i<35;i++)strcat(chinese,"测试结果。");
  assert(!glass_chat_send_mode(chinese,AGENT_MODE_ASSISTANT));
  while(!atomic_load(&entered))usleep(1000);
  struct chat_result_snapshot result;assert(glass_chat_result_get(&result,0,AGENT_MODE_ASSISTANT,0));
  uint32_t result_id=result.turn_id;assert(result.state==CHAT_WAITING);
  assert(!glass_chat_result_get(&result,result_id,AGENT_MODE_CHAT,0));
  struct claw_stream_event event={.kind=CLAW_STREAM_CARD,.text="{\"entity\":\"light.test\",\"kind\":\"light\",\"name\":\"主灯\",\"state\":\"已开启\",\"level\":30,\"on\":true,\"action\":true,\"verified\":true}"};
  glass_chat_stream(result_id,&event);
  assert(glass_chat_result_get(&result,result_id,AGENT_MODE_ASSISTANT,0)&&result.card_count==1&&result.cards[0].verified);
  event.text="{\"entity\":\"light.test\",\"kind\":\"light\",\"name\":\"主灯\",\"state\":\"结果未确认\",\"level\":-1,\"on\":false,\"action\":true,\"verified\":false}";
  glass_chat_stream(result_id,&event);atomic_store(&hold,false);idle();
  assert(glass_chat_result_get(&result,result_id,AGENT_MODE_ASSISTANT,0)&&!result.cards[0].verified&&result.cards[0].action);
  char joined[800]="";size_t offset=0;
  do{assert(glass_chat_result_get(&result,result_id,AGENT_MODE_ASSISTANT,offset));assert(glass_chat_utf8(result.reply,240,false));strcat(joined,result.reply);assert(result.next_offset>offset);offset=result.next_offset;}while(result.more);
  assert(!strcmp(joined,chinese));
  assert(!glass_chat_result_get(&result,result_id+999,AGENT_MODE_ASSISTANT,0));
  /* Result cards persist per turn and survive a reload; invalid card fields
   * reject the stored file instead of rendering damaged history. */
  struct chat_snapshot *withcards=calloc(1,sizeof(*withcards));assert(withcards);
  assert(!glass_chat_store_load(withcards)&&withcards->count==state.count);
  struct chat_turn *last=&withcards->turns[withcards->count-1];
  last->card_count=2;
  strcpy(last->cards[0].entity,"light.living_room");
  snprintf(last->cards[0].kind,sizeof(last->cards[0].kind),"light");
  snprintf(last->cards[0].name,sizeof(last->cards[0].name),"客厅主灯");
  snprintf(last->cards[0].state,sizeof(last->cards[0].state),"已开启");
  last->cards[0].level=80;last->cards[0].on=true;
  last->cards[0].action=last->cards[0].verified=true;
  snprintf(last->cards[1].kind,sizeof(last->cards[1].kind),"sensor");
  snprintf(last->cards[1].name,sizeof(last->cards[1].name),"客厅温度");
  snprintf(last->cards[1].state,sizeof(last->cards[1].state),"23.5 °C");
  last->cards[1].level=-1;last->cards[1].on=false;
  assert(!glass_chat_store_save(withcards));
  struct chat_snapshot *reloaded=calloc(1,sizeof(*reloaded));assert(reloaded);
  assert(!glass_chat_store_load(reloaded)&&reloaded->count==withcards->count);
  const struct chat_turn *saved=&reloaded->turns[reloaded->count-1];
  assert(!strcmp(saved->cards[0].entity,"light.living_room"));
  assert(saved->cards[0].action&&saved->cards[0].verified&&!saved->cards[1].action);
  assert(saved->card_count==2&&!strcmp(saved->cards[0].name,"客厅主灯")&&saved->cards[0].level==80&&saved->cards[0].on&&
         !strcmp(saved->cards[1].kind,"sensor")&&!strcmp(saved->cards[1].state,"23.5 °C")&&!saved->cards[1].on);
  free(withcards);free(reloaded);
  /* Both slots invalid: the load reports failure instead of guessing. */
  for(unsigned i=0;i<2;i++){
    f=fopen(i?CHAT_DATA_ROOT "/chat/history.tmp":CHAT_DATA_ROOT "/chat/history.json","w");assert(f);
    fputs("{\"version\":1,\"sequence\":9,\"turns\":[{\"id\":1,\"state\":1,\"error\":0,\"prompt\":\"p\",\"reply\":\"r\",\"cards\":[{\"kind\":\"light\",\"name\":\"x\",\"state\":\"y\",\"level\":-2,\"on\":true}]}]}",f);fclose(f);
  }
  struct chat_snapshot *broken=calloc(1,sizeof(*broken));assert(broken);
  assert(glass_chat_store_load(broken)!=0&&broken->count==0);free(broken);
  puts("AGENT_MODES_OK: migration, durable mode tags, context isolation, same-mode UI history, scoped clear, retry identity, immutable queued/request mode, cancellation, card persistence");
  return 0;
}
