/* Included after extracted production ASR functions, no live audio/network. */
static void sentence(struct voice_ws *ws,int type,int index,const char *text)
{
  cJSON *root=cJSON_CreateObject(),*result=cJSON_AddObjectToObject(root,"result");assert(result);
  cJSON_AddNumberToObject(result,"slice_type",type);cJSON_AddNumberToObject(result,"index",index);
  cJSON_AddStringToObject(result,"voice_text_str",text);
  char *json=cJSON_PrintUnformatted(root);assert(json);ws_json(ws,json);free(json);cJSON_Delete(root);
}
int main(void)
{
  struct voice_ws ws={0};
  sentence(&ws,0,0,"");sentence(&ws,1,0,"请打开客厅灯");
  sentence(&ws,1,0,"请不要打开客厅灯，只查询状态。");
  sentence(&ws,2,0,"请不要打开客厅灯，只查询状态。");
  const char *first="请不要打开客厅灯，只查询状态。";
  assert(!strcmp(ws.final_text,first));sentence(&ws,2,0,first);assert(!strcmp(ws.final_text,first));
  sentence(&ws,1,1,"另外，卧室灯先不要动。");
  assert(transcript_error(&ws,false,0)==GLASS_VOICE_ERROR_INCOMPLETE);
  ws_json(&ws,"{\"final\":1}");assert(transcript_error(&ws,false,0)==GLASS_VOICE_ERROR_INCOMPLETE);
  sentence(&ws,2,1,"另外，卧室灯先不要动。");
  assert(transcript_error(&ws,false,0)==GLASS_VOICE_ERROR_NONE);
  assert(!strcmp(ws.final_text,"请不要打开客厅灯，只查询状态。另外，卧室灯先不要动。"));
  assert(transcript_error(&ws,true,0)==GLASS_VOICE_ERROR_TOO_LONG);
  ws=(struct voice_ws){0};sentence(&ws,2,0,"查询灯。");
  assert(transcript_error(&ws,false,0)==GLASS_VOICE_ERROR_INCOMPLETE); /* no final packet */
  sentence(&ws,2,2,"不要开灯。");assert(ws.protocol_error);
  ws=(struct voice_ws){0};sentence(&ws,2,0,"开灯。");sentence(&ws,2,0,"不要开灯。");assert(ws.protocol_error);
  ws=(struct voice_ws){0};
  char long_text[GLASS_VOICE_TEXT_BYTES+1]="";
  for(unsigned i=0;i<50;i++)strcat(long_text,"这是长句补充，");
  strcat(long_text,"最后只查询状态，不要操作。");assert(strlen(long_text)>768);
  sentence(&ws,2,0,long_text);ws_json(&ws,"{\"final\":1}");
  assert(!strcmp(ws.final_text,long_text)&&transcript_error(&ws,false,0)==GLASS_VOICE_ERROR_NONE);
  sentence(&ws,2,1,long_text);assert(ws.overflow&&!strcmp(ws.final_text,long_text));
  assert(transcript_error(&ws,false,0)==GLASS_VOICE_ERROR_TOO_LONG);
  char exact[GLASS_VOICE_TEXT_BYTES+1];memset(exact,'x',sizeof(exact)-1);exact[sizeof(exact)-1]=0;
  ws=(struct voice_ws){0};sentence(&ws,2,0,exact);ws_json(&ws,"{\"final\":1}");
  assert(!strcmp(ws.final_text,exact)&&transcript_error(&ws,false,0)==GLASS_VOICE_ERROR_NONE);
  sentence(&ws,2,1,"中");assert(ws.overflow&&!strcmp(ws.final_text,exact));
  ws=(struct voice_ws){0};ws_json(&ws,"{\"result\":{\"slice_type\":2,\"voice_text_str\":\"缺编号\"}}");assert(ws.protocol_error);
  lock_busy=1;assert(voice_audio_lock()==-ETIMEDOUT&&fake_ms==5000);
  fake_ms=tries=0;cancel_after=2;assert(voice_audio_lock()==-ECANCELED&&fake_ms<5000);
  atomic_store(&voice_stop,false);cancel_after=0;lock_busy=0;assert(!voice_audio_lock());
  puts("VOICE_TRANSCRIPT_OK: full long utterance, trailing negation, revised partials, stable deduplication, missing-tail rejection, exact UTF-8 capacity, overflow rejection; bounded cancellable audio lock");
}
