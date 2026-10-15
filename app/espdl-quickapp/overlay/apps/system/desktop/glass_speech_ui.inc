/* SPDX-License-Identifier: Apache-2.0 */
#ifdef GLASS_LOCAL_SPEECH
static struct {lv_obj_t *status,*result;lv_timer_t *timer;uint32_t revision;} g_speech_ui;
static void speech_ui_update(lv_timer_t *timer)
{
  (void)timer;struct speech_snapshot s;glass_speech_get(&s);
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
  (void)e;glass_speech_stop();
  if(g_speech_ui.timer)lv_timer_delete(g_speech_ui.timer);
  memset(&g_speech_ui,0,sizeof(g_speech_ui));
}
static void speech_ui_action(lv_event_t *e)
{
  uintptr_t action=(uintptr_t)lv_event_get_user_data(e);int ret=0;
  if(action==0)ret=glass_speech_listen();
  else if(action==1)ret=glass_speech_say("你好，欢迎使用智能家居。离线语音已经准备好了。");
  else glass_speech_stop();
  if(ret)show_toast(ret==-EBUSY?"请先停止当前音频或语音任务":"本地语音启动失败");
  speech_ui_update(NULL);
}
static void glass_speech_open(lv_event_t *event)
{
  (void)event;lv_obj_t *card=panel_card("本地语音");if(!card)return;
  lv_obj_add_event_cb(card,speech_ui_delete,LV_EVENT_DELETE,NULL);
  g_speech_ui.revision=UINT32_MAX;
  uint32_t ink=theme_primary(),muted=theme_secondary();
  glass_text(card,"中文离线语音",28,74,800,28,ink);
  glass_text(card,"SanoTTS 中文播报 · ESP-SR 唤醒与固定指令",28,118,900,20,muted);
  g_speech_ui.status=glass_text(card,"未启动",28,175,880,24,ink);
  g_speech_ui.result=glass_text(card,"",28,224,880,20,muted);
  glass_button(card,"开始离线聆听",28,292,240,theme_card(),speech_ui_action,(void *)0);
  glass_button(card,"测试中文播报",288,292,240,theme_card(),speech_ui_action,(void *)1);
  glass_button(card,"停止",548,292,160,theme_card(),speech_ui_action,(void *)2);
  glass_text(card,"唤醒词：嗨，乐鑫",28,372,880,20,ink);
  glass_text(card,"支持：打开 / 关闭灯光、窗帘、风扇；返回桌面；停止聆听",28,413,900,16,muted);
  glass_text(card,"识别结果显示在此处；设备操作需先设置明确的绑定。",28,441,900,16,muted);
  glass_text(card,"模型从 SD 卡按需加载，退出本页即停止。",28,473,900,16,muted);
  g_speech_ui.timer=lv_timer_create(speech_ui_update,200,NULL);speech_ui_update(NULL);
}
#endif
