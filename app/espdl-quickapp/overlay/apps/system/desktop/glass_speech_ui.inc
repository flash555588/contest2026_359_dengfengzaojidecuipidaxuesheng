/* SPDX-License-Identifier: Apache-2.0 */
#ifdef GLASS_LOCAL_SPEECH
#include "glass_wake_model.h"
#include "glass_voice.h"
#include "glass_chat.h"
static struct {
  lv_obj_t *root,*status,*result,*feedback;
  lv_timer_t *timer;
  uint32_t owner,tts_owner,revision,turn;
  bool recording;
} g_speech_ui;
/* Background level updates do not require reallocating unchanged labels. */
static void speech_ui_label(lv_obj_t *label,const char *text)
{if(label&&strcmp(lv_label_get_text(label),text))lv_label_set_text(label,text);}
static bool speech_ui_command_pending(void){return false;}
static void speech_ui_update(lv_timer_t *timer)
{
  (void)timer;if(!g_speech_ui.root||(!g_speech_ui.recording&&!g_speech_ui.turn))return;
  struct glass_voice_snapshot v;glass_voice_get(&v);
  if(g_speech_ui.recording){
    if(v.request!=g_speech_ui.owner){g_speech_ui.recording=false;speech_ui_label(g_speech_ui.status,"麦克风已交给新的语音请求");}
    else{
      if(v.text[0])speech_ui_label(g_speech_ui.result,v.text);
      if(v.active)speech_ui_label(g_speech_ui.status,v.phase==GLASS_VOICE_FINISHING?"正在联网识别…":v.phase==GLASS_VOICE_CONNECTING?"正在启动麦克风…":"正在聆听 · 说完点结束输入");
      else{
        g_speech_ui.recording=false;
        if(v.phase==GLASS_VOICE_DONE&&v.text[0]){
          int ret=glass_chat_send_bot(v.text,AGENT_MODE_ASSISTANT);
          if(ret)speech_ui_label(g_speech_ui.status,"AI 暂忙，识别文本已保留");
          else{struct chat_result_snapshot r;if(glass_chat_result_get(&r,0,AGENT_MODE_ASSISTANT,0))g_speech_ui.turn=r.turn_id;speech_ui_label(g_speech_ui.status,"AI 正在联网处理…");}
        }else speech_ui_label(g_speech_ui.status,v.phase==GLASS_VOICE_ERROR?glass_voice_error_text(v.error):"输入已结束");
      }
    }
  }
  if(g_speech_ui.turn){
    struct chat_result_snapshot r;
    if(glass_chat_result_get(&r,g_speech_ui.turn,AGENT_MODE_ASSISTANT,0)){
      speech_ui_label(g_speech_ui.feedback,r.reply[0]?r.reply:r.progress);
      if(r.state!=CHAT_WAITING){speech_ui_label(g_speech_ui.status,r.state==CHAT_DONE?"处理完成 · 完整结果保留在助手对话":"处理失败，请查看助手对话");g_speech_ui.turn=0;}
    }else g_speech_ui.turn=0;
  }
}
static void speech_ui_delete(lv_event_t *e)
{
  (void)e;glass_voice_stop_request(g_speech_ui.owner);glass_speech_stop_request(g_speech_ui.tts_owner);
  if(g_speech_ui.timer)lv_timer_delete(g_speech_ui.timer);memset(&g_speech_ui,0,sizeof(g_speech_ui));
}
static void speech_ui_action(lv_event_t *e)
{
  uintptr_t action=(uintptr_t)lv_event_get_user_data(e);int ret=0;
  if(action==0){
    ret=glass_voice_start_request(&g_speech_ui.owner);
    if(!ret){g_speech_ui.recording=true;g_speech_ui.turn=0;speech_ui_label(g_speech_ui.result,"");speech_ui_label(g_speech_ui.feedback,"");}
  }else if(action==1)ret=glass_speech_say_request("你好，本地唤醒和中文播报已准备好。",&g_speech_ui.tts_owner);
  else glass_voice_stop_request(g_speech_ui.owner);
  if(ret)show_toast(action==0?"请检查联网语音识别配置":"播报暂不可用");
  speech_ui_update(NULL);
}
static void glass_speech_open(lv_event_t *event)
{
  (void)event;lv_obj_t *card=panel_card("语音服务");if(!card)return;g_speech_ui.root=card;
  lv_obj_add_event_cb(card,speech_ui_delete,LV_EVENT_DELETE,NULL);
  detail_page(card);detail_sidebar(card,"语音服务",LV_SYMBOL_AUDIO,panel_hide);
  uint32_t ink=theme_primary(),muted=theme_secondary();
  detail_text(card,"本地唤醒 · 联网识别与 AI",196,28,804,28,ink);
  detail_text(card,"唤醒词：" GLASS_WAKE_PHRASE " · 全局默认开启 · 中文播报保持本地",196,72,804,16,muted);
  lv_obj_t *input=detail_tile(card,196,116,804,180);
  g_speech_ui.status=detail_text(input,"等待唤醒，也可以直接开始语音输入",20,18,764,20,ink);
  g_speech_ui.result=detail_text(input,"",20,60,764,20,ink);lv_obj_set_height(g_speech_ui.result,96);
  lv_label_set_long_mode(g_speech_ui.result,LV_LABEL_LONG_WRAP);
  detail_button(card,"开始联网语音",196,316,252,speech_ui_action,(void *)0);
  detail_button(card,"结束输入",460,316,252,speech_ui_action,(void *)2);
  detail_button(card,"测试中文播报",724,316,252,speech_ui_action,(void *)1);
  lv_obj_t *reply=detail_tile(card,196,380,804,168);
  g_speech_ui.feedback=detail_text(reply,"识别后交给联网 AI 处理；新语音请求会接管麦克风。",20,18,764,18,muted);
  lv_obj_set_height(g_speech_ui.feedback,132);lv_label_set_long_mode(g_speech_ui.feedback,LV_LABEL_LONG_WRAP);
  g_speech_ui.timer=lv_timer_create(speech_ui_update,120,NULL);
}
#endif
