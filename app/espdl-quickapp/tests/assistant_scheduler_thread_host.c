/* SPDX-License-Identifier: Apache-2.0 */
/* Use the same storage/device boundaries with the production worker enabled. */
#define main manual_regression_main
#include "assistant_service_host.c"
#undef main
int main(void)
{
  glass_board_assistant_register();assert(!glass_assistant_start());
  unsigned id=add_schedule("thread-reminder",1,"notify","{\"text\":\"后台提醒\"}");
  bool completed=false;
  for(unsigned i=0;i<50;i++){
    cJSON *r=schedule("{\"operation\":\"list\"}",false);
    for(const cJSON *item=field(r,"items")->child;item;item=item->next)
      if(num(item,"id")==id&&!strcmp(str(item,"state"),"done"))completed=true;
    cJSON_Delete(r);if(completed)break;usleep(100000);
  }
  assert(completed&&effects==1);char text[256];assert(glass_assistant_take_notice(text,sizeof(text))&&!strcmp(text,"后台提醒"));
  sleep(2);assert(effects==1);assert(!glass_assistant_take_notice(text,sizeof(text)));
  puts("ASSISTANT_BACKGROUND_THREAD_OK: real independent worker executes once without chat, model or UI, delivers a notification and remains idle afterward");
  return 0;
}
