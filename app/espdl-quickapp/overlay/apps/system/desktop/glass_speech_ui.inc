/* SPDX-License-Identifier: Apache-2.0 */
#ifdef GLASS_LOCAL_SPEECH
#include "glass_wake_model.h"
#include <time.h>
#include "glass_chat.h"
#include "glass_local_voice.h"
struct local_voice_device {char entity[96],name[80];};
static struct {
  lv_obj_t *root,*status,*result,*feedback,*binding[3],*picker,*rows[4],*page_label;
  lv_timer_t *timer;
  uint32_t revision,owner,last_event,client,configuration,request,deadline,verify_at,verify_deadline;
  int stage,role;bool on,sent,truncated,list_ok,accepting_commands;
  struct local_voice_device bound[3],*choices;
  unsigned choice_count,page;
  char entity[96];
} g_speech_ui;
static void speech_ui_update(lv_timer_t *timer);
#include "glass_local_voice_ui.inc"
static void speech_ui_stop(bool active)
{
  bool pending=g_speech_ui.stage==LOCAL_PRECHECK;
  bool sent=g_speech_ui.sent&&(g_speech_ui.stage==LOCAL_CONTROL||g_speech_ui.stage==LOCAL_VERIFY);
  /* Disarm before releasing either worker: retained command events must not
   * recreate a control request from the stop button's immediate refresh. */
  g_speech_ui.accepting_commands=false;
  glass_speech_stop_request(g_speech_ui.owner);
  local_voice_picker_close(NULL);local_voice_disconnect();
  local_voice_message(sent?"已停止接收指令；控制已发送，请查看设备状态":
    pending?"已停止接收指令，待处理控制已取消":active?"已停止接收新指令":"已停止聆听");
}
static void speech_ui_update(lv_timer_t *timer)
{
  (void)timer;if(!g_speech_ui.root)return;struct speech_snapshot s;glass_speech_get(&s);
  bool event=g_speech_ui.accepting_commands&&s.request==g_speech_ui.owner&&s.command_event&&s.command_event!=g_speech_ui.last_event;
  bool fresh=false;
  if(event){
    g_speech_ui.last_event=s.command_event;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    uint32_t ms=(uint32_t)((uint64_t)now.tv_sec*1000+now.tv_nsec/1000000);
    fresh=ms-s.command_ms<=2500;
    /* Stop and exit win over a completed precheck in this same UI tick. */
    if(fresh&&s.last_command==7){g_speech_ui.accepting_commands=false;panel_hide(NULL);return;}
    if(fresh&&s.last_command==8)speech_ui_stop(s.active);
  }
  local_voice_poll();
  if(event){
    if(!fresh)local_voice_message("指令已过期，请重新说一次");
    else if(s.last_command>=1&&s.last_command<=6)local_voice_execute(s.last_command);
  }
  if(!g_speech_ui.status||g_speech_ui.revision==s.revision)return;
  g_speech_ui.revision=s.revision;
  static const char *phases[]={"未启动","加载模型中","等待唤醒","正在识别指令","正在中文播报","正在停止","语音不可用"};
  const char *error=s.error==-EBADMSG||s.error==-ENOENT?"请安装匹配的 SD 语音资源":
    s.error==-ENOMEM?"可用内存不足":s.error==-EBUSY?"音频正在使用":s.error==-E2BIG?"文本过长，请分句播报":"请检查模型和音频设备";
  lv_label_set_text(g_speech_ui.status,s.error?error:phases[s.phase]);
  lv_label_set_text(g_speech_ui.result,s.text[0]?s.text:"等待语音事件");
}
static void speech_ui_delete(lv_event_t *e)
{
  (void)e;g_speech_ui.accepting_commands=false;glass_speech_stop_request(g_speech_ui.owner);
  local_voice_disconnect();free(g_speech_ui.choices);
  if(g_speech_ui.timer)lv_timer_delete(g_speech_ui.timer);
  memset(&g_speech_ui,0,sizeof(g_speech_ui));
}
static void speech_ui_action(lv_event_t *e)
{
  uintptr_t action=(uintptr_t)lv_event_get_user_data(e);int ret=0;
  if(action==0)ret=glass_speech_listen();
  else if(action==1)ret=glass_speech_say("你好，离线语音已准备好。");
  else {struct speech_snapshot s;glass_speech_get(&s);speech_ui_stop(s.active&&s.request==g_speech_ui.owner);}
  if(!ret&&action<2){struct speech_snapshot s;glass_speech_get(&s);g_speech_ui.owner=s.request;g_speech_ui.last_event=0;g_speech_ui.accepting_commands=action==0;}
  if(ret)show_toast(ret==-EBUSY?"请先停止当前音频或语音任务":"本地语音启动失败");
  speech_ui_update(NULL);
}
static void glass_speech_open(lv_event_t *event)
{
  (void)event;lv_obj_t *card=panel_card("本地语音");if(!card)return;
  g_speech_ui.root=card;
  lv_obj_add_event_cb(card,speech_ui_delete,LV_EVENT_DELETE,NULL);
  g_speech_ui.revision=UINT32_MAX;
  uint32_t ink=theme_primary(),muted=theme_secondary();
  glass_text(card,"端侧语音助手",28,74,800,28,ink);
  glass_text(card,"唤醒与指令识别在本机完成 · 设备通信使用 Home Assistant",28,116,900,16,muted);
  g_speech_ui.status=glass_text(card,"未启动",28,158,880,24,ink);
  g_speech_ui.result=glass_text(card,"",28,197,880,18,muted);
  glass_button(card,"开始离线聆听",28,239,240,theme_card(),speech_ui_action,(void *)0);
  glass_button(card,"测试中文播报",288,239,220,theme_card(),speech_ui_action,(void *)1);
  glass_button(card,"停止",528,239,140,theme_card(),speech_ui_action,(void *)2);
  g_speech_ui.feedback=glass_text(card,"先绑定设备，再开始聆听；返回桌面和停止聆听无需绑定。",28,292,900,16,muted);
  glass_text(card,"设备绑定 · 仅本次页面会话",28,326,900,20,ink);
  const char *roles[]={"灯光","窗帘","风扇"};
  for(unsigned i=0;i<3;i++){
    lv_obj_t *box=glass_box(card,28+i*306,350,288,86,theme_card(),16);
    glass_text(box,roles[i],14,8,252,16,muted);
    lv_obj_t *button=glass_button(box,"选择设备",12,34,264,theme_card(),local_voice_picker_open,(void *)(uintptr_t)i);
    g_speech_ui.binding[i]=lv_obj_get_child(button,0);
  }
  glass_text(card,"唤醒词：" GLASS_WAKE_PHRASE "（说完稍作停顿，再说指令）",28,450,900,16,ink);
  glass_text(card,"打开 / 关闭灯光、窗帘、风扇；返回桌面；停止聆听。退出即停止。",28,478,920,16,muted);
  g_speech_ui.timer=lv_timer_create(speech_ui_update,200,NULL);speech_ui_update(NULL);
}
#endif
