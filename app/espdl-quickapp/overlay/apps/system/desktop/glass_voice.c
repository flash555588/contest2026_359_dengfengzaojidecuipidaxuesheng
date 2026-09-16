/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_voice.h"
#include "glass_portal.h"
#include "glass_https.h"
#include "glass_music.h"
#include "glass_music_pcm.h"
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
#define VOICE_MAX_MS 15000
#define VOICE_BUFFERS 4
#define VOICE_BYTES 4032
#define VOICE_FRAMES (VOICE_BYTES / 4)
#define VOICE_SETTLE_FRAMES 1024
#define VOICE_JSON_BYTES 4096

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
  int service_code;
  char final_text[sizeof(((struct glass_voice_snapshot *)0)->text)];
  char current_text[sizeof(((struct glass_voice_snapshot *)0)->text)];
};

struct voice_capture {
  int fd;
  mqd_t mq;
  char mqname[48];
  struct ap_buffer_s *buffers[VOICE_BUFFERS];
  unsigned count;
  bool reserved;
  bool registered;
  bool started;
  bool session_locked;
  unsigned silent_waits;
  unsigned settle;
};

static pthread_mutex_t voice_lock = PTHREAD_MUTEX_INITIALIZER;
static struct glass_voice_snapshot voice;
static atomic_bool voice_stop;

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
  voice.phase=phase;
  voice.level=level;
  if(text) snprintf(voice.text,sizeof(voice.text),"%s",text);
  voice_changed();
  pthread_mutex_unlock(&voice_lock);
}

static void voice_finish(enum glass_voice_error error,const char *text)
{
  pthread_mutex_lock(&voice_lock);
  voice.active=false;
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

void glass_voice_stop(void)
{
  atomic_store(&voice_stop,true);
  pthread_mutex_lock(&voice_lock);
  if(voice.active&&voice.phase==GLASS_VOICE_LISTENING) {
    voice.phase=GLASS_VOICE_FINISHING;
    voice_changed();
  }
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
    "engine_model_type=16k_zh&expired=%lld&filter_empty_result=1&needvad=0"
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
  if(size>VOICE_BYTES) return -EMSGSIZE;
  unsigned char header[8],mask[4],payload[VOICE_BYTES];
  size_t h=0;
  header[h++]=0x80|(opcode&15);
  if(size<126) header[h++]=0x80|(unsigned char)size;
  else {
    header[h++]=0x80|126;
    header[h++]=(unsigned char)(size>>8);
    header[h++]=(unsigned char)size;
  }
  int ret=random_bytes(mask,sizeof(mask));
  if(ret) return ret;
  memcpy(header+h,mask,sizeof(mask)); h+=sizeof(mask);
  const unsigned char *source=data;
  for(size_t i=0;i<size;i++) payload[i]=source[i]^mask[i&3];
  ret=ws_send_all(ws,header,h);
  if(!ret&&size) ret=ws_send_all(ws,payload,size);
  memset(payload,0,size);
  return ret;
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

static void append_text(char *out,size_t capacity,const char *text)
{
  size_t used=strlen(out),add=strlen(text);
  if(used>=capacity-1) return;
  if(add>capacity-used-1) add=capacity-used-1;
  memcpy(out+used,text,add);
  out[used+add]=0;
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
    cJSON *text=cJSON_GetObjectItemCaseSensitive(result,"voice_text_str");
    const char *piece=cJSON_IsString(text)?text->valuestring:"";
    if(cJSON_IsNumber(slice)&&slice->valueint==2&&*piece) {
      append_text(ws->final_text,sizeof(ws->final_text),piece);
      snprintf(ws->current_text,sizeof(ws->current_text),"%s",ws->final_text);
    } else if(cJSON_IsNumber(slice)&&slice->valueint==1&&*piece) {
      snprintf(ws->current_text,sizeof(ws->current_text),"%s",ws->final_text);
      append_text(ws->current_text,sizeof(ws->current_text),piece);
    }
    if(ws->current_text[0])
      voice_publish(GLASS_VOICE_LISTENING,ws->current_text,0);
  } else {
    ws->started=true;
  }
  cJSON *final=cJSON_GetObjectItemCaseSensitive(root,"final");
  if(cJSON_IsNumber(final)&&final->valueint==1) ws->finished=true;
  cJSON_Delete(root);
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
  while(!ws->started&&!ws->failed&&!ws->closed&&!atomic_load(&voice_stop)) {
    int left=(int)(end-glass_https_milliseconds());
    if(left<=0) return -ETIMEDOUT;
    int ret=ws_receive(ws,left>250?250:left);
    if(ret<0) return ret;
  }
  return ws->started?0:ws->failed?-EACCES:-ECONNRESET;
}

static int capture_open(struct voice_capture *capture)
{
  memset(capture,0,sizeof(*capture));
  capture->fd=-1;
  capture->mq=(mqd_t)-1;
  capture->settle=VOICE_SETTLE_FRAMES;
  int ret=glass_music_stop_wait(5000);
  if(ret) return ret;
  ret=music_pcm_session_lock();
  if(ret) return ret;
  capture->session_locked=true;
  capture->fd=open("/dev/audio/pcm_in0",O_RDWR);
  if(capture->fd<0) { ret=-errno; goto fail; }
  if(ioctl(capture->fd,AUDIOIOC_RESERVE,0)<0) { ret=-errno; goto fail; }
  capture->reserved=true;
  struct audio_caps_desc_s caps={0};
  caps.caps.ac_len=sizeof(caps.caps);
  caps.caps.ac_type=AUDIO_TYPE_INPUT;
  caps.caps.ac_subtype=AUDIO_FMT_PCM;
  caps.caps.ac_channels=2;
  caps.caps.ac_controls.hw[0]=VOICE_RATE;
  caps.caps.ac_controls.b[2]=16;
  if(ioctl(capture->fd,AUDIOIOC_CONFIGURE,(uintptr_t)&caps)<0) {
    ret=-errno; goto fail;
  }
  struct mq_attr attr={.mq_maxmsg=10,.mq_msgsize=sizeof(struct audio_msg_s)};
  snprintf(capture->mqname,sizeof(capture->mqname),"/ime-voice-%lx",
           (unsigned long)pthread_self());
  mq_unlink(capture->mqname);
  capture->mq=mq_open(capture->mqname,O_RDWR|O_CREAT|O_EXCL,0600,&attr);
  if(capture->mq==(mqd_t)-1) { ret=-errno; goto fail; }
  if(ioctl(capture->fd,AUDIOIOC_REGISTERMQ,(uintptr_t)capture->mq)<0) {
    ret=-errno; goto fail;
  }
  capture->registered=true;
  struct ap_buffer_info_s info={0};
  if(ioctl(capture->fd,AUDIOIOC_GETBUFFERINFO,(uintptr_t)&info)<0) {
    ret=-errno; goto fail;
  }
  capture->count=info.nbuffers<VOICE_BUFFERS?info.nbuffers:VOICE_BUFFERS;
  if(capture->count<2) { ret=-ENOBUFS; goto fail; }
  for(unsigned i=0;i<capture->count;i++) {
    struct audio_buf_desc_s desc={.numbytes=VOICE_BYTES,
                                  .u.pbuffer=&capture->buffers[i]};
    int size=ioctl(capture->fd,AUDIOIOC_ALLOCBUFFER,(uintptr_t)&desc);
    if(size!=sizeof(desc)||!capture->buffers[i]) {
      ret=size<0?-errno:-ENOMEM; goto fail;
    }
  }
  if(ioctl(capture->fd,AUDIOIOC_START,0)<0) { ret=-errno; goto fail; }
  capture->started=true;
  for(unsigned i=0;i<capture->count;i++) {
    struct ap_buffer_s *buffer=capture->buffers[i];
    buffer->nbytes=buffer->nmaxbytes; buffer->curbyte=0; buffer->flags=0;
    struct audio_buf_desc_s desc={.numbytes=buffer->nbytes,.u.buffer=buffer};
    if(ioctl(capture->fd,AUDIOIOC_ENQUEUEBUFFER,(uintptr_t)&desc)<0) {
      ret=-errno; goto fail;
    }
  }
  return 0;
fail:
  return ret;
}

static void capture_close(struct voice_capture *capture)
{
  if(capture->fd>=0&&capture->started) ioctl(capture->fd,AUDIOIOC_STOP,0);
  if(capture->fd>=0) for(unsigned i=0;i<VOICE_BUFFERS;i++)
    if(capture->buffers[i]) {
      struct audio_buf_desc_s desc={.u.buffer=capture->buffers[i]};
      ioctl(capture->fd,AUDIOIOC_FREEBUFFER,(uintptr_t)&desc);
    }
  if(capture->fd>=0&&capture->registered)
    ioctl(capture->fd,AUDIOIOC_UNREGISTERMQ,(uintptr_t)capture->mq);
  if(capture->fd>=0&&capture->reserved) ioctl(capture->fd,AUDIOIOC_RELEASE,0);
  if(capture->fd>=0) close(capture->fd);
  if(capture->mq!=(mqd_t)-1) {
    mq_close(capture->mq);
    mq_unlink(capture->mqname);
  }
  if(capture->session_locked) music_pcm_session_unlock();
  capture->fd=-1;
}

static unsigned magnitude16(int value)
{ return value<0?(unsigned)(-value):(unsigned)value; }

static unsigned downmix(int16_t *mono,const int16_t *stereo,unsigned frames)
{
  uint64_t left=0,right=0;
  for(unsigned i=0;i<frames;i++) {
    left+=magnitude16(stereo[i*2]);
    right+=magnitude16(stereo[i*2+1]);
  }
  unsigned channel=left>right*2+256?1:right>left*2+256?2:0;
  unsigned peak=0;
  for(unsigned i=0;i<frames;i++) {
    int value=channel==1?stereo[i*2]:channel==2?stereo[i*2+1]:
      ((int)stereo[i*2]+(int)stereo[i*2+1])/2;
    mono[i]=(int16_t)value;
    unsigned level=magnitude16(value);
    if(level>peak) peak=level;
  }
  return peak;
}

/* Returns mono frames, zero for a timed wait, or a negative errno. */
static int capture_next(struct voice_capture *capture,int16_t mono[VOICE_FRAMES],
                        unsigned *level)
{
  struct timespec until;
  clock_gettime(CLOCK_REALTIME,&until);
  until.tv_nsec+=100000000;
  if(until.tv_nsec>=1000000000) { until.tv_sec++; until.tv_nsec-=1000000000; }
  struct audio_msg_s message;
  ssize_t n;
  do { n=mq_timedreceive(capture->mq,(char *)&message,sizeof(message),NULL,&until); }
  while(n<0&&errno==EINTR);
  if(n<0&&errno==ETIMEDOUT)
    return ++capture->silent_waits<30?0:-ETIMEDOUT;
  if(n!=sizeof(message)) return n<0?-errno:-EIO;
  if(message.msg_id==AUDIO_MSG_IOERR||message.msg_id==AUDIO_MSG_COMPLETE)
    return -EIO;
  if(message.msg_id!=AUDIO_MSG_DEQUEUE) return 0;
  struct ap_buffer_s *buffer=message.u.ptr;
  bool known=false;
  for(unsigned i=0;i<capture->count;i++) if(buffer==capture->buffers[i]) known=true;
  if(!known||!buffer->nbytes||buffer->nbytes>VOICE_BYTES||buffer->nbytes%4)
    return -EIO;
  capture->silent_waits=0;
  unsigned frames=buffer->nbytes/4;
  *level=downmix(mono,(const int16_t *)buffer->samp,frames);
  buffer->curbyte=0; buffer->flags=0; buffer->nbytes=buffer->nmaxbytes;
  struct audio_buf_desc_s next={.numbytes=buffer->nbytes,.u.buffer=buffer};
  if(ioctl(capture->fd,AUDIOIOC_ENQUEUEBUFFER,(uintptr_t)&next)<0) return -errno;
  if(capture->settle) {
    unsigned skip=capture->settle<frames?capture->settle:frames;
    capture->settle-=skip;
    memmove(mono,mono+skip,(frames-skip)*sizeof(*mono));
    frames-=skip;
  }
  return (int)frames;
}

static enum glass_voice_error connect_error(int ret)
{
  return ret==-ETIME?GLASS_VOICE_ERROR_CLOCK:
    ret==-EACCES?GLASS_VOICE_ERROR_AUTH:
    ret==-ENOMEM?GLASS_VOICE_ERROR_MEMORY:
    ret==-EPROTO?GLASS_VOICE_ERROR_TLS:GLASS_VOICE_ERROR_NETWORK;
}

static void *voice_worker(void *argument)
{
  struct portal_voice_config *config=argument;
  char path[1152];
  int ret=voice_path(config,path,sizeof(path));
  memset(config,0,sizeof(*config));
  free(config);
  if(ret) { voice_finish(connect_error(ret),NULL); return NULL; }
  struct voice_ws ws;
  ret=ws_open(&ws,path);
  memset(path,0,sizeof(path));
  if(ret) { voice_finish(connect_error(ret),NULL); return NULL; }
  ret=ws_wait_start(&ws);
  if(ret) {
    enum glass_voice_error error=ws.failed?GLASS_VOICE_ERROR_AUTH:connect_error(ret);
    ws_close(&ws); voice_finish(error,NULL); return NULL;
  }
  struct voice_capture capture;
  ret=capture_open(&capture);
  if(ret) {
    capture_close(&capture);
    ws_close(&ws);
    voice_finish(ret==-ENOMEM?GLASS_VOICE_ERROR_MEMORY:GLASS_VOICE_ERROR_AUDIO,NULL);
    return NULL;
  }
  voice_publish(GLASS_VOICE_LISTENING,"",0);
  int16_t mono[VOICE_FRAMES];
  uint64_t samples=0;
  unsigned peak=0;
  int64_t end=glass_https_milliseconds()+VOICE_MAX_MS;
  while(!atomic_load(&voice_stop)&&glass_https_milliseconds()<end&&!ws.failed&&!ws.closed) {
    unsigned level=0;
    int frames=capture_next(&capture,mono,&level);
    if(frames<0) { ret=frames; break; }
    if(level>peak) peak=level;
    if(frames>0) {
      ret=ws_send_frame(&ws,2,mono,(size_t)frames*2);
      if(ret) break;
      samples+=(unsigned)frames;
      pthread_mutex_lock(&voice_lock);
      voice.level=level>32767?32767:level;
      pthread_mutex_unlock(&voice_lock);
    }
    for(unsigned i=0;i<4;i++) {
      int got=ws_receive(&ws,0);
      if(got<0) { ret=got; break; }
      if(!got) break;
    }
    if(ret<0) break;
  }
  capture_close(&capture);
  if(ws.failed) ret=-EACCES;
  if(ret<0||ws.closed) {
    enum glass_voice_error error=ws.failed?GLASS_VOICE_ERROR_AUTH:
      ret==-ENOMEM?GLASS_VOICE_ERROR_MEMORY:GLASS_VOICE_ERROR_NETWORK;
    ws_close(&ws);
    printf("[voice] stream failed ret=%d samples=%lu peak=%u\n",
           ret,(unsigned long)samples,peak);
    voice_finish(error,NULL);
    return NULL;
  }
  voice_publish(GLASS_VOICE_FINISHING,ws.current_text,0);
  static const char end_message[]="{\"type\":\"end\"}";
  ret=ws_send_frame(&ws,1,end_message,sizeof(end_message)-1);
  int64_t finish_deadline=glass_https_milliseconds()+8000;
  while(!ret&&!ws.finished&&!ws.failed&&!ws.closed&&
        glass_https_milliseconds()<finish_deadline) {
    int got=ws_receive(&ws,200);
    if(got<0) { ret=got; break; }
  }
  char result[sizeof(ws.current_text)];
  snprintf(result,sizeof(result),"%s",ws.final_text[0]?ws.final_text:ws.current_text);
  enum glass_voice_error error=GLASS_VOICE_ERROR_NONE;
  if(ws.failed) error=GLASS_VOICE_ERROR_AUTH;
  else if(ret<0) error=GLASS_VOICE_ERROR_NETWORK;
  else if(!result[0]) error=GLASS_VOICE_ERROR_EMPTY;
  ws_close(&ws);
  printf("[voice] complete samples=%lu peak=%u text_bytes=%u error=%d\n",
         (unsigned long)samples,peak,(unsigned)strlen(result),error);
  voice_finish(error,error==GLASS_VOICE_ERROR_NONE?result:NULL);
  memset(result,0,sizeof(result));
  return NULL;
}

int glass_voice_start(void)
{
  struct portal_voice_config *config=calloc(1,sizeof(*config));
  if(!config) return -ENOMEM;
  int ret=portal_voice_load(config);
  pthread_mutex_lock(&voice_lock);
  if(voice.active) {
    pthread_mutex_unlock(&voice_lock);
    memset(config,0,sizeof(*config)); free(config);
    return -EBUSY;
  }
  if(ret) {
    voice.error=GLASS_VOICE_ERROR_CONFIG;
    voice.phase=GLASS_VOICE_ERROR;
    voice.text[0]=0;
    voice_changed();
    pthread_mutex_unlock(&voice_lock);
    memset(config,0,sizeof(*config)); free(config);
    return ret;
  }
  atomic_store(&voice_stop,false);
  voice.active=true;
  voice.error=GLASS_VOICE_ERROR_NONE;
  voice.phase=GLASS_VOICE_CONNECTING;
  voice.level=0;
  voice.text[0]=0;
  voice_changed();
  pthread_mutex_unlock(&voice_lock);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
#ifndef __linux__
  pthread_attr_setstacksize(&attr,32768);
#endif
  pthread_t thread;
  ret=pthread_create(&thread,&attr,voice_worker,config);
  pthread_attr_destroy(&attr);
  if(ret) {
    memset(config,0,sizeof(*config)); free(config);
    voice_finish(ret==ENOMEM?GLASS_VOICE_ERROR_MEMORY:GLASS_VOICE_ERROR_RESPONSE,NULL);
    return -ret;
  }
  pthread_detach(thread);
  return 0;
}
