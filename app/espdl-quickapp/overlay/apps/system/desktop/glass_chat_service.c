/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_chat.h"
#include "glass_ai_config.h"
#include "glass_chat_backend.h"
#include "glass_vision.h"
#include "qpk_espdl.h"
#include "glass_camera_observation.h"
#include <cJSON.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t chat_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t chat_wake = PTHREAD_COND_INITIALIZER;
/* Large histories belong in the runtime heap, after PSRAM initialization. */
static struct chat_snapshot *chat_state;
#define chat (*chat_state)
static bool started, send_pending, clear_pending, refresh_pending,worker_busy;
static atomic_bool cancelled;
static int clear_mode = -1;
static uint32_t next_id = 1;
static char *stream_text;
static size_t stream_size, stream_capacity;
static enum chat_activity current_activity;

const char *glass_chat_error_text(enum chat_error error)
{
  switch (error) {
    case CHAT_ERROR_CONFIG: return "请先扫码配置 AI 服务、模型和密钥。";
    case CHAT_ERROR_TIMEOUT: return "连接长时间没有收到数据，已保留收到的内容，可以重试。";
    case CHAT_ERROR_OUTPUT_LIMIT: return "模型的本次输出额度已用完，可以提高最大输出 Token 后重试。";
    case CHAT_ERROR_STREAM: return "连接在回复完成前断开，已保留收到的内容，可以重试。";
    case CHAT_ERROR_AUTH: return "密钥无效或没有访问权限，请检查 AI 配置。";
    case CHAT_ERROR_MODEL: return "接口或模型不可用，请检查服务地址和模型名称。";
    case CHAT_ERROR_LIMIT: return "服务请求过于频繁或额度不足，请稍后重试。";
    case CHAT_ERROR_TLS: return "安全连接失败，请检查设备时间和服务证书。";
    case CHAT_ERROR_RESPONSE: return "服务返回了无法读取的回复，请检查模型后重试。";
    case CHAT_ERROR_MEMORY: return "可用内存不足，关闭其他应用后再试。";
    case CHAT_ERROR_INTERRUPTED: return "上次回复因设备重启而中断，可以重新发送。";
    case CHAT_ERROR_CLEANUP: return "上一请求仍在结束中，请稍后重试。";
    case CHAT_ERROR_CAMERA: return "未能完成相机采集：请连接 USB 摄像头并关闭其他相机任务，再手动重试。";
    case CHAT_ERROR_CAMERA_STORAGE: return "录制未保存到 SD，已停止分析。请检查 SD 卡和剩余空间后手动重试。";
    default: return "暂时无法连接 AI 服务，请检查 Wi-Fi 后重试。";
  }
}

static void changed(void)
{ if (!++chat.revision) chat.revision = 1; }

static uint32_t now_ms(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000); }

/* Cards come from in-process board tools; every field is re-validated here so
 * a damaged event can never reach the dialog or the stored history. */
static bool chat_card_text(const char *text, size_t limit, bool empty)
{
  if (!text) return false;
  size_t used = 0;
  const unsigned char *p = (const unsigned char *)text;
  while (*p) {
    unsigned code = *p, extra = 0, minimum = 0;
    if (code < 128) {
      if (code < 32 || code == 127) return false;
      if (++used > limit) return false;
      p++;
      continue;
    }
    if (code >= 0xc2 && code <= 0xdf) { code &= 31; extra = 1; minimum = 0x80; }
    else if (code >= 0xe0 && code <= 0xef) { code &= 15; extra = 2; minimum = 0x800; }
    else if (code >= 0xf0 && code <= 0xf4) { code &= 7; extra = 3; minimum = 0x10000; }
    else return false;
    p++;
    for (unsigned i = 0; i < extra; i++) {
      if ((*p & 0xc0) != 0x80) return false;
      code = (code << 6) | (*p++ & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    used += extra + 1;
    if (used > limit) return false;
  }
  return empty || used > 0;
}

static bool chat_card_parse(const char *text, struct chat_card *out)
{
  if (!text || strlen(text) > CHAT_CARD_TEXT) return false;
  cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
  const cJSON *entity = cJSON_GetObjectItemCaseSensitive(root, "entity");
  const cJSON *kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
  const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
  const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
  const cJSON *level = cJSON_GetObjectItemCaseSensitive(root, "level");
  const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
  const cJSON *action=cJSON_GetObjectItemCaseSensitive(root,"action");
  const cJSON *verified=cJSON_GetObjectItemCaseSensitive(root,"verified");
  bool ok = cJSON_IsObject(root) &&
    (!entity || (cJSON_IsString(entity) && chat_card_text(entity->valuestring, sizeof(out->entity)-1, true))) &&
    cJSON_IsString(kind) && chat_card_text(kind->valuestring, sizeof(out->kind) - 1, false) &&
    cJSON_IsString(name) && chat_card_text(name->valuestring, sizeof(out->name) - 1, false) &&
    cJSON_IsString(state) && chat_card_text(state->valuestring, sizeof(out->state) - 1, true) &&
    cJSON_IsNumber(level) && level->valuedouble >= -1 && level->valuedouble <= 100 &&
    level->valuedouble == (int)level->valuedouble && cJSON_IsBool(on)&&
    (!action||cJSON_IsBool(action))&&(!verified||cJSON_IsBool(verified));
  if (ok) for (const char *p = kind->valuestring; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) { ok = false; break; }
  if (ok) {
    snprintf(out->entity, sizeof(out->entity), "%s", entity ? entity->valuestring : "");
    strcpy(out->kind, kind->valuestring); strcpy(out->name, name->valuestring);
    strcpy(out->state, state->valuestring);
    out->level = (int)level->valuedouble; out->on = cJSON_IsTrue(on);
    out->action=cJSON_IsTrue(action);out->verified=out->action&&cJSON_IsTrue(verified);
  }
  cJSON_Delete(root);
  return ok;
}

void glass_chat_stream(uint32_t id, const struct claw_stream_event *e)
{
  pthread_mutex_lock(&chat_lock);
  if (!chat_state || !chat.count || clear_pending || atomic_load(&cancelled)) goto done;
  struct chat_turn *t = &chat.turns[chat.count - 1];
  if (t->id != id || t->state != CHAT_WAITING) goto done;
  uint32_t now = now_ms();
  bool notify = e->kind != CLAW_STREAM_ACTIVITY || now - t->activity_ms >= 1000;
  t->activity_ms = now;
  if (e->kind == CLAW_STREAM_CONNECT) {
    current_activity = CHAT_ACTIVITY_CONNECT;
    t->stream_round++; strcpy(t->progress, "正在连接服务");
  } else if (e->kind == CLAW_STREAM_TEXT && e->text) {
    current_activity = CHAT_ACTIVITY_REPLY;
    size_t length = strlen(t->reply), size = strlen(e->text);
    t->text_bytes += size;
    if (size <= 2 * 1024 * 1024 - stream_size) {
      size_t need = stream_size + size + 1;
      if (need > stream_capacity) {
        size_t capacity = stream_capacity ? stream_capacity : 8192;
        while (capacity < need) capacity *= 2;
        if (capacity > 2 * 1024 * 1024 + 1) capacity = 2 * 1024 * 1024 + 1;
        char *grown = realloc(stream_text, capacity);
        if (grown) { stream_text = grown; stream_capacity = capacity; }
      }
      if (need <= stream_capacity) { memcpy(stream_text + stream_size, e->text, size + 1); stream_size += size; }
    }
    /* Keep the current output visible even when the retained excerpt is full. */
    if (length + size > CHAT_REPLY_BYTES) {
      size_t drop = length > CHAT_REPLY_BYTES / 2 ? length - CHAT_REPLY_BYTES / 2 : length;
      while (drop < length && ((unsigned char)t->reply[drop] & 0xc0) == 0x80) drop++;
      memmove(t->reply, t->reply + drop, length - drop + 1); length -= drop;
      t->clipped = true;
    }
    size_t copied = glass_chat_copy_utf8(t->reply + length, sizeof(t->reply) - length, e->text);
    if (copied < size) t->clipped = true;
    strcpy(t->progress, "正在接收回复");
  } else if (e->kind == CLAW_STREAM_REASONING && e->text) {
    t->reasoning_bytes += strlen(e->text);
    size_t n = strlen(t->reasoning), size = strlen(e->text);
    if (n + size >= sizeof(t->reasoning)) {
      size_t keep = n > 512 ? 512 : n;
      const char *start = t->reasoning + n - keep;
      while ((*start & 0xc0) == 0x80) start++;
      memmove(t->reasoning, start, strlen(start) + 1);
    }
    n = strlen(t->reasoning);
    glass_chat_copy_utf8(t->reasoning + n, sizeof(t->reasoning) - n, e->text);
    current_activity = CHAT_ACTIVITY_THINK;
    strcpy(t->progress, "正在思考");
  } else if (e->kind == CLAW_STREAM_TOOL) {
    current_activity = CHAT_ACTIVITY_TOOL;
    const char *name = e->text ? e->text : "";
    if(t->tool_count==CHAT_TOOL_TRACE){memmove(t->tools,t->tools+1,(CHAT_TOOL_TRACE-1)*sizeof(t->tools[0]));t->tool_count--;}
    snprintf(t->tools[t->tool_count++],sizeof(t->tools[0]),"%s",name);t->tool_total++;
    snprintf(t->progress, sizeof(t->progress), "%s", !strcmp(name, "qpk_write_draft") ? "正在编写应用草稿" :
      !strcmp(name, "qpk_read_example") ? "正在读取验证过的示例" : !strcmp(name, "qpk_read_draft") ? "正在检查当前草稿" : !strcmp(name,"home_control")||!strcmp(name,"home_service")?"正在控制设备":!strncmp(name,"home_",5)?"正在读取家居设备":!strncmp(name,"music_",6)?"正在处理音乐":!strncmp(name,"skill_",6)?"正在使用技能":!strncmp(name,"mcp_",4)?"正在调用 MCP 工具":"正在查看示例目录");
  } else if (e->kind == CLAW_STREAM_CARD && e->text) {
    /* One card per device per turn; newest cards of a long turn survive. */
    struct chat_card card;
    if (chat_card_parse(e->text, &card)) {
      unsigned slot = t->card_count;
      for (unsigned i = 0; i < t->card_count; i++)
        if (card.entity[0] ? !strcmp(t->cards[i].entity, card.entity) :
            (!t->cards[i].entity[0] && !strcmp(t->cards[i].kind, card.kind) && !strcmp(t->cards[i].name, card.name))) { slot = i; break; }
      if (slot == t->card_count) {
        if (t->card_count == CHAT_CARD_COUNT) {
          memmove(t->cards, t->cards + 1, (CHAT_CARD_COUNT - 1) * sizeof(t->cards[0]));
          slot = --t->card_count;
        }
        t->card_count++;
      }
      t->cards[slot] = card;
    }
  } else if (e->kind == CLAW_STREAM_USAGE) {
    if (e->has_output_tokens) { t->has_output_tokens = true; t->output_tokens = e->output_tokens; }
    if (e->has_reasoning_tokens) { t->has_reasoning_tokens = true; t->reasoning_tokens = e->reasoning_tokens; }
  } else if (e->kind == CLAW_STREAM_LIMIT) t->output_limited = true;
  if (notify) changed();
done:
  pthread_mutex_unlock(&chat_lock);
}

static void save_snapshot(struct chat_snapshot *snapshot)
{
  int ret = glass_chat_store_save(snapshot);
  pthread_mutex_lock(&chat_lock);
  if (chat.storage_error != ret) { chat.storage_error = ret; changed(); }
  pthread_mutex_unlock(&chat_lock);
}

static void refresh_settings(struct portal_ai_config *settings)
{
  memset(settings, 0, sizeof(*settings));
  int ret = glass_chat_load_settings(settings);
  pthread_mutex_lock(&chat_lock);
  bool configured = !ret;
  if (chat.configured != configured || strcmp(chat.model, settings->model)) {
    chat.configured = configured;
    snprintf(chat.model, sizeof(chat.model), "%s", settings->model);
    changed();
  }
  pthread_mutex_unlock(&chat_lock);
}

static void camera_progress(uint32_t id,const char *text,enum chat_activity activity)
{
  pthread_mutex_lock(&chat_lock);
  if(chat.count&&chat.turns[chat.count-1].id==id&&!atomic_load(&cancelled)){
    snprintf(chat.turns[chat.count-1].progress,sizeof(chat.turns[0].progress),"%s",text);
    current_activity=activity;changed();
  }
  pthread_mutex_unlock(&chat_lock);
}
static enum chat_error camera_reply(struct chat_turn *turn,char **reply)
{
  struct qpk_dl_result local;qpk_dl_status(&local);
  if(local.busy)return CHAT_ERROR_CAMERA;
  camera_progress(turn->id,"检查 SD · 准备录制",CHAT_ACTIVITY_TOOL);
  int ret=glass_camera_recording_check();
  if(ret){turn->camera_save_error=ret;return CHAT_ERROR_CAMERA_STORAGE;}
  struct qpk_camera_clip clip={0};
  camera_progress(turn->id,"正在录制 · 5 秒画面采样",CHAT_ACTIVITY_TOOL);
  uint32_t start=now_ms();ret=qpk_camera_clip_capture(&clip,&cancelled);
  printf("[bot-camera] turn=%lu capture_ms=%lu frames=%u result=%d camera_closed=1\n",
    (unsigned long)turn->id,(unsigned long)(now_ms()-start),clip.count,ret);
  if(ret){qpk_camera_clip_free(&clip);return CHAT_ERROR_CAMERA;}
  camera_progress(turn->id,"录制结束 · 正在保存 SD",CHAT_ACTIVITY_TOOL);
  start=now_ms();ret=glass_camera_recording_save(turn->id,turn->prompt,&clip,&cancelled,turn->camera_recording);
  printf("[bot-camera] turn=%lu save_ms=%lu recording_saved=%u result=%d\n",
    (unsigned long)turn->id,(unsigned long)(now_ms()-start),turn->camera_recording[0]!=0,ret);
  if(ret){turn->camera_save_error=ret;qpk_camera_clip_free(&clip);return CHAT_ERROR_CAMERA_STORAGE;}
  pthread_mutex_lock(&chat_lock);
  if(chat.count&&chat.turns[chat.count-1].id==turn->id)
    snprintf(chat.turns[chat.count-1].camera_recording,128,"%s",turn->camera_recording);
  pthread_mutex_unlock(&chat_lock);
  camera_progress(turn->id,"已保存 SD · 正在上传并思考",CHAT_ACTIVITY_THINK);
  start=now_ms();
  char *response=glass_vision_analyze_clip_take(&clip,turn->prompt,&cancelled);
  cJSON *root=response?cJSON_Parse(response):NULL;
  const cJSON *ok=cJSON_GetObjectItemCaseSensitive(root,"ok"),*answer=cJSON_GetObjectItemCaseSensitive(root,"answer");
  enum chat_error error=CHAT_ERROR_RESPONSE;
  if(!response)error=CHAT_ERROR_MEMORY;
  else if(cJSON_IsTrue(ok)&&cJSON_IsString(answer)&&glass_chat_utf8(answer->valuestring,4096,true)){
    camera_progress(turn->id,"思考完成 · 正在整理答案",CHAT_ACTIVITY_REPLY);
    *reply=strdup(answer->valuestring);error=*reply?CHAT_ERROR_NONE:CHAT_ERROR_MEMORY;
    if(turn->session_history&&*reply&&!atomic_load(&cancelled))turn->camera_save_error=glass_camera_observation_save(
      turn->id,turn->mode,turn->prompt,*reply,&clip,&cancelled,turn->camera_observation);
  }else{
    const cJSON *reason=cJSON_GetObjectItemCaseSensitive(root,"error");
    const char *code=cJSON_IsString(reason)?reason->valuestring:"";
    if(!strcmp(code,"vision_timeout"))error=CHAT_ERROR_TIMEOUT;
    else if(!strcmp(code,"vision_output_limit")||!strcmp(code,"vision_response_limit"))error=CHAT_ERROR_OUTPUT_LIMIT;
    else if(!strcmp(code,"vision_auth_failed"))error=CHAT_ERROR_AUTH;
    else if(!strcmp(code,"vision_model_not_configured"))error=CHAT_ERROR_CONFIG;
    else if(!strcmp(code,"vision_model_rejected_image"))error=CHAT_ERROR_MODEL;
    else if(!strcmp(code,"vision_transport_failed")||!strcmp(code,"vision_http_failed"))error=CHAT_ERROR_NETWORK;
  }
  /* Keep service diagnostics out of the answer and never log payloads/keys. */
  printf("[bot-camera] turn=%lu analysis_ms=%lu result=%d cancelled=%u\n",
    (unsigned long)turn->id,(unsigned long)(now_ms()-start),error,atomic_load(&cancelled));
  qpk_camera_clip_free(&clip);cJSON_Delete(root);free(response);return error;
}

char *glass_chat_observe_tool(const char *question,atomic_bool *cancel)
{
  if(!question||!glass_chat_utf8(question,512,true)||!*question)return strdup("{\"ok\":false,\"error\":\"invalid_question\"}");
  struct chat_turn *turn=calloc(1,sizeof(*turn));if(!turn)return NULL;
  pthread_mutex_lock(&chat_lock);
  struct chat_turn *current=chat_state&&chat.count?&chat.turns[chat.count-1]:NULL;
  bool allowed=cancel==&cancelled&&current&&current->mode==AGENT_MODE_ASSISTANT&&
    chat.phase==CHAT_REQUESTING&&!current->camera_tool_used&&!atomic_load(cancel);
  if(allowed){current->camera_tool_used=true;turn->id=current->id;turn->mode=current->mode;
    turn->session_history=true;snprintf(turn->prompt,sizeof(turn->prompt),"%s",question);}
  pthread_mutex_unlock(&chat_lock);
  if(!allowed){free(turn);return strdup("{\"ok\":false,\"error\":\"camera_not_available_or_already_attempted_no_retry\"}");}
  char *answer=NULL;enum chat_error error=camera_reply(turn,&answer);
  pthread_mutex_lock(&chat_lock);
  current=chat.count?&chat.turns[chat.count-1]:NULL;
  if(current&&current->id==turn->id){
    current->camera_save_error=turn->camera_save_error;
    snprintf(current->camera_recording,128,"%s",turn->camera_recording);
    snprintf(current->camera_observation,128,"%s",turn->camera_observation);
  }
  pthread_mutex_unlock(&chat_lock);
  cJSON *result=cJSON_CreateObject();char *wire=NULL;
  if(result){
    bool success=error==CHAT_ERROR_NONE&&answer&&!atomic_load(cancel);
    cJSON_AddBoolToObject(result,"ok",success);
    cJSON_AddStringToObject(result,"scope","cloud_temporal_samples");
    cJSON_AddStringToObject(result,"recording",turn->camera_recording);
    if(success)cJSON_AddStringToObject(result,"observation",answer);
    else cJSON_AddStringToObject(result,"error",atomic_load(cancel)?"cancelled":
      error==CHAT_ERROR_CAMERA_STORAGE?"sd_recording_failed_no_upload":"camera_observation_failed_no_retry");
    wire=cJSON_PrintUnformatted(result);cJSON_Delete(result);
  }
  free(answer);free(turn);return wire;
}

static void *chat_worker(void *unused)
{
  (void)unused;
  struct chat_snapshot *copy = calloc(1, sizeof(*copy));
  struct portal_ai_config *settings = calloc(1, sizeof(*settings));
  if (!copy || !settings) {
    free(copy); free(settings);
    pthread_mutex_lock(&chat_lock);
    started = worker_busy = false; chat.storage_error = -ENOMEM; changed();
    pthread_mutex_unlock(&chat_lock); return NULL;
  }
  int loaded = glass_chat_store_load(copy);
  pthread_mutex_lock(&chat_lock);
  memcpy(chat.turns, copy->turns, sizeof(chat.turns)); chat.count = copy->count;
  next_id = chat.count ? chat.turns[chat.count - 1].id + 1 : 1;
  if (!next_id) next_id = 1;
  chat.storage_error = loaded;
  pthread_mutex_unlock(&chat_lock);
  refresh_settings(settings);
  memset(settings, 0, sizeof(*settings));
  pthread_mutex_lock(&chat_lock);
  chat.loaded = true; chat.phase = CHAT_IDLE; worker_busy=false;changed();
  pthread_mutex_unlock(&chat_lock);

  for (;;) {
    pthread_mutex_lock(&chat_lock);
    while (!send_pending && !clear_pending && !refresh_pending) pthread_cond_wait(&chat_wake, &chat_lock);
    worker_busy=true;
    bool clear = clear_pending, send = send_pending;
    refresh_pending = false;
    if (clear) {
      clear_pending = send_pending = false;
      chat.phase = CHAT_CLEARING; changed();
      memcpy(copy,&chat,sizeof(*copy));
      unsigned kept=0;
      for(unsigned i=0;i<copy->count;i++)if(clear_mode>=0&&copy->turns[i].mode!=clear_mode)copy->turns[kept++]=copy->turns[i];
      copy->count=kept;memset(copy->turns+kept,0,(CHAT_MAX_TURNS-kept)*sizeof(copy->turns[0]));
      pthread_mutex_unlock(&chat_lock);
      int ret = glass_chat_store_save(copy);
      pthread_mutex_lock(&chat_lock);
      if (!ret) {
        memcpy(chat.turns,copy->turns,sizeof(chat.turns));chat.count=copy->count;
        /* Keep IDs monotonic while other modes retain their archives. */
      } else if (chat.count && chat.turns[chat.count - 1].state == CHAT_WAITING) {
        chat.turns[chat.count - 1].state = CHAT_CANCELLED;
      }
      chat.storage_error = ret; chat.phase = CHAT_IDLE; changed();
      pthread_mutex_unlock(&chat_lock);
      refresh_settings(settings); memset(settings, 0, sizeof(*settings));
      pthread_mutex_lock(&chat_lock);worker_busy=false;pthread_mutex_unlock(&chat_lock);continue;
    }
    if (!send) {
      pthread_mutex_unlock(&chat_lock); refresh_settings(settings); memset(settings, 0, sizeof(*settings));
      pthread_mutex_lock(&chat_lock);worker_busy=false;pthread_mutex_unlock(&chat_lock);continue;
    }
    send_pending = false;
    memcpy(copy, &chat, sizeof(*copy));
    chat.phase = atomic_load(&cancelled) ? CHAT_STOPPING : CHAT_REQUESTING; changed();
    pthread_mutex_unlock(&chat_lock);

    refresh_settings(settings);
    struct chat_turn *turn = &copy->turns[copy->count - 1];
    uint32_t request_began=now_ms();
    if(turn->session_history)printf("[bot-session] turn=%lu stage=begin camera=%u flash_history=0\n",(unsigned long)turn->id,turn->camera_samples);
    /* Bot requests never save Flash snapshots, including connection and
     * answer completion. Successful text receipts use the SD card. */
    if(!turn->session_history)save_snapshot(copy);
    char *context = turn->camera_samples?NULL:glass_chat_context(copy), *reply = NULL;
    enum chat_error error = CHAT_ERROR_NONE;
    if (!atomic_load(&cancelled)) {
      pthread_mutex_lock(&chat_lock); bool configured = chat.configured; pthread_mutex_unlock(&chat_lock);
      if (!configured) error = CHAT_ERROR_CONFIG;
      else if(turn->camera_samples)error=camera_reply(turn,&reply);
      else if (!context) error = CHAT_ERROR_MEMORY;
      else error = glass_chat_request(settings, context, turn->prompt, turn->id, turn->mode, &cancelled, &reply);
    }
    free(context);
    pthread_mutex_lock(&chat_lock);
    if(chat.count&&chat.turns[chat.count-1].id==turn->id&&chat.turns[chat.count-1].camera_tool_used){
      turn->camera_tool_used=true;
      snprintf(turn->camera_recording,128,"%s",chat.turns[chat.count-1].camera_recording);
    }
    pthread_mutex_unlock(&chat_lock);
    char archive[48] = ""; uint32_t archive_bytes = 0;
    const char *complete = error == CHAT_ERROR_NONE && reply && !atomic_load(&cancelled) ? reply : stream_text;
    int archive_error = 0;
    if (complete && strlen(complete) > CHAT_REPLY_BYTES)
      archive_error = turn->session_history?glass_chat_archive_session(complete,turn->id,archive,&archive_bytes):
        glass_chat_archive(complete, turn->id, archive, &archive_bytes);
    if(turn->session_history&&!turn->camera_samples&&error==CHAT_ERROR_NONE&&reply&&!atomic_load(&cancelled))
      turn->camera_save_error=glass_camera_observation_save(turn->id,turn->mode,turn->prompt,reply,NULL,&cancelled,turn->camera_observation);
    /* Do not retain the API key in the idle worker's configuration buffer. */
    memset(settings, 0, sizeof(*settings));
    pthread_mutex_lock(&chat_lock);
    bool reset = clear_pending;
    if (!reset) {
      struct chat_turn *current = &chat.turns[chat.count - 1];
      current->activity_ms=now_ms();
      if(current->session_history){
        current->camera_save_error=turn->camera_save_error;
        snprintf(current->camera_observation,sizeof(current->camera_observation),"%s",turn->camera_observation);
        snprintf(current->camera_recording,sizeof(current->camera_recording),"%s",turn->camera_recording);
      }
      snprintf(current->archive, sizeof(current->archive), "%s", archive); current->archive_bytes = archive_bytes;
      if (archive_error) chat.storage_error = archive_error;
      if (atomic_load(&cancelled)) {
        current->state = CHAT_CANCELLED; current->error = CHAT_ERROR_NONE;
      } else if (error == CHAT_ERROR_NONE && reply && reply[0] && glass_chat_utf8(reply, 2 * 1024 * 1024, true)) {
        current->state = CHAT_DONE; current->error = CHAT_ERROR_NONE;
        size_t bytes = glass_chat_copy_utf8(current->reply, sizeof(current->reply), reply);
        current->clipped = strlen(reply) > bytes;
      } else {
        current->state = CHAT_FAILED;
        current->error = error == CHAT_ERROR_NONE ? CHAT_ERROR_RESPONSE : error;
      }
      if(current->camera_samples)snprintf(current->progress,sizeof(current->progress),"%s",
        current->state==CHAT_DONE?(current->camera_save_error?"AI 分析完成 · 仅本次会话，SD 保存失败":"AI 分析完成 · 已保存 SD"):
        current->state==CHAT_CANCELLED?(current->camera_recording[0]?"已停止 · SD 录制已保留":"录制已停止"):
        current->error==CHAT_ERROR_CAMERA_STORAGE?"SD 保存失败 · 未上传分析":
        current->camera_recording[0]?"录制已存 SD · AI 分析未完成":"录制未完成");
      memcpy(copy, &chat, sizeof(*copy)); changed();
    }
    pthread_mutex_unlock(&chat_lock);
    free(reply);
    free(stream_text); stream_text = NULL; stream_size = stream_capacity = 0;
    if (!reset&&!turn->session_history) save_snapshot(copy);
    if(turn->session_history)printf("[bot-session] turn=%lu stage=end elapsed_ms=%lu result=%d cancelled=%u sd_saved=%u sd_error=%d\n",
      (unsigned long)turn->id,(unsigned long)(now_ms()-request_began),error,atomic_load(&cancelled),turn->camera_observation[0]!=0,turn->camera_save_error);
    pthread_mutex_lock(&chat_lock);
    if (!clear_pending) chat.phase = CHAT_IDLE;
    worker_busy=false;
    changed();
    pthread_mutex_unlock(&chat_lock);
  }
  return NULL;
}

static int chat_start(void)
{
  pthread_mutex_lock(&chat_lock);
  if (started) { pthread_mutex_unlock(&chat_lock); return 0; }
  if (!chat_state) {
    chat_state = calloc(1, sizeof(*chat_state));
    if (!chat_state) { pthread_mutex_unlock(&chat_lock); return -ENOMEM; }
    chat.revision = 1; chat.phase = CHAT_LOADING;
  }
  pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32768);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t thread; atomic_store(&cancelled, false);
  int ret = pthread_create(&thread, &attr, chat_worker, NULL);
  pthread_attr_destroy(&attr);
  if (!ret) started = worker_busy = true;
  pthread_mutex_unlock(&chat_lock); return -ret;
}

int glass_chat_start(void)
{
  if(!glass_ai_start_enter())return -EBUSY;
  int ret=chat_start();glass_ai_start_leave();return ret;
}
bool glass_chat_config_idle(void)
{
  pthread_mutex_lock(&chat_lock);
  bool idle=!started||(!worker_busy&&!send_pending&&!clear_pending&&!refresh_pending&&chat.phase==CHAT_IDLE);
  pthread_mutex_unlock(&chat_lock);return idle;
}
void glass_chat_config_reload(void)
{
  pthread_mutex_lock(&chat_lock);bool ready=started&&!worker_busy;pthread_mutex_unlock(&chat_lock);
  if(!ready)return;
  struct portal_ai_config *settings=calloc(1,sizeof(*settings));
  if(settings){refresh_settings(settings);memset(settings,0,sizeof(*settings));free(settings);}
}
static void chat_refresh_config(void)
{
  if (glass_chat_start()) return;
  pthread_mutex_lock(&chat_lock); refresh_pending = true;
  pthread_cond_signal(&chat_wake); pthread_mutex_unlock(&chat_lock);
}

void glass_chat_refresh_config(void)
{
  if(!glass_ai_start_enter())return;
  chat_refresh_config();glass_ai_start_leave();
}
static int queue_turn_ungated(const char *prompt, bool retry, enum glass_agent_mode mode,bool camera,bool session)
{
  if (!glass_agent_mode_valid(mode)) return -EINVAL;
  if(camera&&mode==AGENT_MODE_CREATE)return -EACCES;
  if (!retry && (!prompt || !glass_chat_utf8(prompt, CHAT_PROMPT_BYTES, true))) return -EINVAL;
  if (glass_chat_start()) return -ENOMEM;
  pthread_mutex_lock(&chat_lock); int ret = -EBUSY;
  if (!chat.loaded || chat.phase != CHAT_IDLE) goto done;
  if (!chat.configured) { ret = -ENOKEY; goto done; }
  if (retry) {
    unsigned index=chat.count;
    while(index&&chat.turns[index-1].mode!=mode)index--;
    if(!index||(chat.turns[index-1].state!=CHAT_FAILED&&chat.turns[index-1].state!=CHAT_CANCELLED)){
      ret=-EINVAL;goto done;
    }
    index--;
    if(chat.turns[index].camera_samples){ret=-EACCES;goto done;}
    session=chat.turns[index].session_history;
    /* Another mode may have completed since this failure. Move only this
     * mode's latest failed prompt to the queue tail; keep IDs ordered. */
    if(index!=chat.count-1){
      char *saved=strdup(chat.turns[index].prompt);
      if(!saved){ret=-ENOMEM;goto done;}
      memmove(chat.turns+index,chat.turns+index+1,(chat.count-index-1)*sizeof(chat.turns[0]));
      struct chat_turn *tail=&chat.turns[chat.count-1];memset(tail,0,sizeof(*tail));
      tail->mode=mode;strcpy(tail->prompt,saved);free(saved);
    }
  } else {
    while (*prompt == ' ' || *prompt == '\n' || *prompt == '\t') prompt++;
    size_t size = strlen(prompt);
    while (size && (prompt[size - 1] == ' ' || prompt[size - 1] == '\n' || prompt[size - 1] == '\t')) size--;
    if (!size) { ret = -EINVAL; goto done; }
    if (chat.count == CHAT_MAX_TURNS) {
      memmove(chat.turns, chat.turns + 1, (CHAT_MAX_TURNS - 1) * sizeof(chat.turns[0])); chat.count--;
    }
    struct chat_turn *t = &chat.turns[chat.count++]; memset(t, 0, sizeof(*t));
    t->mode=mode;memcpy(t->prompt, prompt, size); t->prompt[size] = 0;
  }
  struct chat_turn *t = &chat.turns[chat.count - 1];
  t->camera_samples=camera;
  t->session_history=session;
  t->id = next_id++;
  chat.active_mode = t->mode;
  t->reply[0] = 0; t->clipped = false; t->state = CHAT_WAITING; t->error = CHAT_ERROR_NONE;
  t->text_bytes = t->reasoning_bytes = t->output_tokens = t->reasoning_tokens = t->stream_round = 0;
  t->has_output_tokens = t->has_reasoning_tokens = t->output_limited = false;
  t->archive[0] = 0; t->archive_bytes = 0;
  t->camera_tool_used=false;t->camera_save_error=0;t->camera_observation[0]=0;t->camera_recording[0]=0;
  t->reasoning[0] = 0; strcpy(t->progress, "等待服务响应"); t->began_ms = t->activity_ms = now_ms();
  t->tool_count=t->tool_total=0;memset(t->tools,0,sizeof(t->tools));
  t->card_count=0;memset(t->cards,0,sizeof(t->cards));
  atomic_store(&cancelled, false); send_pending = true; chat.phase = CHAT_QUEUED;
  current_activity = CHAT_ACTIVITY_CONNECT;
  changed(); pthread_cond_signal(&chat_wake); ret = 0;
done:
  pthread_mutex_unlock(&chat_lock); return ret;
}

static int queue_turn(const char *prompt,bool retry,enum glass_agent_mode mode,bool camera,bool session)
{
  if(!glass_ai_start_enter())return -EBUSY;
  int ret=queue_turn_ungated(prompt,retry,mode,camera,session);glass_ai_start_leave();return ret;
}
int glass_chat_send_mode(const char *prompt,enum glass_agent_mode mode) {return queue_turn(prompt,false,mode,false,false);}
int glass_chat_send_bot(const char *prompt,enum glass_agent_mode mode) {
  return queue_turn(prompt,false,mode,false,true);
}
int glass_chat_send_bot_camera(const char *prompt,enum glass_agent_mode mode)
{
  if(mode!=AGENT_MODE_ASSISTANT)return glass_chat_send_camera(prompt,mode);
  if(!glass_chat_utf8(prompt,CHAT_PROMPT_BYTES,true))return -EINVAL;
  char question[CHAT_PROMPT_BYTES+1];
  int n=snprintf(question,sizeof(question),"请调用摄像头观察：%s",prompt);
  if(n<0||n>CHAT_PROMPT_BYTES)return -E2BIG;
  return glass_chat_send_bot(question,mode);
}
int glass_chat_send_camera(const char *prompt,enum glass_agent_mode mode) { return queue_turn(prompt,false,mode,true,true); }
int glass_chat_retry_mode(enum glass_agent_mode mode) { return queue_turn(NULL,true,mode,false,false); }
int glass_chat_send(const char *prompt) { return glass_chat_send_mode(prompt,AGENT_MODE_CREATE); }
int glass_chat_retry(void) { return glass_chat_retry_mode(AGENT_MODE_CREATE); }

void glass_chat_cancel(void)
{
  pthread_mutex_lock(&chat_lock);
  if (!chat_state) { pthread_mutex_unlock(&chat_lock); return; }
  if (chat.phase == CHAT_REQUESTING || chat.phase == CHAT_QUEUED) {
    atomic_store(&cancelled, true); chat.phase = CHAT_STOPPING; changed();
  }
  pthread_mutex_unlock(&chat_lock);
}

static int clear_turns_ungated(int mode)
{
  if (glass_chat_start()) return -ENOMEM;
  pthread_mutex_lock(&chat_lock);
  if (!chat.loaded || chat.phase == CHAT_CLEARING) { pthread_mutex_unlock(&chat_lock); return -EBUSY; }
  if(mode>=0&&chat.phase!=CHAT_IDLE&&chat.count&&chat.turns[chat.count-1].mode!=mode){pthread_mutex_unlock(&chat_lock);return -EBUSY;}
  clear_mode=mode;clear_pending = true; atomic_store(&cancelled, true); chat.phase = CHAT_CLEARING;
  changed(); pthread_cond_signal(&chat_wake); pthread_mutex_unlock(&chat_lock); return 0;
}

static int clear_turns(int mode)
{
  if(!glass_ai_start_enter())return -EBUSY;
  int ret=clear_turns_ungated(mode);glass_ai_start_leave();return ret;
}
int glass_chat_clear(void) { return clear_turns(-1); }
int glass_chat_clear_mode(enum glass_agent_mode mode)
{return glass_agent_mode_valid(mode)?clear_turns(mode):-EINVAL;}

bool glass_chat_get_mode(struct chat_snapshot *out,enum glass_agent_mode mode)
{
  if(!out||!glass_agent_mode_valid(mode))return false;
  if(out->view_mode!=mode)out->revision=0;
  if(!glass_chat_get(out))return false;
  unsigned n=0;for(unsigned i=0;i<out->count;i++)if(out->turns[i].mode==mode)out->turns[n++]=out->turns[i];
  out->count=n;out->view_mode=mode;memset(out->turns+n,0,(CHAT_MAX_TURNS-n)*sizeof(out->turns[0]));return true;
}

bool glass_chat_get(struct chat_snapshot *out)
{
  if (!out) return false;
  pthread_mutex_lock(&chat_lock);
  if (!chat_state) {
    bool update = out->revision != 1;
    if (update) { memset(out, 0, sizeof(*out)); out->revision = 1; out->phase = CHAT_LOADING; }
    pthread_mutex_unlock(&chat_lock); return update;
  }
  bool update = out->revision != chat.revision;
  if (update) {memcpy(out, &chat, sizeof(*out));out->view_mode=AGENT_MODE_COUNT;}
  pthread_mutex_unlock(&chat_lock); return update;
}

void glass_chat_activity_get(struct chat_activity_snapshot *out)
{
  if (!out) return;
  memset(out, 0, sizeof(*out)); out->phase = CHAT_IDLE; out->state = CHAT_DONE;
  pthread_mutex_lock(&chat_lock);
  if (chat_state) {
    out->phase = chat.phase; out->activity = current_activity;
    out->loaded = chat.loaded; out->configured = chat.configured;
    if (chat.count) {
      const struct chat_turn *t = &chat.turns[chat.count - 1];
      out->turn_id = t->id; out->state = t->state;
    }
  }
  pthread_mutex_unlock(&chat_lock);
}

bool glass_chat_result_get(struct chat_result_snapshot *out,uint32_t id,
                          enum glass_agent_mode mode,size_t offset)
{
  if(!out||!glass_agent_mode_valid(mode))return false;
  memset(out,0,sizeof(*out));bool found=false;
  pthread_mutex_lock(&chat_lock);
  if(chat_state)for(unsigned n=chat.count;n>0;n--){
    const struct chat_turn *t=&chat.turns[n-1];
    if(t->mode!=mode||(id&&t->id!=id))continue;
    out->revision=chat.revision;out->turn_id=t->id;out->phase=chat.phase;
    out->state=t->state;out->error=t->error;
    snprintf(out->progress,sizeof(out->progress),"%s",t->progress);
    out->card_count=t->card_count>CHAT_CARD_COUNT?CHAT_CARD_COUNT:t->card_count;
    memcpy(out->cards,t->cards,out->card_count*sizeof(out->cards[0]));
    size_t len=strlen(t->reply);
    if(offset<=len&&(!offset||((unsigned char)t->reply[offset]&0xc0)!=0x80)){
      size_t copied=glass_chat_copy_utf8(out->reply,sizeof(out->reply),t->reply+offset);
      out->next_offset=(uint32_t)(offset+copied);out->more=out->next_offset<len;
    }
    found=true;break;
  }
  pthread_mutex_unlock(&chat_lock);return found;
}
