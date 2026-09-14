#include "claw_voice.h"
#include "claw_core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static atomic_int capture_calls, network_calls, chat_calls;
static int capture_error, network_error, chat_error, wait_stage;
static const char *transcription = "{\"text\":\"你好\"}";
static void stage_wait(int stage,atomic_bool *cancel) {
  while(wait_stage==stage && !atomic_load(cancel)) usleep(1000);
}
int claw_tls_initialize(const unsigned char *ca,size_t size)
{ assert(ca && size>1); return 0; }
int claw_voice_capture(unsigned int seconds,atomic_bool *cancel,atomic_bool *finish,unsigned char **wav,size_t *size)
{
  (void)finish; assert(seconds>=1 && seconds<=15); atomic_fetch_add(&capture_calls,1);
  stage_wait(1,cancel);
  if(atomic_load(cancel)) return -ECANCELED;
  if(capture_error) return capture_error;
  *size=32044; *wav=calloc(1,*size); claw_voice_wav_header(*wav,*size-44);
  claw_voice_progress(1000,2345); return 0;
}
int claw_webclient_post_binary(const char *url,const char *key,const char *content,const void *data,
                               size_t size,atomic_bool *cancel,char **response)
{
  assert(!strcmp(url,"https://test.invalid/v1/audio/transcriptions"));
  assert(!strcmp(key,"fixture-key")); assert(strstr(content,"multipart/form-data"));
  assert(data && size>32044); atomic_fetch_add(&network_calls,1); stage_wait(2,cancel);
  if(atomic_load(cancel))return -ECANCELED;
  if(network_error)return network_error;
  *response=strdup(transcription);return 0;
}
int claw_voice_chat(const char *text,const claw_core_config_t *config,atomic_bool *cancel,char *out,size_t size)
{
  assert(!strcmp(text,"你好"));assert(!strcmp(config->model,"fixture-chat"));assert(size==4097);
  atomic_fetch_add(&chat_calls,1);stage_wait(3,cancel);
  if(atomic_load(cancel))return -ECANCELED;
  if(chat_error)return chat_error;
  strcpy(out,"你好，有什么可以帮你？");return 0;
}
static struct claw_voice_snapshot read_state(void) {
  struct claw_voice_snapshot s;claw_voice_read(&s);return s;
}
static struct claw_voice_snapshot finish(void) {
  for(int i=0;i<3000;i++){struct claw_voice_snapshot s=read_state();if(!s.busy)return s;usleep(1000);}
  assert(!"worker timeout");return read_state();
}
static void config(void) {
  FILE *f=fopen("voice-test.json","w");assert(f);
  fputs("{\"transcription_url\":\"https://test.invalid/v1/audio/transcriptions\","
  "\"transcription_model\":\"fixture-transcriber\",\"transcription_api_key\":\"fixture-key\","
  "\"chat_base_url\":\"https://test.invalid/v1\",\"chat_model\":\"fixture-chat\","
  "\"chat_api_key\":\"fixture-key\",\"ca_file\":\"/tmp/espclaw-voice-tests/ca.pem\"}",f);fclose(f);
  f=fopen("ca.pem","w");assert(f);fputs("fixture-public-ca",f);fclose(f);
}
int main(void) {
  unlink("voice-test.json");
  assert(!claw_voice_start(1,true));struct claw_voice_snapshot s=finish();
  assert(s.state==CLAW_VOICE_ERROR && !atomic_load(&capture_calls) && !atomic_load(&network_calls));
  assert(!claw_voice_start(1,false));s=finish();assert(s.state==CLAW_VOICE_DONE && !atomic_load(&network_calls));
  config();
  for(int k=0;k<10;k++){assert(!claw_voice_start(1,true));s=finish();assert(s.state==CLAW_VOICE_DONE);
    assert(!strcmp(s.transcript,"你好") && !strcmp(s.answer,"你好，有什么可以帮你？"));}
  for(int stage=1;stage<=3;stage++) {
    wait_stage=stage;assert(!claw_voice_start(1,true));
    assert(claw_voice_start(1,false)==-EBUSY);
    for(int k=0;k<2000;k++){s=read_state();if(s.state==CLAW_VOICE_RECORDING+stage-1)break;usleep(1000);}
    claw_voice_cancel();s=finish();assert(s.state==CLAW_VOICE_CANCELLED && !*s.answer);wait_stage=0;
  }
  int before=atomic_load(&network_calls);
  capture_error=-EIO;assert(!claw_voice_start(1,true));s=finish();assert(s.state==CLAW_VOICE_ERROR);
  assert(atomic_load(&network_calls)==before);capture_error=0;
  before=atomic_load(&chat_calls);network_error=-ETIMEDOUT;
  assert(!claw_voice_start(1,true));s=finish();assert(s.state==CLAW_VOICE_ERROR);
  assert(atomic_load(&chat_calls)==before);network_error=0;
  transcription="{\"text\":\" \"}";assert(!claw_voice_start(1,true));s=finish();
  assert(s.state==CLAW_VOICE_ERROR && atomic_load(&chat_calls)==before);
  transcription="{\"text\":\"你好\"}";chat_error=-EIO;
  assert(!claw_voice_start(1,true));s=finish();assert(s.state==CLAW_VOICE_ERROR && !strcmp(s.transcript,"你好"));
  puts("PASS: voice lifecycle, repeated requests, missing config, failure gating, cancellation at all stages");
}
