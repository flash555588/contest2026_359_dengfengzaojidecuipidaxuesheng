/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_board_music.h"
#include "glass_agent.h"
#include "glass_chat.h"
#include "glass_music.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Only metadata crosses the MCP boundary. URLs and credentials stay in music. */
static const char catalog[] =
"{\"name\":\"music_status\",\"description\":\"Read native playback state, title, artist, progress and volume. Renders a music card in chat; never opens a page.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
"{\"name\":\"music_search\",\"description\":\"Search for one song by title and artist. Does not start playback. No stream URL is returned.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"maxLength\":127}},\"required\":[\"query\"],\"additionalProperties\":false}},"
"{\"name\":\"music_play\",\"description\":\"Request playback by title and artist. Returns accepted, not proof of audible playback. Use music_status to observe playing. Playback continues after the chat request ends. Never opens the music app.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\",\"maxLength\":127}},\"required\":[\"query\"],\"additionalProperties\":false}},"
"{\"name\":\"music_pause\",\"description\":\"Request pause, idempotently. Does not toggle or start another song.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
"{\"name\":\"music_resume\",\"description\":\"Request resume of the current active track, idempotently. Does not restart a stopped song.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
"{\"name\":\"music_stop\",\"description\":\"Request stop of native music playback.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
"{\"name\":\"music_volume\",\"description\":\"Set native music volume to an integer percentage, 0 to 100. Does not change system-wide volume.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"level\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100}},\"required\":[\"level\"],\"additionalProperties\":false}},"
"{\"name\":\"music_skip\",\"description\":\"Request next or previous song in the native favorites playlist. No replay after timeout.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"direction\":{\"type\":\"string\",\"enum\":[\"next\",\"previous\"]}},\"required\":[\"direction\"],\"additionalProperties\":false}}";

static pthread_mutex_t bridge_lock=PTHREAD_MUTEX_INITIALIZER;
static char *finish(cJSON *root)
{char *text=root?cJSON_PrintUnformatted(root):NULL;cJSON_Delete(root);return text;}
static char *error(const char *code)
{cJSON *r=cJSON_CreateObject();if(!r)return NULL;cJSON_AddBoolToObject(r,"ok",false);cJSON_AddStringToObject(r,"error",code);return finish(r);}
static bool only(const cJSON *args,const char *key)
{
  if(!cJSON_IsObject(args))return false;
  if(!key)return cJSON_GetArraySize(args)==0;
  return cJSON_GetArraySize(args)==1&&args->child->string&&!strcmp(args->child->string,key);
}
static uint64_t now_ms(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static const char *state_name(enum music_state s)
{
  switch(s){case MUSIC_LOADING:return "loading";case MUSIC_PLAYING:return "playing";
    case MUSIC_PAUSED:return "paused";case MUSIC_STOPPED:return "stopped";
    case MUSIC_ENDED:return "ended";case MUSIC_ERROR:return "error";default:return "idle";}
}
static const char *state_label(enum music_state s)
{
  switch(s){case MUSIC_LOADING:return "正在加载";case MUSIC_PLAYING:return "播放中";
    case MUSIC_PAUSED:return "已暂停";case MUSIC_STOPPED:return "已停止";
    case MUSIC_ENDED:return "播放结束";case MUSIC_ERROR:return "播放出错";default:return "暂无播放";}
}
static void safe_text(char *out,size_t cap,const char *input)
{
  if(!glass_chat_utf8(input,128,false))input="";
  glass_chat_copy_utf8(out,cap,input);
}
static cJSON *card(const char *title,const char *artist,const char *label,unsigned position,unsigned duration,bool on)
{
  char name[64],who[40],state[64];safe_text(name,sizeof(name),title);safe_text(who,sizeof(who),artist);
  if(!name[0])snprintf(name,sizeof(name),"音乐");
  snprintf(state,sizeof(state),"%s%s%s",label,who[0]?" · ":"",who);
  cJSON *r=cJSON_CreateObject();if(!r)return NULL;
  int level=duration?(int)(((uint64_t)position*100)/duration):-1;if(level>100)level=100;
  cJSON_AddStringToObject(r,"entity","music.player");cJSON_AddStringToObject(r,"kind","music");
  cJSON_AddStringToObject(r,"name",name);cJSON_AddStringToObject(r,"state",state);
  cJSON_AddNumberToObject(r,"level",level);cJSON_AddBoolToObject(r,"on",on);return r;
}
static char *snapshot(const char *request)
{
  struct music_now_playing *s=calloc(1,sizeof(*s));if(!s)return NULL;
  glass_music_now_playing(s);
  cJSON *r=cJSON_CreateObject();if(!r){free(s);return NULL;}
  cJSON_AddBoolToObject(r,"ok",true);cJSON_AddStringToObject(r,"state",state_name(s->state));
  cJSON_AddStringToObject(r,"name",s->name);cJSON_AddStringToObject(r,"artist",s->artist);
  cJSON_AddNumberToObject(r,"position_ms",s->position_ms);cJSON_AddNumberToObject(r,"duration_ms",s->duration_ms);
  cJSON_AddNumberToObject(r,"volume",s->volume);cJSON_AddNumberToObject(r,"track_id",s->track_id);
  cJSON_AddBoolToObject(r,"pause_requested",s->pause_requested);
  if(request){cJSON_AddBoolToObject(r,"accepted",true);cJSON_AddBoolToObject(r,"verified",false);}
  cJSON_AddItemToObject(r,"card",card(s->name,s->artist,request?request:state_label(s->state),s->position_ms,s->duration_ms,s->state==MUSIC_PLAYING));
  free(s);return finish(r);
}
static bool query_arg(const cJSON *args,const char **query)
{
  const cJSON *q=cJSON_GetObjectItemCaseSensitive(args,"query");
  if(!only(args,"query")||!cJSON_IsString(q)||!q->valuestring[0]||!glass_chat_utf8(q->valuestring,127,false))return false;
  /* The public tool takes names, never a transport URL or filesystem path. */
  if(strstr(q->valuestring,"://")||q->valuestring[0]=='/')return false;
  if(strspn(q->valuestring," ")==strlen(q->valuestring))return false;
  *query=q->valuestring;return true;
}
static char *search(const char *query,atomic_bool *cancel)
{
  struct music_snapshot *s=calloc(1,sizeof(*s));if(!s)return NULL;
  glass_music_get(s);if(s->searching){free(s);return error("music_search_busy");}
  unsigned before=s->search_revision;
  int ret=glass_music_search(query);if(ret){free(s);return error("music_search_rejected");}
  uint64_t deadline=now_ms()+12000;
  for(;;){
    if(cancel&&atomic_load(cancel)){free(s);return error("cancelled_search_may_continue");}
    glass_music_get(s);
    if(!s->searching&&s->search_revision!=before)break;
    if(now_ms()>=deadline){free(s);return error("search_timeout_do_not_replay");}
    usleep(50000);
  }
  if(!s->has_result||s->search_error){free(s);return error("music_not_found_or_search_failed");}
  if(strcmp(s->result.query,query)){free(s);return error("music_search_replaced");}
  cJSON *r=cJSON_CreateObject();if(!r){free(s);return NULL;}
  cJSON_AddBoolToObject(r,"ok",true);cJSON_AddStringToObject(r,"name",s->result.name);
  cJSON_AddStringToObject(r,"artist",s->result.artist);cJSON_AddStringToObject(r,"query",query);
  cJSON *found=card(s->result.name,s->result.artist,"找到歌曲",0,0,false);
  if(found)cJSON_ReplaceItemInObjectCaseSensitive(found,"entity",cJSON_CreateString("music.search"));
  cJSON_AddItemToObject(r,"card",found);
  free(s);return finish(r);
}
static char *call_locked(const char *tool,const cJSON *args,atomic_bool *cancel)
{
  if(cancel&&atomic_load(cancel))return error("cancelled");
  if(!strcmp(tool,"music_status"))return only(args,NULL)?snapshot(NULL):error("invalid_arguments");
  if(!strcmp(tool,"music_play")||!strcmp(tool,"music_search")){
    const char *query;if(!query_arg(args,&query))return error("invalid_arguments");
    glass_music_start();
    if(!strcmp(tool,"music_search"))return search(query,cancel);
    struct music_song song={0};snprintf(song.name,sizeof(song.name),"%s",query);snprintf(song.query,sizeof(song.query),"%s",query);
    if(glass_music_play(&song))return error("music_play_rejected");
    return snapshot("已请求播放");
  }
  if(!strcmp(tool,"music_pause")||!strcmp(tool,"music_resume")){
    if(!only(args,NULL))return error("invalid_arguments");
    bool paused=!strcmp(tool,"music_pause");
    if(glass_music_set_paused(paused))return error("no_active_track");
    return snapshot(paused?"已请求暂停":"已请求继续");
  }
  if(!strcmp(tool,"music_stop")){
    if(!only(args,NULL))return error("invalid_arguments");
    glass_music_stop();return snapshot("已请求停止");
  }
  if(!strcmp(tool,"music_volume")){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(args,"level");
    if(!only(args,"level")||!cJSON_IsNumber(v)||!(v->valuedouble>=0&&v->valuedouble<=100)||v->valuedouble!=(int)v->valuedouble)return error("invalid_arguments");
    glass_music_volume((unsigned)v->valueint);return snapshot("音量已设置");
  }
  if(!strcmp(tool,"music_skip")){
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(args,"direction");
    if(!only(args,"direction")||!cJSON_IsString(v)||(strcmp(v->valuestring,"next")&&strcmp(v->valuestring,"previous")))return error("invalid_arguments");
    glass_music_start();
    if(glass_music_skip(!strcmp(v->valuestring,"next")?1:-1))return error("no_favorite_tracks");
    return snapshot("已请求切歌");
  }
  return error("unknown_music_tool");
}
static char *call(const char *tool,const cJSON *args,atomic_bool *cancel)
{
  if(pthread_mutex_trylock(&bridge_lock))return error("music_tool_busy");
  char *out=call_locked(tool,args,cancel);pthread_mutex_unlock(&bridge_lock);return out;
}
static const struct glass_board_home binding={catalog,call};
void glass_board_music_register(void){glass_agent_set_board_music(&binding);}
