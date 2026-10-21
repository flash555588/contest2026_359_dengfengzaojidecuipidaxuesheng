/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_voice.h"
#include "glass_ai_config.h"
#include "glass_portal.h"
#include "glass_https.h"
#include "glass_music.h"
#include "glass_music_pcm.h"
#ifdef GLASS_LOCAL_SPEECH
#include "glass_speech.h"
#endif
#include "voice_root_ca.inc"
#include <nuttx/audio/audio.h>
#include <netutils/webclient.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define VOICE_HOST "asr.cloud.tencent.com"
#define VOICE_RATE 16000
#define VOICE_BUFFERS 4
/* Match offline capture's smaller DMA request. The 64 KiB kernel heap can
 * lose a contiguous 4 KiB block after TLS/chat activity despite free PSRAM. */
#define VOICE_BYTES 960
#define VOICE_FRAMES (VOICE_BYTES / 4)
#define VOICE_SETTLE_FRAMES 1024
#define VOICE_JSON_BYTES 4096
#define VOICE_PACKET_BYTES 6400
#define VOICE_RING_FRAMES (VOICE_RATE * 20u)

struct voice_ws {
  const struct webclient_tls_ops *ops;
  struct glass_https_request request;
  struct webclient_tls_connection *connection;
  char json[VOICE_JSON_BYTES + 1];
  size_t json_used;
  unsigned fragment_opcode;
  bool started;
  bool finished;
  bool failed;
  bool closed;
  bool overflow;
  bool protocol_error;
  bool pending_text;
  unsigned next_index;
  size_t last_start;
  int service_code;
  char final_text[sizeof(((struct glass_voice_snapshot *)0)->text)];
  char current_text[sizeof(((struct glass_voice_snapshot *)0)->text)];
};



static pthread_mutex_t voice_lock = PTHREAD_MUTEX_INITIALIZER;
static struct glass_voice_snapshot voice;
static atomic_bool voice_stop,voice_cancel;
/* A single dispatcher serializes teardown. Only the newest pending request is
 * retained; it never resets cancellation while the old worker owns audio. */
static bool voice_running,voice_pending_stop;
static uint32_t voice_worker_request;
static struct portal_voice_config *voice_pending;

static int random_bytes(void *out,size_t size)
{
  int fd=open("/dev/urandom",O_RDONLY);
  if(fd<0) return -errno;
  unsigned char *p=out;
  int ret=0;
  while(size) {
    ssize_t n=read(fd,p,size);
    if(n<0&&errno==EINTR) continue;
    if(n<=0) { ret=n<0?-errno:-EIO; break; }
    p+=n; size-=n;
  }
  close(fd);
  return ret;
}

static void voice_changed(void)
{ if(!++voice.revision) voice.revision=1; }

static void voice_publish(enum glass_voice_phase phase,const char *text,unsigned level)
{
  pthread_mutex_lock(&voice_lock);
  if(voice_worker_request!=voice.request){pthread_mutex_unlock(&voice_lock);return;}
  if(voice.phase!=GLASS_VOICE_FINISHING||phase!=GLASS_VOICE_LISTENING)voice.phase=phase;
  voice.level=level;
  if(text) snprintf(voice.text,sizeof(voice.text),"%s",text);
  voice_changed();
  pthread_mutex_unlock(&voice_lock);
}

static void voice_finish(enum glass_voice_error error,const char *text)
{
  pthread_mutex_lock(&voice_lock);
  if(voice_worker_request!=voice.request){pthread_mutex_unlock(&voice_lock);return;}
  voice.active=false;
  voice.wake_observing=false;
  voice.error=error;
  voice.phase=error==GLASS_VOICE_ERROR_NONE?GLASS_VOICE_DONE:GLASS_VOICE_ERROR;
  if(text) snprintf(voice.text,sizeof(voice.text),"%s",text);
  else if(error) voice.text[0]=0;
  voice_changed();
  pthread_mutex_unlock(&voice_lock);
}

const char *glass_voice_error_text(enum glass_voice_error error)
{
  switch(error) {
    case GLASS_VOICE_ERROR_CONFIG: return "请先在网页配置腾讯云语音识别";
    case GLASS_VOICE_ERROR_CLOCK: return "设备时间尚未同步";
    case GLASS_VOICE_ERROR_TLS: return "语音服务安全连接失败";
    case GLASS_VOICE_ERROR_AUTH: return "语音识别凭据无效或无权限";
    case GLASS_VOICE_ERROR_AUDIO: return "麦克风暂时不可用";
    case GLASS_VOICE_ERROR_EMPTY: return "没有识别到语音";
    case GLASS_VOICE_ERROR_MEMORY: return "可用内存不足";
    case GLASS_VOICE_ERROR_RESPONSE: return "语音服务返回异常";
    case GLASS_VOICE_ERROR_INCOMPLETE: return "语音未完整识别，未发送，请重说";
    case GLASS_VOICE_ERROR_TOO_LONG: return "语音内容过长，未发送，请分段重说";
    default: return "无法连接语音识别服务";
  }
}

void glass_voice_get(struct glass_voice_snapshot *out)
{
  if(!out) return;
  pthread_mutex_lock(&voice_lock);
  *out=voice;
  pthread_mutex_unlock(&voice_lock);
}

void glass_voice_stop_request(uint32_t request)
{
  pthread_mutex_lock(&voice_lock);
  if(request&&request==voice.request){
    if(voice_pending)voice_pending_stop=true;
    else atomic_store(&voice_stop,true);
    if(voice.active){voice.phase=GLASS_VOICE_FINISHING;voice_changed();}
  }
  pthread_mutex_unlock(&voice_lock);
}
void glass_voice_stop(void)
{struct glass_voice_snapshot s;glass_voice_get(&s);glass_voice_stop_request(s.request);}
void glass_voice_cancel(void)
{
  pthread_mutex_lock(&voice_lock);
  atomic_store(&voice_stop,true);atomic_store(&voice_cancel,true);
  if(voice_pending){memset(voice_pending,0,sizeof(*voice_pending));free(voice_pending);voice_pending=NULL;}
  if(!++voice.request)voice.request=1;
  voice.active=voice.wake_observing=false;voice.phase=GLASS_VOICE_IDLE;
  voice.error=GLASS_VOICE_ERROR_NONE;voice.text[0]=0;voice.level=0;voice_changed();
  pthread_mutex_unlock(&voice_lock);
}

static int url_encode(char *out,size_t capacity,const char *value)
{
  static const char hex[]="0123456789ABCDEF";
  size_t used=0;
  for(const unsigned char *p=(const unsigned char *)value;*p;p++) {
    unsigned char c=*p;
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
       c=='-'||c=='_'||c=='.'||c=='~') {
      if(used+1>=capacity) return -ENOSPC;
      out[used++]=c;
    } else {
      if(used+3>=capacity) return -ENOSPC;
      out[used++]='%'; out[used++]=hex[c>>4]; out[used++]=hex[c&15];
    }
  }
  out[used]=0;
  return 0;
}

static int hmac_sha1(const void *key,size_t key_size,const void *data,
                     size_t data_size,unsigned char digest[20])
{
  const mbedtls_md_info_t *md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if(!md) return -EIO;
  unsigned char block[64]={0},inner[20],outer[84];
  if(key_size>sizeof(block)) {
    if(mbedtls_md(md,key,key_size,block)) return -EIO;
    key_size=20;
  } else {
    memcpy(block,key,key_size);
  }
  unsigned char *input=malloc(sizeof(block)+data_size);
  if(!input) { memset(block,0,sizeof(block)); return -ENOMEM; }
  for(unsigned i=0;i<sizeof(block);i++) input[i]=block[i]^0x36;
  memcpy(input+sizeof(block),data,data_size);
  int ret=mbedtls_md(md,input,sizeof(block)+data_size,inner);
  memset(input,0,sizeof(block)+data_size);
  free(input);
  if(!ret) {
    for(unsigned i=0;i<sizeof(block);i++) outer[i]=block[i]^0x5c;
    memcpy(outer+sizeof(block),inner,sizeof(inner));
    ret=mbedtls_md(md,outer,sizeof(outer),digest);
  }
  memset(block,0,sizeof(block));
  memset(inner,0,sizeof(inner));
  memset(outer,0,sizeof(outer));
  return ret?-EIO:0;
}

static int voice_path(const struct portal_voice_config *config,
                      char *path,size_t capacity)
{
  time_t now=time(NULL);
  if(now<1700000000) return -ETIME;
  unsigned char nonce_data[12],digest[20];
  if(random_bytes(nonce_data,sizeof(nonce_data))) return -EIO;
  uint32_t nonce;
  memcpy(&nonce,nonce_data,sizeof(nonce));
  nonce=nonce%1000000000u+1;
  char voice_id[48],secret_id[385],query[720],source[896];
  snprintf(voice_id,sizeof(voice_id),"p4-%02x%02x%02x%02x%02x%02x%02x%02x",
    nonce_data[4],nonce_data[5],nonce_data[6],nonce_data[7],
    nonce_data[8],nonce_data[9],nonce_data[10],nonce_data[11]);
  int ret=url_encode(secret_id,sizeof(secret_id),config->secret_id);
  if(ret) return ret;
  int n=snprintf(query,sizeof(query),
    "engine_model_type=16k_zh&expired=%lld&filter_empty_result=0&needvad=0"
    "&nonce=%lu&secretid=%s&timestamp=%lld&voice_format=1&voice_id=%s",
    (long long)now+86400,(unsigned long)nonce,secret_id,(long long)now,voice_id);
  if(n<=0||(size_t)n>=sizeof(query)) return -ENOSPC;
  n=snprintf(source,sizeof(source),VOICE_HOST "/asr/v2/%s?%s",config->app_id,query);
  if(n<=0||(size_t)n>=sizeof(source)) return -ENOSPC;
  ret=hmac_sha1(config->secret_key,strlen(config->secret_key),
                source,strlen(source),digest);
  if(ret) return ret;
  unsigned char signature[64];
  size_t signature_size=0;
  if(mbedtls_base64_encode(signature,sizeof(signature)-1,&signature_size,
      digest,sizeof(digest))||signature_size>=sizeof(signature)) return -EIO;
  signature[signature_size]=0;
  char encoded[192];
  ret=url_encode(encoded,sizeof(encoded),(const char *)signature);
  memset(digest,0,sizeof(digest));
  memset(signature,0,sizeof(signature));
  if(ret) return ret;
  n=snprintf(path,capacity,"/asr/v2/%s?%s&signature=%s",
             config->app_id,query,encoded);
  memset(source,0,sizeof(source));
  return n>0&&(size_t)n<capacity?0:-ENOSPC;
}

static int ws_send_all(struct voice_ws *ws,const void *data,size_t size)
{
  const unsigned char *p=data;
  glass_https_set_deadline(ws->connection,glass_https_milliseconds()+3000);
  while(size) {
    ssize_t n=ws->ops->send(&ws->request,ws->connection,p,size);
    if(n<=0) return n<0?(int)n:-EIO;
    p+=n; size-=n;
  }
  return 0;
}

static int ws_recv_exact(struct voice_ws *ws,void *data,size_t size)
{
  unsigned char *p=data;
  glass_https_set_deadline(ws->connection,glass_https_milliseconds()+2000);
  while(size) {
    ssize_t n=ws->ops->recv(&ws->request,ws->connection,p,size);
    if(n<=0) return n<0?(int)n:-ECONNRESET;
    p+=n; size-=n;
  }
  return 0;
}

static int ws_send_frame(struct voice_ws *ws,unsigned opcode,
                         const void *data,size_t size)
{
  if(size>VOICE_PACKET_BYTES) return -EMSGSIZE;
  /* One TLS write per 200 ms packet, rather than two writes per DMA block. */
  unsigned char frame[VOICE_PACKET_BYTES+8],mask[4];size_t h=0;
  frame[h++]=0x80|(opcode&15);
  if(size<126)frame[h++]=0x80|(unsigned char)size;
  else{frame[h++]=0x80|126;frame[h++]=(unsigned char)(size>>8);frame[h++]=(unsigned char)size;}
  int ret=random_bytes(mask,sizeof(mask));if(ret)return ret;
  memcpy(frame+h,mask,sizeof(mask));h+=sizeof(mask);
  const unsigned char *source=data;
  for(size_t i=0;i<size;i++)frame[h+i]=source[i]^mask[i&3];
  ret=ws_send_all(ws,frame,h+size);memset(frame,0,h+size);return ret;
}

static bool header_value(const char *headers,const char *name,const char *expected)
{
  size_t length=strlen(name);
  const char *line=strstr(headers,"\r\n");
  while(line&&line[2]) {
    line+=2;
    const char *end=strstr(line,"\r\n");
    if(!end) break;
    if((size_t)(end-line)>length&&!strncasecmp(line,name,length)&&line[length]==':') {
      const char *value=line+length+1;
      while(value<end&&(*value==' '||*value=='\t')) value++;
      size_t size=(size_t)(end-value);
      return size==strlen(expected)&&!memcmp(value,expected,size);
    }
    line=end;
  }
  return false;
}

static int ws_handshake(struct voice_ws *ws,const char *path)
{
  unsigned char random[16],key[32],digest[20];
  size_t key_size=0,digest_size=0;
  if(random_bytes(random,sizeof(random))||
     mbedtls_base64_encode(key,sizeof(key)-1,&key_size,random,sizeof(random)))
    return -EIO;
  key[key_size]=0;
  char request[1536];
  int n=snprintf(request,sizeof(request),
    "GET %s HTTP/1.1\r\nHost: " VOICE_HOST "\r\nUpgrade: websocket\r\n"
    "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
    "Sec-WebSocket-Version: 13\r\nUser-Agent: ESP32-P4\r\n\r\n",path,key);
  if(n<=0||(size_t)n>=sizeof(request)) return -ENOSPC;
  int ret=ws_send_all(ws,request,(size_t)n);
  memset(request,0,sizeof(request));
  if(ret) return ret;
  char response[1536];
  size_t used=0;
  glass_https_set_deadline(ws->connection,glass_https_milliseconds()+8000);
  while(used+1<sizeof(response)) {
    ssize_t got=ws->ops->recv(&ws->request,ws->connection,response+used,1);
    if(got<=0) return got<0?(int)got:-ECONNRESET;
    used++;
    if(used>=4&&!memcmp(response+used-4,"\r\n\r\n",4)) break;
  }
  response[used]=0;
  if(used+1>=sizeof(response)||strncmp(response,"HTTP/1.1 101 ",13)) return -EPROTO;
  char accept_source[96],accept[64];
  n=snprintf(accept_source,sizeof(accept_source),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
  if(n<=0||(size_t)n>=sizeof(accept_source)) return -EIO;
  const mbedtls_md_info_t *sha1=mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if(!sha1||mbedtls_md(sha1,(const unsigned char *)accept_source,strlen(accept_source),digest)||
     mbedtls_base64_encode((unsigned char *)accept,sizeof(accept)-1,&digest_size,
                           digest,sizeof(digest))) return -EIO;
  accept[digest_size]=0;
  ret=header_value(response,"Sec-WebSocket-Accept",accept)?0:-EPROTO;
  memset(key,0,sizeof(key)); memset(digest,0,sizeof(digest));
  memset(response,0,sizeof(response));
  return ret;
}

static int ws_open(struct voice_ws *ws,const char *path)
{
  memset(ws,0,sizeof(*ws));
  ws->ops=glass_https_tls_ops();
  ws->request=(struct glass_https_request){
    .ca_pem=voice_root_ca,.ca_size=sizeof(voice_root_ca),
    .abort_flag=&voice_cancel,
    .deadline_ms=glass_https_milliseconds()+15000
  };
  int ret=ws->ops->connect(&ws->request,VOICE_HOST,"443",15,&ws->connection);
  if(ret) return ret;
  ret=ws_handshake(ws,path);
  if(ret) {
    ws->ops->close(&ws->request,ws->connection);
    ws->connection=NULL;
  }
  return ret;
}

static void ws_close(struct voice_ws *ws)
{
  if(ws->connection) {
    if(!ws->closed) (void)ws_send_frame(ws,8,NULL,0);
    ws->ops->close(&ws->request,ws->connection);
    ws->connection=NULL;
  }
  memset(ws->json,0,sizeof(ws->json));
}

static bool append_text(char *out,size_t capacity,const char *text)
{
  size_t used=strlen(out),add=strlen(text);
  if(used>=capacity||add>=capacity-used)return false;
  memcpy(out+used,text,add+1);return true;
}

static void ws_json(struct voice_ws *ws,const char *json)
{
  cJSON *root=cJSON_Parse(json);
  if(!root) { ws->failed=true; return; }
  cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code");
  if(cJSON_IsNumber(code)&&code->valueint!=0) {
    ws->service_code=code->valueint;
    ws->failed=true;
    cJSON_Delete(root);
    return;
  }
  cJSON *result=cJSON_GetObjectItemCaseSensitive(root,"result");
  if(cJSON_IsObject(result)) {
    cJSON *slice=cJSON_GetObjectItemCaseSensitive(result,"slice_type");
    cJSON *index=cJSON_GetObjectItemCaseSensitive(result,"index");
    cJSON *text=cJSON_GetObjectItemCaseSensitive(result,"voice_text_str");
    const char *piece=cJSON_IsString(text)?text->valuestring:"";
    if(!cJSON_IsNumber(slice)||slice->valuedouble<0||slice->valuedouble>2||
       slice->valuedouble!=slice->valueint||!cJSON_IsNumber(index)||
       index->valuedouble<0||index->valuedouble>100000||index->valuedouble!=index->valueint||
       !cJSON_IsString(text))ws->protocol_error=true;
    else if((unsigned)index->valueint>ws->next_index)ws->protocol_error=true;
    else if((unsigned)index->valueint<ws->next_index){
      /* Stable sentence repeats must not duplicate or rewrite the command. */
      if(slice->valueint==2&&(unsigned)index->valueint+1==ws->next_index&&
         strcmp(ws->final_text+ws->last_start,piece))ws->protocol_error=true;
    }else if(slice->valueint==2){
      ws->last_start=strlen(ws->final_text);
      if(!append_text(ws->final_text,sizeof(ws->final_text),piece))ws->overflow=true;
      else {ws->next_index++;ws->pending_text=false;
        strcpy(ws->current_text,ws->final_text);}
    }else{
      strcpy(ws->current_text,ws->final_text);
      if(!append_text(ws->current_text,sizeof(ws->current_text),piece))ws->overflow=true;
      ws->pending_text=ws->pending_text||piece[0]!=0;
    }
    if(ws->current_text[0]&&!ws->overflow&&!ws->protocol_error)
      voice_publish(GLASS_VOICE_LISTENING,ws->current_text,0);
  } else {
    ws->started=true;
  }
  cJSON *final=cJSON_GetObjectItemCaseSensitive(root,"final");
  if(cJSON_IsNumber(final)&&final->valueint==1) ws->finished=true;
  cJSON_Delete(root);
}

static enum glass_voice_error transcript_error(const struct voice_ws *ws,bool duration_limit,int ret)
{
  if(ws->overflow||duration_limit)return GLASS_VOICE_ERROR_TOO_LONG;
  if(ws->protocol_error)return GLASS_VOICE_ERROR_INCOMPLETE;
  if(ws->failed)return GLASS_VOICE_ERROR_AUTH;
  if(ret<0)return GLASS_VOICE_ERROR_NETWORK;
  if(!ws->finished||ws->pending_text)return GLASS_VOICE_ERROR_INCOMPLETE;
  return ws->final_text[0]?GLASS_VOICE_ERROR_NONE:GLASS_VOICE_ERROR_EMPTY;
}

/* Returns one when a frame was consumed, zero when no frame is ready. */
static int ws_receive(struct voice_ws *ws,int timeout_ms)
{
  int ready=glass_https_poll(ws->connection,POLLIN,timeout_ms);
  if(ready<=0) return ready;
  unsigned char header[2];
  int ret=ws_recv_exact(ws,header,sizeof(header));
  if(ret) return ret;
  bool fin=(header[0]&0x80)!=0,masked=(header[1]&0x80)!=0;
  unsigned opcode=header[0]&15;
  uint64_t size=header[1]&127;
  if(size==126) {
    unsigned char extended[2];
    if((ret=ws_recv_exact(ws,extended,sizeof(extended)))) return ret;
    size=((uint64_t)extended[0]<<8)|extended[1];
  } else if(size==127) {
    unsigned char extended[8];
    if((ret=ws_recv_exact(ws,extended,sizeof(extended)))) return ret;
    size=0;
    for(unsigned i=0;i<8;i++) size=(size<<8)|extended[i];
  }
  if(size>VOICE_JSON_BYTES) return -EMSGSIZE;
  unsigned char mask[4]={0};
  if(masked&&(ret=ws_recv_exact(ws,mask,sizeof(mask)))) return ret;
  unsigned char payload[VOICE_JSON_BYTES];
  if(size&&(ret=ws_recv_exact(ws,payload,(size_t)size))) return ret;
  if(masked) for(size_t i=0;i<size;i++) payload[i]^=mask[i&3];
  if(opcode==8) { ws->closed=true; return 1; }
  if(opcode==9) return ws_send_frame(ws,10,payload,(size_t)size)?-EIO:1;
  if(opcode==10) return 1;
  if(opcode==1||opcode==2) {
    ws->fragment_opcode=opcode;
    ws->json_used=0;
  } else if(opcode!=0) return -EPROTO;
  if(ws->fragment_opcode==1) {
    if(size>VOICE_JSON_BYTES-ws->json_used) return -EMSGSIZE;
    memcpy(ws->json+ws->json_used,payload,(size_t)size);
    ws->json_used+=(size_t)size;
    if(fin) {
      ws->json[ws->json_used]=0;
      ws_json(ws,ws->json);
      ws->json_used=0;
      ws->fragment_opcode=0;
    }
  } else if(fin) {
    ws->fragment_opcode=0;
    ws->json_used=0;
  }
  return 1;
}

static int ws_wait_start(struct voice_ws *ws)
{
  int64_t end=glass_https_milliseconds()+8000;
  while(!ws->started&&!ws->failed&&!ws->closed&&!atomic_load(&voice_cancel)) {
    int left=(int)(end-glass_https_milliseconds());
    if(left<=0) return -ETIMEDOUT;
    int ret=ws_receive(ws,left>250?250:left);
    if(ret<0) return ret;
  }
  return atomic_load(&voice_cancel)?-ECANCELED:ws->started?0:ws->failed?-EACCES:-ECONNRESET;
}

static int voice_audio_lock(void)
{
  if(atomic_load(&voice_stop))return -ECANCELED;
  int64_t end=glass_https_milliseconds()+5000;
  for(;;){
    if(atomic_load(&voice_stop))return -ECANCELED;
    int ret=music_pcm_session_trylock();
    if(ret!=-EBUSY)return ret;
    if(glass_https_milliseconds()>=end)return -ETIMEDOUT;
    usleep(10000);
  }
}
#define VOICE_CAPTURE_CANCELLED() atomic_load(&voice_stop)
#define VOICE_CAPTURE_LOCK() voice_audio_lock()
#include "glass_voice_capture.inc"

static enum glass_voice_error connect_error(int ret)
{
  return ret==-ETIME?GLASS_VOICE_ERROR_CLOCK:
    ret==-EACCES?GLASS_VOICE_ERROR_AUTH:
    ret==-ENOMEM?GLASS_VOICE_ERROR_MEMORY:
    ret==-EPROTO?GLASS_VOICE_ERROR_TLS:GLASS_VOICE_ERROR_NETWORK;
}

#include "glass_voice_wake.inc"

/* Only this producer touches DMA buffers. Network stalls cannot starve I2S.
 * The bounded PSRAM queue preserves order; overflow fails the whole utterance. */
struct voice_stream {
  pthread_mutex_t lock;
  int16_t pcm[VOICE_RING_FRAMES];
  size_t read,used;
  uint64_t samples;
  unsigned peak;
  int error;
  int64_t started,elapsed;
  bool ready,done,duration_limit;
};
static void *voice_capture_worker(void *argument)
{
  struct voice_stream *stream=argument;struct voice_capture capture;
  int ret=capture_open(&capture);
#ifdef GLASS_LOCAL_SPEECH
  struct glass_custom_wake *wake=ret?NULL:voice_wake_open();
#endif
  int64_t started=glass_https_milliseconds(),end=started+GLASS_VOICE_MAX_MS;
  pthread_mutex_lock(&stream->lock);stream->ready=!ret;stream->started=started;pthread_mutex_unlock(&stream->lock);
  if(!ret)voice_publish(GLASS_VOICE_LISTENING,"",0);
  int16_t mono[VOICE_FRAMES];
  while(!ret&&!atomic_load(&voice_stop)&&glass_https_milliseconds()<end){
    unsigned level=0;int frames=capture_next(&capture,mono,&level);
    if(frames<0){ret=frames;break;}
    if(!frames)continue;
    pthread_mutex_lock(&stream->lock);
    if((size_t)frames>VOICE_RING_FRAMES-stream->used)ret=-ENOBUFS;
    else{
      size_t write=(stream->read+stream->used)%VOICE_RING_FRAMES;
      size_t first=VOICE_RING_FRAMES-write;if(first>(size_t)frames)first=frames;
      memcpy(stream->pcm+write,mono,first*sizeof(*mono));
      memcpy(stream->pcm,mono+first,((size_t)frames-first)*sizeof(*mono));
      stream->used+=(size_t)frames;stream->samples+=(unsigned)frames;
      if(level>stream->peak)stream->peak=level;
    }
    pthread_mutex_unlock(&stream->lock);
    pthread_mutex_lock(&voice_lock);
    if(voice_worker_request==voice.request){voice.level=level>32767?32767:level;
#ifdef GLASS_LOCAL_SPEECH
      voice.wake_observing=wake!=NULL;
#endif
    }
    pthread_mutex_unlock(&voice_lock);
#ifdef GLASS_LOCAL_SPEECH
    if(!atomic_load(&voice_cancel))voice_wake_feed(&wake,mono,(size_t)frames);
#endif
  }
  capture_close(&capture);
#ifdef GLASS_LOCAL_SPEECH
  voice_wake_close(wake);
#endif
  pthread_mutex_lock(&voice_lock);
  if(voice_worker_request==voice.request)voice.wake_observing=false;
  pthread_mutex_unlock(&voice_lock);
  pthread_mutex_lock(&stream->lock);
  stream->elapsed=glass_https_milliseconds()-started;
  stream->duration_limit=!atomic_load(&voice_stop)&&glass_https_milliseconds()>=end;
  stream->error=ret;stream->done=true;
  pthread_mutex_unlock(&stream->lock);
  memset(mono,0,sizeof(mono));return NULL;
}
static size_t voice_stream_take(struct voice_stream *s,int16_t *out,bool *done,int *error)
{
  pthread_mutex_lock(&s->lock);
  *done=s->done;*error=s->error;size_t n=s->used;
  if(n>VOICE_PACKET_BYTES/2)n=VOICE_PACKET_BYTES/2;
  if(n<VOICE_PACKET_BYTES/2&&!s->done)n=0;
  size_t first=VOICE_RING_FRAMES-s->read;if(first>n)first=n;
  memcpy(out,s->pcm+s->read,first*sizeof(*out));
  memcpy(out+first,s->pcm,(n-first)*sizeof(*out));
  s->read=(s->read+n)%VOICE_RING_FRAMES;s->used-=n;
  *done=s->done&&!s->used;
  pthread_mutex_unlock(&s->lock);return n;
}
static void voice_run(struct portal_voice_config *config)
{
  char path[1152];int ret=voice_path(config,path,sizeof(path));
  memset(config,0,sizeof(*config));free(config);
  if(ret||atomic_load(&voice_cancel)){voice_finish(connect_error(ret),NULL);return;}
  struct voice_stream *stream=calloc(1,sizeof(*stream));
  if(!stream){voice_finish(GLASS_VOICE_ERROR_MEMORY,NULL);return;}
  pthread_mutex_init(&stream->lock,NULL);
  pthread_attr_t attr;pthread_attr_init(&attr);
#ifndef __linux__
  pthread_attr_setstacksize(&attr,96*1024);
#endif
  pthread_t capture_thread;ret=pthread_create(&capture_thread,&attr,voice_capture_worker,stream);
  pthread_attr_destroy(&attr);
  if(ret){pthread_mutex_destroy(&stream->lock);free(stream);voice_finish(GLASS_VOICE_ERROR_MEMORY,NULL);return;}
  /* Start capture before DNS/TLS; keep the beginning of an immediate utterance. */
  bool ready=false,done=false;
  while(!ready&&!done&&!atomic_load(&voice_cancel)){
    pthread_mutex_lock(&stream->lock);ready=stream->ready;done=stream->done;ret=stream->error;pthread_mutex_unlock(&stream->lock);
    if(!ready&&!done)usleep(10000);
  }
  struct voice_ws ws={0};enum glass_voice_error error=GLASS_VOICE_ERROR_NONE;
  uint64_t sent=0;unsigned packets=0;
  if(!ready){error=ret==-ENOMEM?GLASS_VOICE_ERROR_MEMORY:GLASS_VOICE_ERROR_AUDIO;goto cleanup;}
  ret=ws_open(&ws,path);memset(path,0,sizeof(path));
  if(!ret)ret=ws_wait_start(&ws);
  if(ret){error=ws.failed?GLASS_VOICE_ERROR_AUTH:connect_error(ret);goto cleanup;}
  int16_t packet[VOICE_PACKET_BYTES/2];int64_t send_started=glass_https_milliseconds();
  for(;;){
    if(atomic_load(&voice_cancel)){ret=-ECANCELED;break;}
    int capture_error=0;size_t n=voice_stream_take(stream,packet,&done,&capture_error);
    if(capture_error){ret=capture_error;error=ret==-ENOBUFS?GLASS_VOICE_ERROR_INCOMPLETE:GLASS_VOICE_ERROR_AUDIO;break;}
    if(n){
      /* Buffered connection-time audio still goes to the service at 1:1 speed. */
      int64_t due=send_started+(int64_t)(sent*1000/VOICE_RATE);
      while(glass_https_milliseconds()<due&&!atomic_load(&voice_cancel))usleep(10000);
      if(atomic_load(&voice_cancel)){ret=-ECANCELED;break;}
      ret=ws_send_frame(&ws,2,packet,n*sizeof(*packet));
      if(ret)break;
      sent+=n;packets++;
    }
    /* Poll needs a fresh deadline even while no PCM packet is ready. */
    glass_https_set_deadline(ws.connection,glass_https_milliseconds()+3000);
    for(unsigned i=0;i<4;i++){int got=ws_receive(&ws,0);if(got<0){ret=got;break;}if(!got)break;}
    if(ret<0||ws.failed||ws.closed||ws.overflow||ws.protocol_error||ws.finished)break;
    if(done)break;
    if(!n)usleep(5000);
  }
  if(!ret&&!ws.closed&&!ws.failed&&!ws.finished){
    voice_publish(GLASS_VOICE_FINISHING,ws.current_text,0);
    static const char end_message[]="{\"type\":\"end\"}";
    ret=ws_send_frame(&ws,1,end_message,sizeof(end_message)-1);
    int64_t finish_deadline=glass_https_milliseconds()+8000;
    while(!ret&&!ws.finished&&!ws.failed&&!ws.closed&&!ws.overflow&&!ws.protocol_error&&
          !atomic_load(&voice_cancel)&&glass_https_milliseconds()<finish_deadline){
      glass_https_set_deadline(ws.connection,finish_deadline);
      int got=ws_receive(&ws,200);if(got<0)ret=got;
    }
  }
  if(!error)error=transcript_error(&ws,false,ret);
  memset(packet,0,sizeof(packet));
cleanup:
  atomic_store(&voice_stop,true);pthread_join(capture_thread,NULL);
  if(stream->duration_limit)error=GLASS_VOICE_ERROR_TOO_LONG;
  if(!error&&stream->samples!=sent)error=GLASS_VOICE_ERROR_INCOMPLETE;
  printf("[voice] request=%lu capture_ms=%lu samples=%lu sent=%lu packets=%u peak=%u text_bytes=%u service=%d final=%u pending=%u error=%d cancelled=%u\n",
    (unsigned long)voice_worker_request,(unsigned long)stream->elapsed,(unsigned long)stream->samples,
    (unsigned long)sent,packets,stream->peak,(unsigned)strlen(ws.final_text),ws.service_code,
    ws.finished,ws.pending_text,error,atomic_load(&voice_cancel));
  ws_close(&ws);
  voice_finish(error,error==GLASS_VOICE_ERROR_NONE?ws.final_text:NULL);
  pthread_mutex_destroy(&stream->lock);memset(stream,0,sizeof(*stream));free(stream);
  memset(path,0,sizeof(path));
}
static void *voice_worker(void *unused)
{
  (void)unused;
  for(;;){
    pthread_mutex_lock(&voice_lock);
    struct portal_voice_config *config=voice_pending;voice_pending=NULL;
    if(!config){voice_running=false;pthread_mutex_unlock(&voice_lock);return NULL;}
    voice_worker_request=voice.request;
    atomic_store(&voice_cancel,false);atomic_store(&voice_stop,voice_pending_stop);voice_pending_stop=false;
    pthread_mutex_unlock(&voice_lock);
    voice_run(config);
  }
}
bool glass_voice_config_idle(void)
{pthread_mutex_lock(&voice_lock);bool idle=!voice_running&&!voice_pending;pthread_mutex_unlock(&voice_lock);return idle;}
static int voice_start_request(uint32_t *request)
{
  struct portal_voice_config *config=calloc(1,sizeof(*config));if(!config)return -ENOMEM;
  int ret=portal_voice_load(config);
  if(ret){memset(config,0,sizeof(*config));free(config);return ret;}
  pthread_mutex_lock(&voice_lock);
#ifdef GLASS_LOCAL_SPEECH
  /* Cancel local audio in request order, before a new local-wake request can
   * supersede this ASR. A delayed capture worker must never cancel its owner. */
  glass_speech_stop();
#endif
  if(voice_running){atomic_store(&voice_cancel,true);atomic_store(&voice_stop,true);}
  if(voice_pending){memset(voice_pending,0,sizeof(*voice_pending));free(voice_pending);}
  voice_pending=config;voice_pending_stop=false;
  if(!++voice.request)voice.request=1;
  voice.active=true;voice.wake_observing=false;voice.error=GLASS_VOICE_ERROR_NONE;
  voice.phase=GLASS_VOICE_CONNECTING;voice.level=0;voice.text[0]=0;voice_changed();
  if(request)*request=voice.request;
  if(!voice_running){
    voice_running=true;pthread_attr_t attr;pthread_attr_init(&attr);
#ifndef __linux__
    pthread_attr_setstacksize(&attr,64*1024);
#endif
    pthread_t thread;ret=pthread_create(&thread,&attr,voice_worker,NULL);pthread_attr_destroy(&attr);
    if(!ret)pthread_detach(thread);
    else{voice_running=false;voice_pending=NULL;memset(config,0,sizeof(*config));free(config);
      voice.active=false;voice.error=GLASS_VOICE_ERROR_MEMORY;voice.phase=GLASS_VOICE_ERROR;voice_changed();}
  }
  pthread_mutex_unlock(&voice_lock);return -ret;
}
int glass_voice_start_request(uint32_t *request)
{
  if(request)*request=0;
  if(!glass_ai_start_enter())return -EBUSY;
  int ret=voice_start_request(request);glass_ai_start_leave();return ret;
}
int glass_voice_start(void){return glass_voice_start_request(NULL);}
