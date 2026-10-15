/* SPDX-License-Identifier: Apache-2.0 */
/* Native ports of Pure Live Huya/CC adapter contracts; reuse desktop TLS/PSA. */
#include "live_platform.h"
#include "bili_http.h"
#include <cJSON.h>
#include <psa/crypto.h>
#include <mbedtls/base64.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
static const cJSON *get(const cJSON *o,const char *key)
{ return cJSON_GetObjectItemCaseSensitive(o,key); }
static const char *str(const cJSON *o,const char *key)
{ const cJSON *v=get(o,key);return cJSON_IsString(v)?v->valuestring:NULL; }
static int integer(const cJSON *o,const char *key,int fallback)
{ const cJSON *v=get(o,key);return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble>=0 && v->valuedouble<=10000000 && floor(v->valuedouble)==v->valuedouble?v->valueint:fallback; }
const char *live_platform_name(int p)
{ return p==LIVE_HUYA?"虎牙":p==LIVE_CC?"网易CC":"B站"; }
static int add_url(struct bili_streams *out,const char *url)
{
  if (!url || strlen(url)>=BILI_URL_MAX || strpbrk(url,"\r\n") ||
      (strncmp(url,"https://",8) && strncmp(url,"http://",7))) return -EPROTO;
  for(int i=0;i<out->count;i++) if(!strcmp(out->urls[i],url)) return 0;
  if(out->count<BILI_MAX_LINES) strcpy(out->urls[out->count++],url);
  return 0;
}
static int hex(int c)
{ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static int query(const char *q,const char *key,char *out,size_t size)
{
  size_t n=strlen(key);
  while(q && *q) {
    const char *end=strchr(q,'&');if(!end)end=q+strlen(q);
    if((size_t)(end-q)>n && !strncmp(q,key,n) && q[n]=='=') {
      const char *p=q+n+1;size_t i=0;
      while(p<end) {
        int c=(unsigned char)*p++;
        if(c=='%') {if(end-p<2 || hex(p[0])<0 || hex(p[1])<0)return -EPROTO;c=hex(p[0])*16+hex(p[1]);p+=2;}
        else if(c=='+') c=' ';
        if(!c || c=='\r' || c=='\n' || i+1>=size)return -EFBIG;
        out[i++]=c;
      }
      out[i]=0;return 0;
    }
    q=*end?end+1:end;
  }
  return -ENOENT;
}
static int md5(const char *s,char out[33])
{
  uint8_t hash[16];size_t n=0;
  if(psa_crypto_init()!=PSA_SUCCESS || psa_hash_compute(PSA_ALG_MD5,(const uint8_t *)s,strlen(s),hash,sizeof(hash),&n)!=PSA_SUCCESS || n!=16)return -EIO;
  for(int i=0;i<16;i++)sprintf(out+i*2,"%02x",hash[i]);return 0;
}
static int huya_url(const cJSON *line,int rate,char out[BILI_URL_MAX])
{
  const char *anti=str(line,"sFlvAntiCode"),*name=str(line,"sStreamName"),*host=str(line,"sFlvUrl");
  char fm[1024],ctype[64],t[16],ws[32],fs[64],decoded[768],material[1024],part[128],hash[33],secret[33];
  size_t len=0,pos=0;struct timespec now;unsigned long long millis,seq;
  uint32_t uid=1460000000u,rot=(uid<<8)|(uid>>24);
  if(!anti||!name||!host || strlen(name)>160 || strpbrk(name,"\r\n/?&") ||
     (strncmp(host,"http://",7) && strncmp(host,"https://",8)))return -EPROTO;
  if(query(anti,"fm",fm,sizeof(fm)) || query(anti,"ctype",ctype,sizeof(ctype)) ||
     query(anti,"t",t,sizeof(t)) || query(anti,"wsTime",ws,sizeof(ws)))return -EPROTO;
  /* These server query fields are emitted verbatim; accept only unreserved. */
  if(strspn(ctype,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")!=strlen(ctype) ||
     strspn(t,"0123456789")!=strlen(t) || strspn(ws,"0123456789abcdefABCDEF")!=strlen(ws))return -EPROTO;
  if(query(anti,"fs",fs,sizeof(fs)))strcpy(fs,"bgct");
  if(strspn(fs,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")!=strlen(fs))return -EPROTO;
  if(mbedtls_base64_decode((unsigned char *)decoded,sizeof(decoded)-1,&len,(unsigned char *)fm,strlen(fm)))return -EPROTO;
  decoded[len]=0;
  for(int i=0;i<4;i++){snprintf(part,sizeof(part),"$%d",i);if(!strstr(decoded,part))return -EPROTO;}
  clock_gettime(CLOCK_REALTIME,&now);millis=(unsigned long long)now.tv_sec*1000+now.tv_nsec/1000000;
  char *end;unsigned long long expiry=strtoull(ws,&end,16);
  if(*end || !expiry || (unsigned long long)now.tv_sec>expiry+300)return -ESTALE;
  seq=uid+millis;snprintf(part,sizeof(part),"%llu|%s|%s",seq,ctype,t);
  if(md5(part,hash))return -EIO;
  for(const char *p=decoded;*p;) {
    const char *v=NULL;char num[32];size_t n=1;
    if(p[0]=='$' && p[1]>='0' && p[1]<='3') {
      switch(p[1]){case '0':snprintf(num,sizeof(num),"%u",!strcmp(t,"103")?uid:rot);v=num;break;case '1':v=name;break;case '2':v=hash;break;case '3':v=ws;break;}
      n=strlen(v);p+=2;
    }else{num[0]=*p++;num[1]=0;v=num;}
    if(pos+n>=sizeof(material))return -EFBIG;memcpy(material+pos,v,n);pos+=n;
  }
  material[pos]=0;if(md5(material,secret))return -EIO;
  const char *hostname=host+(!strncmp(host,"https://",8)?8:7);
  int n=snprintf(out,BILI_URL_MAX,"https://%s/%s.flv?wsSecret=%s&wsTime=%s&seqid=%llu&ctype=%s&ver=1&fs=%s&t=%s&%s=%u&codec=264&ratio=%d%s",
    hostname,name,secret,ws,seq,ctype,fs,t,!strcmp(t,"103")?"uid":"u",!strcmp(t,"103")?uid:rot,rate,!strcmp(t,"103")?"&uuid=1460000000":"");
  return n<0 || n>=BILI_URL_MAX?-EFBIG:0;
}
int live_parse_huya(const char *json,const char *room,struct bili_streams *out)
{
  cJSON *root=cJSON_ParseWithOpts(json,NULL,1);int r=-EPROTO;memset(out,0,sizeof(*out));
  if(!root)return r;
  const cJSON *data=get(root,"data"),*stream=get(data,"stream"),*tier;
  if(integer(root,"status",0)!=200)goto done;
  const char *state=str(data,"liveStatus");if(!state || strcmp(state,"ON")){r=-ENODATA;goto done;}
  if(integer(data,"roomPay",0)>0 || integer(data,"isRoomPay",0)>0){r=-EACCES;goto done;}
  int rate=0;
  cJSON_ArrayForEach(tier,get(get(stream,"flv"),"rateArray")) {
    int v=integer(tier,"iBitRate",0);if(v>0 && (!rate || v<rate) && integer(tier,"iCodecType",0)==0)rate=v;
  }
  if(!rate){r=-ENOTSUP;goto done;}
  const cJSON *line;cJSON_ArrayForEach(line,get(stream,"baseSteamInfoList")) {
    char url[BILI_URL_MAX];r=huya_url(line,rate,url);if(!r)add_url(out,url);
    if(out->count==3)break;
  }
  if(out->count){out->qn=rate;snprintf(out->quality,sizeof(out->quality),"请求 %d kbps",rate);snprintf(out->referer,sizeof(out->referer),"https://www.huya.com/%s",room);r=0;}
done:cJSON_Delete(root);return r;
}
int live_parse_cc(const char *json,struct bili_streams *out)
{
  cJSON *root=cJSON_ParseWithOpts(json,NULL,1);int r=-EPROTO,best=0x7fffffff;memset(out,0,sizeof(*out));
  if(!root)return r;
  const cJSON *room=cJSON_GetArrayItem(get(root,"data"),0),*tiers=get(room,"quickplay"),*tier,*chosen=NULL;
  if(integer(room,"status",0)!=1){r=-ENODATA;goto done;}
  if(!cJSON_IsObject(tiers))tiers=get(room,"stream_list");
  if(!cJSON_IsObject(tiers)){r=-ENOTSUP;goto done;}
  /* Only the direct live FLV contract is supported, not HLS/replay. */
  if(get(tiers,"resolution")){r=-ENOTSUP;goto done;}
  cJSON_ArrayForEach(tier,tiers) {
    int v=integer(tier,"vbr",0);
    if(v>0 && v<best && str(get(tier,"CDN_FMT"),"ali") && str(tier,"streamname")){best=v;chosen=tier;}
  }
  const char *rid=str(room,"ccid");char roomid[16];
  if(!rid){const cJSON *id=get(room,"ccid");if(cJSON_IsNumber(id) && isfinite(id->valuedouble) && id->valuedouble>0 && id->valuedouble<=4294967295.0 && floor(id->valuedouble)==id->valuedouble)snprintf(roomid,sizeof(roomid),"%.0f",id->valuedouble);else roomid[0]=0;rid=roomid;}
  const char *name=str(chosen,"streamname"),*token=str(get(chosen,"CDN_FMT"),"ali");
  if(!chosen||!name||!token||!bili_valid_room(rid) || !*name || strlen(name)>160 ||
     strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(name)) {r=-ENOTSUP;goto done;}
  /* /redirect/video/<room> ignores the tier and returns the original stream.
   * The API's tier-specific streamname and token belong to /live/<name>.flv.
   * Keep only the CDN contract verified with actual low-tier H.264 decoding.
   */
  char url[BILI_URL_MAX];int n=snprintf(url,sizeof(url),"https://alirtspull.cc.netease.com/live/%s.flv?%s",name,token);
  if(n<0 || n>=sizeof(url)){r=-EFBIG;goto done;}
  r=add_url(out,url);
  if(out->count){out->qn=best;snprintf(out->quality,sizeof(out->quality),"请求 %d kbps",best);snprintf(out->referer,sizeof(out->referer),"https://cc.163.com/%s",rid);r=0;}else r=-ENOTSUP;
done:cJSON_Delete(root);return r;
}
int live_resolve(int platform,const char *room,const char *cookie,atomic_bool *cancel,struct bili_streams *out)
{
  char url[512],referer[96],*json=NULL;int r;unsigned long id;
  if(!bili_valid_room(room)||platform<0||platform>=LIVE_PLATFORM_COUNT)return -EINVAL;
  if(atomic_load(cancel))return -ECANCELED;
  snprintf(referer,sizeof(referer),"%s",platform==LIVE_HUYA?"https://www.huya.com/":platform==LIVE_CC?"https://cc.163.com/":"https://live.bilibili.com/");
  if(platform==LIVE_HUYA)snprintf(url,sizeof(url),"https://mp.huya.com/cache.php?m=Live&do=profileRoom&roomid=%s",room);
  else if(platform==LIVE_CC)snprintf(url,sizeof(url),"https://api.cc.163.com/v1/activitylives/anchor/lives?anchor_ccid=%s",room);
  else snprintf(url,sizeof(url),"https://api.live.bilibili.com/room/v1/Room/room_init?id=%s",room);
  r=bili_http_json_site(url,platform==LIVE_BILI?cookie:"",referer,cancel,&json);
  if(!r && platform==LIVE_HUYA)r=live_parse_huya(json,room,out);
  if(!r && platform==LIVE_CC) {
    cJSON *root=cJSON_ParseWithOpts(json,NULL,1);const cJSON *entry=get(get(root,"data"),room),*channel=get(entry,"channel_id");char channelid[64];
    if(cJSON_IsString(channel))snprintf(channelid,sizeof(channelid),"%s",channel->valuestring);
    else if(cJSON_IsNumber(channel) && isfinite(channel->valuedouble) && channel->valuedouble>0 && channel->valuedouble<1e16 && floor(channel->valuedouble)==channel->valuedouble)snprintf(channelid,sizeof(channelid),"%.0f",channel->valuedouble);
    else channelid[0]=0;
    cJSON_Delete(root);
    if(!channelid[0] || strspn(channelid,"0123456789")!=strlen(channelid))r=-ENODATA;
    else {free(json);json=NULL;snprintf(url,sizeof(url),"https://cc.163.com/live/channel/?channelids=%s&anchor_ccid=%s",channelid,room);r=bili_http_json_site(url,"",referer,cancel,&json);if(!r)r=live_parse_cc(json,out);}
  }
  if(!r && platform==LIVE_BILI) {
    r=bili_room_id(json,&id);free(json);json=NULL;
    if(!r){
      printf("bilibili: input room=%s canonical=%lu\n",room,id);
      snprintf(url,sizeof(url),"https://api.live.bilibili.com/xlive/web-room/v1/playUrl/playUrl?cid=%lu&qn=80&platform=web",id);
      r=bili_http_json_site(url,cookie,referer,cancel,&json);
      if(!r)r=bili_low_play_info(json,out);
      if(r&&r!=-ECANCELED&&r!=-ENOMEM&&!atomic_load(cancel)){
        free(json);json=NULL;
        snprintf(url,sizeof(url),"https://api.live.bilibili.com/xlive/web-room/v2/index/getRoomPlayInfo?room_id=%lu&protocol=0&format=0&codec=0&qn=80&platform=web&ptype=8&no_playurl=0",id);
        r=bili_http_json_site(url,cookie,referer,cancel,&json);
        if(!r)r=bili_play_info(json,out);
      }
      if(!r){
        snprintf(out->referer,sizeof(out->referer),"https://live.bilibili.com/%lu",id);
        snprintf(out->quality,sizeof(out->quality),"qn=%d",out->qn);
      }
    }
  }
  free(json);if(atomic_load(cancel))return -ECANCELED;
  if(!r)printf("live: platform=%d room=%s requested=%d lines=%d\n",platform,room,out->qn,out->count);
  return r;
}
