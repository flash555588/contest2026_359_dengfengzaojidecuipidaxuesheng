/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "glass_grok.h"
#include "../overlay/apps/system/desktop/glass_chat_service.c"
size_t glass_chat_copy_utf8(char *out,size_t cap,const char *text){size_t n=strlen(text);if(n>=cap)n=cap-1;memcpy(out,text,n);out[n]=0;return n;}
static void expression(unsigned expected){struct chat_activity_snapshot out;glass_chat_activity_get(&out);assert(glass_grok_emotion(&out,NULL)==expected);}
int main(void){
  struct chat_activity_snapshot out;glass_chat_activity_get(&out);assert(!chat_state&&out.phase==CHAT_IDLE&&!out.loaded&&!out.turn_id);
  chat_state=calloc(1,sizeof(*chat_state));assert(chat_state);chat.phase=CHAT_REQUESTING;chat.loaded=chat.configured=true;chat.count=1;chat.turns[0].id=7;chat.turns[0].state=CHAT_WAITING;
  struct claw_stream_event e={.kind=CLAW_STREAM_CONNECT};glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_CONNECT&&out.turn_id==7);
  expression(36);
  e.kind=CLAW_STREAM_REASONING;e.text="thinking";glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_THINK);
  expression(30);
  e.kind=CLAW_STREAM_TEXT;e.text="hello";glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_REPLY);
  expression(39);
  e.kind=CLAW_STREAM_USAGE;glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_REPLY);
  expression(39);
  e.kind=CLAW_STREAM_TOOL;e.text="qpk_write_draft";glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_TOOL);
  expression(32);
  for(unsigned i=0;i<6;i++){char name[40];snprintf(name,sizeof(name),"qpk_test_%u",i);e.text=name;glass_chat_stream(7,&e);}
  assert(chat.turns[0].tool_count==CHAT_TOOL_TRACE&&chat.turns[0].tool_total==7);
  assert(!strcmp(chat.turns[0].tools[0],"qpk_test_2")&&!strcmp(chat.turns[0].tools[3],"qpk_test_5"));
  e.text="thinking";
  e.kind=CLAW_STREAM_REASONING;glass_chat_stream(99,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_TOOL);
  atomic_store(&cancelled,true);glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_TOOL);atomic_store(&cancelled,false);
  clear_pending=true;glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_TOOL);clear_pending=false;
  glass_chat_stream(7,&e);glass_chat_activity_get(&out);assert(out.activity==CHAT_ACTIVITY_THINK);
  expression(30);
  /* Smart home result cards: validated, deduplicated, capped, newest kept. */
  e.kind=CLAW_STREAM_CARD;e.text="{\"kind\":\"light\",\"name\":\"客厅主灯\",\"state\":\"已开启\",\"level\":80,\"on\":true}";
  glass_chat_stream(7,&e);
  assert(chat.turns[0].card_count==1&&!strcmp(chat.turns[0].cards[0].kind,"light")&&chat.turns[0].cards[0].level==80&&chat.turns[0].cards[0].on);
  e.text="{\"kind\":\"light\",\"name\":\"客厅主灯\",\"state\":\"已关闭\",\"level\":-1,\"on\":false}";
  glass_chat_stream(7,&e);
  assert(chat.turns[0].card_count==1&&!strcmp(chat.turns[0].cards[0].state,"已关闭")&&chat.turns[0].cards[0].level==-1);
  e.text="{\"kind\":\"switch\",\"name\":\"书房插座\",\"state\":\"已开启\",\"level\":-1,\"on\":true}";glass_chat_stream(7,&e);
  e.text="{\"kind\":\"sensor\",\"name\":\"客厅温度\",\"state\":\"23.5 °C\",\"level\":-1,\"on\":false}";glass_chat_stream(7,&e);
  e.text="{\"kind\":\"cover\",\"name\":\"阳台窗帘\",\"state\":\"已打开\",\"level\":60,\"on\":true}";glass_chat_stream(7,&e);
  assert(chat.turns[0].card_count==CHAT_CARD_COUNT);
  e.text="{\"kind\":\"scene\",\"name\":\"观影模式\",\"state\":\"已执行\",\"level\":-1,\"on\":true}";glass_chat_stream(7,&e);
  assert(chat.turns[0].card_count==CHAT_CARD_COUNT&&!strcmp(chat.turns[0].cards[0].name,"书房插座")&&!strcmp(chat.turns[0].cards[3].name,"观影模式"));
  char control[160];snprintf(control,sizeof(control),"{\"kind\":\"light\",\"name\":\"bad%c\",\"state\":\"y\",\"level\":-1,\"on\":true}",1);
  e.text=control;glass_chat_stream(7,&e);
  e.text="{\"kind\":\"li ght\",\"name\":\"x\",\"state\":\"y\",\"level\":-1,\"on\":true}";glass_chat_stream(7,&e);
  e.text="{\"kind\":\"light\",\"name\":\"x\",\"state\":\"y\",\"level\":101,\"on\":true}";glass_chat_stream(7,&e);
  e.text="{\"kind\":\"light\"}";glass_chat_stream(7,&e);
  e.text="[1]";glass_chat_stream(7,&e);
  assert(chat.turns[0].card_count==CHAT_CARD_COUNT&&!strcmp(chat.turns[0].cards[3].name,"观影模式"));
  chat.turns[0].card_count=0;
  e.text="{\"entity\":\"light.a\",\"kind\":\"light\",\"name\":\"同名灯\",\"state\":\"on\",\"level\":80,\"on\":true}";
  glass_chat_stream(7,&e);
  e.text="{\"entity\":\"light.b\",\"kind\":\"light\",\"name\":\"同名灯\",\"state\":\"off\",\"level\":-1,\"on\":false}";
  glass_chat_stream(7,&e);
  assert(chat.turns[0].card_count==2&&!strcmp(chat.turns[0].cards[1].entity,"light.b"));
  glass_chat_stream(7,&e);assert(chat.turns[0].card_count==2);
  chat.phase=CHAT_IDLE;chat.turns[0].state=CHAT_FAILED;glass_chat_activity_get(&out);assert(out.state==CHAT_FAILED&&out.phase==CHAT_IDLE);
  expression(34);chat.turns[0].state=CHAT_DONE;expression(33);chat.turns[0].state=CHAT_CANCELLED;expression(41);
  chat.phase=CHAT_STOPPING;expression(41);chat.phase=CHAT_CLEARING;expression(36);chat.phase=CHAT_QUEUED;expression(31);
  free(stream_text);free(chat_state);chat_state=NULL;expression(2);puts("CHAT_ACTIVITY_HOST_OK: real service stream events drive original expression IDs; passive getter; usage retention; stale/cancelled/clearing event rejection; terminal states");return 0;
}
