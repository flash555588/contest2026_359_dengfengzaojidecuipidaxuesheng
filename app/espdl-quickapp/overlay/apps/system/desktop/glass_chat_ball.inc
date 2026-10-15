/* SPDX-License-Identifier: Apache-2.0 */
/* A half-duplex voice presentation of the existing ESPClaw conversation.
 * UI-thread state only. ASR, chat and TTS keep their existing bounded workers.
 * Opening never captures audio; the user's tap starts one visible session. */
/* One 120 x 48 draw object, 25 rounded bars; no per-frame LVGL objects,
 * image buffers, shadows or floating-point transforms. */
static void chat_ball_wave_draw(lv_event_t *event)
{
  lv_layer_t *layer=lv_event_get_layer(event);lv_area_t bounds;
  lv_obj_get_coords(lv_event_get_target(event),&bounds);
  lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
  d.radius=2;d.bg_color=lv_color_hex(g_chat_ui.primary);d.bg_opa=210;
  int cx=(bounds.x1+bounds.x2)/2,cy=(bounds.y1+bounds.y2)/2;
  if(g_chat_ui.voice_wave==BALL_WAVE_OFF){
    d.bg_opa=55;lv_area_t line={cx-12,cy-1,cx+12,cy+1};lv_draw_rect(layer,&d,&line);return;
  }
  for(unsigned i=0;i<25;i++){
    int height=g_chat_ui.voice_wave_heights[i],x=cx-49+(int)i*4;
    lv_area_t bar={x,cy-height/2,x+2,cy-height/2+height-1};lv_draw_rect(layer,&d,&bar);
  }
}
static void chat_ball_wave_render(unsigned phase,unsigned level)
{
  static const uint8_t profile[25]={18,44,70,36,28,62,86,100,91,66,44,59,42,20,35,51,30,68,43,57,48,62,38,49,24};
  uint8_t heights[25]={0};
  if(g_chat_ui.voice_wave!=BALL_WAVE_OFF){
    if(g_chat_ui.voice_wave==BALL_WAVE_THINK){unsigned triangle=phase<512?phase:1023-phase;level=28+triangle/10;}
    if(g_glass.reduced_motion){phase=0;level=96;}
    unsigned shift=phase*25/1024;
    for(unsigned i=0;i<25;i++)heights[i]=4+level*profile[(i+shift)%25]*40/(256*100);
  }
  if(memcmp(heights,g_chat_ui.voice_wave_heights,sizeof(heights))){
    memcpy(g_chat_ui.voice_wave_heights,heights,sizeof(heights));
    if(g_chat_ui.voice_waveform)lv_obj_invalidate(g_chat_ui.voice_waveform);
  }
}
static void chat_ball_wave_tick(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if(!g_chat_ui.root||!lv_obj_is_visible(g_chat_ui.root)||g_chat_ui.voice_wave==BALL_WAVE_OFF)return;
  unsigned level=0,period=2400;
  if(g_chat_ui.voice_wave==BALL_WAVE_LISTEN){
    struct glass_voice_snapshot v;glass_voice_get(&v);
    if(v.active&&v.phase==GLASS_VOICE_LISTENING)level=v.level*256/6000;
    period=1600;
  }
#ifdef GLASS_LOCAL_SPEECH
  else if(g_chat_ui.voice_wave==BALL_WAVE_SPEAK){
    struct speech_snapshot speech;glass_speech_get(&speech);
    if(g_chat_ui.speech_owned&&speech.active&&speech.phase==SPEECH_SPEAKING)level=speech.level*256/24000;
    period=1300;
  }
#endif
  if(level>256)level=256;
  g_chat_ui.voice_wave_level=(g_chat_ui.voice_wave_level*2+level)/3;
  unsigned phase=((lv_tick_get()-g_chat_ui.voice_wave_started)%period)*1024/period;
  chat_ball_wave_render(phase,g_chat_ui.voice_wave_level);
}
static void chat_ball_wave_set(unsigned emotion)
{
  enum chat_ball_wave mode=BALL_WAVE_OFF;
  if(g_chat_ui.voice_session){
    if(emotion==35)mode=BALL_WAVE_LISTEN;
    else if(g_chat_ui.voice_phase==BALL_SPEAK&&emotion==39)mode=BALL_WAVE_SPEAK;
    else if(g_chat_ui.voice_phase==BALL_REPLY||g_chat_ui.voice_phase==BALL_LISTEN)mode=BALL_WAVE_THINK;
  }
  if(mode==g_chat_ui.voice_wave&&g_glass.reduced_motion==g_chat_ui.voice_wave_reduced)return;
  g_chat_ui.voice_wave_reduced=g_glass.reduced_motion;
  g_chat_ui.voice_wave=mode;g_chat_ui.voice_wave_started=lv_tick_get();g_chat_ui.voice_wave_level=0;
  chat_ball_wave_render(0,0);
  if(g_chat_ui.voice_wave_timer){
    if(mode==BALL_WAVE_OFF||g_glass.reduced_motion)lv_timer_pause(g_chat_ui.voice_wave_timer);
    else lv_timer_resume(g_chat_ui.voice_wave_timer);
  }
}
static void chat_ball_hint(const char *text,unsigned emotion)
{
  chat_ui_label(g_chat_ui.voice_hint,text);
  glass_grok_select(g_chat_ui.ball,emotion);
  chat_ball_wave_set(emotion);
}

static void chat_ball_face_toggle(lv_event_t *event)
{
  LV_UNUSED(event);
  if(g_chat_ui.voice_face_request){
    qpk_dl_cancel_request(g_chat_ui.voice_face_request);g_chat_ui.voice_face_request=0;
    chat_ui_label(g_chat_ui.voice_face_label,"人脸跟随已关闭");return;
  }
  int ret=qpk_runtime_camera_busy()?-EBUSY:qpk_dl_face_start_default();
  if(ret>0){
    g_chat_ui.voice_face_request=(uint32_t)ret;g_chat_ui.voice_face_frame=0;
    g_chat_ui.voice_face_seen=lv_tick_get();chat_ui_label(g_chat_ui.voice_face_label,"摄像头开启 · 寻找人脸");
  }else chat_ui_label(g_chat_ui.voice_face_label,ret==-EBUSY?"摄像头正忙 · 点此重试":"未找到可用摄像头 · 点此重试");
}

static void chat_ball_face_poll(void)
{
  int x=0,y=0;uint32_t now=lv_tick_get();
  if(g_chat_ui.voice_face_request){
    struct qpk_dl_result result;qpk_dl_status(&result);
    if(result.request!=g_chat_ui.voice_face_request){
      g_chat_ui.voice_face_request=0;chat_ui_label(g_chat_ui.voice_face_label,"人脸跟随已结束 · 点此重试");
    }else if(result.error||!result.busy){
      qpk_dl_cancel_request(g_chat_ui.voice_face_request);g_chat_ui.voice_face_request=0;
      chat_ui_label(g_chat_ui.voice_face_label,"人脸跟随不可用 · 点此重试");
    }else{
      if(result.frame!=g_chat_ui.voice_face_frame){
        g_chat_ui.voice_face_frame=result.frame;g_chat_ui.voice_face_seen=now;
      }
      bool fresh=result.frame&&now-g_chat_ui.voice_face_seen<1000;
      if(fresh&&result.track.state==QPK_DL_TRACKING){
        x=result.track.offset_x;y=result.track.offset_y;
        if(x<-1000)x=-1000;if(x>1000)x=1000;if(y<-1000)y=-1000;if(y>1000)y=1000;
        chat_ui_label(g_chat_ui.voice_face_label,"摄像头开启 · 正在看着你");
      }else chat_ui_label(g_chat_ui.voice_face_label,"摄像头开启 · 寻找人脸");
    }
  }
  /* Existing geometric tracker selects one face; ease its target and recenter
   * when missing/stale. Do not move the body or replace the AI emotion. */
  g_chat_ui.voice_gaze_x=(g_chat_ui.voice_gaze_x+x)/2;
  g_chat_ui.voice_gaze_y=(g_chat_ui.voice_gaze_y+y)/2;
  glass_grok_gaze(g_chat_ui.ball,g_chat_ui.voice_gaze_x,g_chat_ui.voice_gaze_y);
}

static void chat_ball_stop(void)
{
  g_chat_ui.voice_session=false;
  if(g_chat_ui.voice_owned)glass_voice_stop();
#ifdef GLASS_LOCAL_SPEECH
  if(g_chat_ui.speech_owned)glass_speech_stop();
#endif
  struct chat_snapshot *s=g_chat_ui.snapshot;
  if(g_chat_ui.voice_turn&&g_chat_ui.voice_phase==BALL_REPLY&&s&&s->count&&
     s->turns[s->count-1].id==g_chat_ui.voice_turn)glass_chat_cancel();
  g_chat_ui.voice_owned=g_chat_ui.speech_owned=false;
  g_chat_ui.voice_turn=0;g_chat_ui.voice_phase=BALL_IDLE;
  chat_ball_wave_set(2);
}

static void chat_ball_error(const char *text)
{
  chat_ball_stop();chat_ball_hint(text,34);
}

static void chat_ball_listen(void)
{
  struct glass_voice_snapshot v;glass_voice_get(&v);
  if(v.active){chat_ball_error("麦克风正在使用，请稍后重试");return;}
  int ret=glass_voice_start();
  if(ret){glass_voice_get(&v);chat_ball_error(glass_voice_error_text(v.error));return;}
  g_chat_ui.voice_owned=true;g_chat_ui.voice_heard=false;
  g_chat_ui.voice_started=g_chat_ui.voice_last_sound=lv_tick_get();
  g_chat_ui.voice_revision=v.revision;g_chat_ui.voice_phase=BALL_LISTEN;
  chat_ui_label(g_chat_ui.voice_caption,"");
  chat_ball_hint("正在连接麦克风…",36);
}

static void chat_ball_end(lv_event_t *e)
{
  LV_UNUSED(e);chat_ball_stop();
  chat_ui_label(g_chat_ui.voice_caption,"");
  chat_ball_hint("对话已结束 · 轻点小球重新开始",2);
}

static void chat_ball_tap(lv_event_t *e)
{
  LV_UNUSED(e);
  if(!g_chat_ui.snapshot->configured){chat_ui_config(NULL);return;}
#ifndef GLASS_LOCAL_SPEECH
  chat_ball_error("此固件未启用语音播报");return;
#else
  if(!g_chat_ui.voice_session){
    struct speech_snapshot speech;glass_speech_get(&speech);
    if(speech.active){chat_ball_hint("语音正在使用，请先结束其他语音任务",34);return;}
    if(g_chat_ui.snapshot->phase!=CHAT_IDLE){chat_ball_hint("ESPClaw 正在回复，请稍候再开始",30);return;}
    g_chat_ui.voice_session=true;chat_ball_listen();return;
  }
  if(g_chat_ui.voice_phase==BALL_LISTEN){
    /* Tap again to finish this utterance early, not to discard it. */
    glass_voice_stop();chat_ball_hint("正在识别…",32);return;
  }
  if(g_chat_ui.voice_phase==BALL_REPLY){
    struct chat_snapshot *s=g_chat_ui.snapshot;glass_chat_get_mode(s,g_chat_ui.mode);
    if(s->count&&s->turns[s->count-1].id==g_chat_ui.voice_turn)glass_chat_cancel();
    g_chat_ui.voice_turn=0;
  }
  if(g_chat_ui.speech_owned)glass_speech_stop();
  g_chat_ui.voice_phase=BALL_DRAIN;g_chat_ui.voice_ready_at=lv_tick_get()+600;
  chat_ball_hint("正在打断…",41);
#endif
}

/* Short UTF-8 chunks bound synthesis memory/time and preserve the full reply
 * in the shared history. Never modify a reply to fit the speech API limit. */
static size_t chat_ball_chunk(char *out,size_t capacity,const char *text)
{
  size_t n=glass_chat_copy_utf8(out,capacity,text),cut=0;
  for(size_t i=0;i<n;i++){
    if(out[i]=='\n'||out[i]=='.'||out[i]=='!'||out[i]=='?'||out[i]==';')cut=i+1;
    if(i+2<n&&(unsigned char)out[i]==0xe3&&(unsigned char)out[i+1]==0x80&&
       ((unsigned char)out[i+2]==0x82||(unsigned char)out[i+2]==0x81))cut=i+3;
    if(i+2<n&&(unsigned char)out[i]==0xef&&(unsigned char)out[i+1]==0xbc&&
       ((unsigned char)out[i+2]==0x81||(unsigned char)out[i+2]==0x9f||(unsigned char)out[i+2]==0x9b))cut=i+3;
  }
  if(cut&&text[n]){n=cut;out[n]=0;}return n;
}

static void chat_ball_refresh(void)
{
  struct chat_snapshot *s=g_chat_ui.snapshot;glass_chat_get_mode(s,g_chat_ui.mode);
  chat_ball_face_poll();
  uint32_t now=lv_tick_get();
  if(!g_chat_ui.voice_session)return;
  struct glass_voice_snapshot v;glass_voice_get(&v);
#ifdef GLASS_LOCAL_SPEECH
  struct speech_snapshot speech;glass_speech_get(&speech);
  if(g_chat_ui.voice_phase==BALL_DRAIN){
    if(!v.active&&!speech.active&&s->phase==CHAT_IDLE&&(int32_t)(now-g_chat_ui.voice_ready_at)>=0){
      g_chat_ui.speech_owned=false;chat_ball_listen();
    }
    return;
  }
#endif
  if(g_chat_ui.voice_phase==BALL_LISTEN){
    if(v.active){
      if(v.phase==GLASS_VOICE_LISTENING){
        if(v.level>650||(v.text[0]&&v.revision!=g_chat_ui.voice_revision)){
          g_chat_ui.voice_heard=true;g_chat_ui.voice_last_sound=now;
        }
        g_chat_ui.voice_revision=v.revision;
        chat_ball_hint("我在听 · 说完会自动发送",35);
        if((g_chat_ui.voice_heard&&now-g_chat_ui.voice_last_sound>=1400&&now-g_chat_ui.voice_started>=1000)||
           now-g_chat_ui.voice_started>=12000){glass_voice_stop();chat_ball_hint("正在识别…",32);}
      }else chat_ball_hint(v.phase==GLASS_VOICE_FINISHING?"正在识别…":"正在连接麦克风…",v.phase==GLASS_VOICE_FINISHING?32:36);
      return;
    }
    g_chat_ui.voice_owned=false;
    if(v.phase!=GLASS_VOICE_DONE||!v.text[0]){
      chat_ball_error(v.phase==GLASS_VOICE_ERROR?glass_voice_error_text(v.error):"没有听清 · 轻点小球再试一次");return;
    }
    int ret=glass_chat_send_mode(v.text,g_chat_ui.mode);
    if(ret){chat_ball_error(ret==-EBUSY?"ESPClaw 正在回复，请稍候再试":"发送失败 · 轻点小球重试");return;}
    glass_chat_get_mode(s,g_chat_ui.mode);g_chat_ui.voice_turn=s->count?s->turns[s->count-1].id:0;
    g_chat_ui.voice_phase=BALL_REPLY;chat_ball_hint("让我想一想…",30);return;
  }
  const struct chat_turn *turn=s->count?&s->turns[s->count-1]:NULL;
  if(!turn||turn->id!=g_chat_ui.voice_turn){chat_ball_error("对话已更新 · 轻点小球继续");return;}
  if(g_chat_ui.voice_phase==BALL_REPLY){
    if(s->phase!=CHAT_IDLE){
      /* Read the real service event projection. Voice mode must not mask
       * connect / reasoning / tool / text events with one fixed thinking face. */
      struct chat_activity_snapshot activity;glass_chat_activity_get(&activity);
      unsigned emotion=activity.turn_id==g_chat_ui.voice_turn?glass_grok_emotion(&activity,NULL):30;
      const char *hint=emotion==39?"正在整理回答…":emotion==32?"正在处理你的请求…":
        emotion==36||emotion==31?"正在连接 AI…":emotion==41?"正在停止回复…":"让我想一想…";
      chat_ball_hint(hint,emotion);return;
    }
    if(turn->state!=CHAT_DONE||!turn->reply[0]){
      chat_ball_error(turn->state==CHAT_FAILED?glass_chat_error_text(turn->error):"回复已停止 · 轻点小球继续");return;
    }
    g_chat_ui.speech_cursor=0;g_chat_ui.voice_phase=BALL_SPEAK;
  }
#ifdef GLASS_LOCAL_SPEECH
  if(g_chat_ui.voice_phase==BALL_SPEAK){
    if(speech.active){chat_ball_hint("正在回答 · 轻点小球打断",39);return;}
    if(g_chat_ui.speech_owned&&speech.error){
      chat_ball_error(speech.error==-ENOENT||speech.error==-EBADMSG?"请安装 SD 卡语音模型 · 回复已存入 ESPClaw":
        speech.error==-ENOMEM?"语音内存不足 · 回复已存入 ESPClaw":"播报失败 · 回复已存入 ESPClaw");return;
    }
    g_chat_ui.speech_owned=false;
    if(!turn->reply[g_chat_ui.speech_cursor]){
      g_chat_ui.voice_phase=BALL_DRAIN;g_chat_ui.voice_ready_at=now+600;
      chat_ball_hint("我在这里…",2);return;
    }
    char chunk[241];size_t n=chat_ball_chunk(chunk,sizeof(chunk),turn->reply+g_chat_ui.speech_cursor);
    int ret=glass_speech_say(chunk);
    if(ret){chat_ball_error(ret==-EBUSY?"音频正在使用，请先暂停音乐":"语音未能启动 · 回复已存入 ESPClaw");return;}
    g_chat_ui.speech_owned=true;g_chat_ui.speech_cursor+=n;
    /* Only the current spoken phrase, never a second scrolling transcript. */
    chat_ui_label(g_chat_ui.voice_caption,chunk);chat_ball_hint("正在回答 · 轻点小球打断",39);
  }
#endif
}

static void chat_ball_create(lv_obj_t *card)
{
  chat_ui_button(card,"返回",24,20,80,40,false,glass_chat_back,NULL);
  chat_ui_button(card,glass_agent_mode_title(g_chat_ui.mode),120,20,170,40,false,chat_ui_modes,NULL);
  chat_ui_button(card,"ESPClaw",786,20,110,40,false,chat_ui_open_espclaw,NULL);
  chat_ui_button(card,"设置",910,20,88,40,false,chat_ui_config,NULL);
  lv_obj_t *face=chat_ui_button(card,"人脸跟随",356,20,312,40,false,chat_ball_face_toggle,NULL);
  g_chat_ui.voice_face_label=lv_obj_get_child(face,0);
  g_chat_ui.ball_mount=glass_box(card,332,56,360,360,g_chat_ui.background,0);
  lv_obj_remove_flag(g_chat_ui.ball_mount,LV_OBJ_FLAG_SCROLLABLE);
  g_chat_ui.ball=glass_grok_create_sprite(g_chat_ui.ball_mount,0,0,360,360,220,!g_desktop.light_theme,zh_font);
  glass_grok_set_reduced_motion(g_chat_ui.ball,g_glass.reduced_motion);
  if(!glass_grok_gaze(g_chat_ui.ball,0,0))show_toast("正视素材内存不足");
  lv_obj_t *touch=g_chat_ui.ball?glass_grok_root(g_chat_ui.ball):g_chat_ui.ball_mount;
  glass_clickable(touch,chat_ball_tap,NULL);
  g_chat_ui.voice_waveform=glass_box(card,452,366,120,48,g_chat_ui.background,0);
  lv_obj_set_style_bg_opa(g_chat_ui.voice_waveform,LV_OPA_TRANSP,0);
  lv_obj_remove_flag(g_chat_ui.voice_waveform,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_chat_ui.voice_waveform,chat_ball_wave_draw,LV_EVENT_DRAW_MAIN,NULL);
  g_chat_ui.voice_hint=chat_ui_text(card,"轻点小球，开始语音对话",162,430,700,20,g_chat_ui.primary,false);
  lv_obj_set_style_text_align(g_chat_ui.voice_hint,LV_TEXT_ALIGN_CENTER,0);
  g_chat_ui.voice_caption=chat_ui_text(card,"",212,466,600,16,g_chat_ui.secondary,false);
  lv_obj_set_style_text_align(g_chat_ui.voice_caption,LV_TEXT_ALIGN_CENTER,0);
  g_chat_ui.voice_end=chat_ui_button(card,"结束对话",452,510,120,40,false,chat_ball_end,NULL);
  g_chat_ui.voice_wave_timer=lv_timer_create(chat_ball_wave_tick,100,NULL);
  if(g_chat_ui.voice_wave_timer)lv_timer_pause(g_chat_ui.voice_wave_timer);
  glass_chat_refresh_config();
  chat_ball_face_toggle(NULL);
  g_chat_ui.timer=lv_timer_create(chat_ui_refresh,250,NULL);chat_ball_refresh();
}
