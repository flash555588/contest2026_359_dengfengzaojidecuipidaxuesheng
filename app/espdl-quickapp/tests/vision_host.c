/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../overlay/apps/system/desktop/glass_vision.c"
static bool use_anthropic,temporal,use_mimo;
static unsigned requests,scenario;
static struct qpk_camera_clip *owned_request;
static const char *posted;
static unsigned fail_allocation,allocation_count;
static void *fixture_allocate(size_t n){if(fail_allocation&&++allocation_count==fail_allocation)return NULL;return malloc(n);}
int glass_chat_load_settings(struct portal_ai_config *out)
{
 memset(out,0,sizeof(*out));strcpy(out->api_key,"fixture-secret");strcpy(out->base_url,"https://model.invalid/v1/");
 strcpy(out->model,use_mimo?"mimo-v2.6-flash":"fixture-vision");strcpy(out->backend,use_anthropic?"anthropic_compatible":"openai_compatible");return 0;
}
const unsigned char *glass_agent_trust(size_t *n){*n=5;return (const unsigned char *)"test";}
int glass_https_initialize(void){return 0;}
int64_t glass_https_milliseconds(void){return 1000;}
const struct webclient_tls_ops *glass_https_tls_ops(void){return NULL;}
void webclient_set_defaults(struct webclient_context *c){memset(c,0,sizeof(*c));}
void webclient_set_static_body(struct webclient_context *c,const char *body,size_t n){(void)c;assert(strlen(body)==n);posted=body;}
int webclient_perform(struct webclient_context *c)
{
 if(owned_request)for(unsigned i=0;i<QPK_CLIP_FRAMES;i++)assert(!owned_request->frames[i].jpeg);
 fail_allocation=0; /* Only production request construction is fault-injected. */
 requests++;assert(!strcmp(c->method,"POST")&&c->timeout_sec==45);
 assert(!strcmp(c->url,use_anthropic?"https://model.invalid/v1/messages":"https://model.invalid/v1/chat/completions"));
 struct glass_https_request *tls=c->tls_ctx;assert(tls->deadline_ms==46000&&tls->abort_flag);
 cJSON *body=cJSON_Parse(posted);assert(body&&!vget(body,"tools")&&cJSON_IsFalse(vget(body,"stream")));
 assert(!strcmp(vstr(body,"model"),use_mimo?"mimo-v2.6-flash":"fixture-vision"));
 if(use_mimo){assert(!strcmp(vstr(vget(body,"thinking"),"type"),"disabled"));
   assert(vget(body,use_anthropic?"max_tokens":"max_completion_tokens")->valueint==1024);
   if(!use_anthropic)assert(!vget(body,"max_tokens"));
 }else assert(!vget(body,"thinking")&&vget(body,"max_tokens")->valueint==1024);
 const cJSON *message=cJSON_GetArrayItem(vget(body,"messages"),use_anthropic?0:1),*content=vget(message,"content");
 assert(cJSON_GetArraySize(content)==(temporal?7:2));const cJSON *image=cJSON_GetArrayItem(content,temporal?1:0),*text=cJSON_GetArrayItem(content,temporal?6:1);
 assert(!strcmp(vstr(text,"text"),"What is visible?"));
 if(temporal){for(unsigned i=0;i<3;i++){
   char label[64];snprintf(label,sizeof(label),"Camera sample %u at %u ms",i+1,i*2000);
   assert(!strcmp(vstr(cJSON_GetArrayItem(content,i*2),"text"),label));
   const cJSON *im=cJSON_GetArrayItem(content,i*2+1);
   if(use_anthropic)assert(!strcmp(vstr(vget(im,"source"),"data"),"/9hB/9k=")&&!strcmp(vstr(vget(im,"source"),"media_type"),"image/jpeg"));
   else assert(!strcmp(vstr(vget(im,"image_url"),"url"),"data:image/jpeg;base64,/9hB/9k="));
 }}else if(use_anthropic){assert(!strcmp(vstr(image,"type"),"image"));assert(!strcmp(vstr(vget(image,"source"),"media_type"),"image/png"));assert(!strncmp(vstr(vget(image,"source"),"data"),"iVBOR",5));}
 else{assert(!strcmp(vstr(image,"type"),"image_url"));assert(!strncmp(vstr(vget(image,"image_url"),"url"),"data:image/png;base64,iVBOR",26));}
 cJSON_Delete(body);
 if(scenario==4)return c->header_callback("Location: https://other.invalid",false,c->header_callback_arg);
 if(scenario==5){atomic_store(tls->abort_flag,true);return -ECANCELED;}
 c->http_status=scenario==1?429:200;
 char *response=use_anthropic?"{\"content\":[{\"type\":\"text\",\"text\":\"a red cup\"}],\"stop_reason\":\"end_turn\"}":
   scenario==2?"{\"choices\":[{\"message\":{\"content\":\"partial\"},\"finish_reason\":\"length\"}]}":
   "{\"choices\":[{\"message\":{\"content\":\"a red cup\"},\"finish_reason\":\"stop\"}]}";
 if(scenario==3){char *huge=calloc(VISION_RESPONSE_LIMIT+2,1);memset(huge,'x',VISION_RESPONSE_LIMIT+1);int length=VISION_RESPONSE_LIMIT+1;
   int ret=c->sink_callback(&huge,0,length,&length,c->sink_callback_arg);free(huge);return ret;}
 int n=strlen(response);for(int i=0;i<n;i++){char *p=response+i;int len=1;int ret=c->sink_callback(&p,0,1,&len,c->sink_callback_arg);if(ret)return ret;}return 0;
}
int main(int argc,char **argv)
{
 assert(argc==2);char path[512],outpath[512];snprintf(path,sizeof(path),"%s/rgb24.bmp",argv[1]);
 const char *mime=NULL;char *base64=image_base64(path,&mime);assert(base64&&!strcmp(mime,"image/png"));
 snprintf(outpath,sizeof(outpath),"%s/rgb24.b64",argv[1]);FILE *f=fopen(outpath,"w");assert(f);fputs(base64,f);fclose(f);free(base64);
 snprintf(path,sizeof(path),"%s/rgb565.bmp",argv[1]);base64=image_base64(path,&mime);assert(base64&&!strcmp(mime,"image/png"));
 snprintf(outpath,sizeof(outpath),"%s/rgb565.b64",argv[1]);f=fopen(outpath,"w");assert(f);fputs(base64,f);fclose(f);free(base64);
 const char *invalid[]={"truncated.bmp","oversized.bmp","symlink.bmp","not-image.txt"};
 for(unsigned i=0;i<4;i++){snprintf(path,sizeof(path),"%s/%s",argv[1],invalid[i]);assert(!image_base64(path,&mime));}
 snprintf(path,sizeof(path),"%s/rgb24.bmp",argv[1]);atomic_bool cancel=false;
 for(unsigned backend=0;backend<2;backend++){use_anthropic=backend;scenario=0;char *s=glass_vision_analyze(path,"What is visible?",&cancel);
   assert(s&&strstr(s,"a red cup")&&strstr(s,"cloud_image_analysis")&&!strstr(s,"fixture-secret"));free(s);}
 use_anthropic=false;
 for(scenario=1;scenario<=5;scenario++){unsigned before=requests;char *s=glass_vision_analyze(path,"What is visible?",&cancel);
   assert(s&&strstr(s,"error")&&!strstr(s,"fixture-secret")&&requests==before+1);free(s);}
 unsigned before=requests;char *s=glass_vision_analyze(path,"What is visible?",&cancel);assert(s&&strstr(s,"cancelled")&&requests==before);free(s);
 atomic_store(&cancel,false);scenario=0;temporal=true;
 unsigned char jpeg[]={255,216,'A',255,217};struct qpk_camera_clip clip={.count=3,.duration_ms=5000};
 for(unsigned i=0;i<3;i++)clip.frames[i]=(struct qpk_clip_frame){jpeg,sizeof(jpeg),i*2000};
 for(unsigned backend=0;backend<2;backend++){
   use_anthropic=backend;before=requests;s=glass_vision_analyze_clip(&clip,"What is visible?",&cancel);
   assert(s&&strstr(s,"cloud_temporal_samples")&&strstr(s,"a red cup")&&requests==before+1);free(s);
 }
 use_mimo=true;
 for(unsigned backend=0;backend<2;backend++){
   use_anthropic=backend;s=glass_vision_analyze_clip(&clip,"What is visible?",&cancel);
   assert(s&&strstr(s,"cloud_temporal_samples"));free(s);
 }
 use_anthropic=false;scenario=2;s=glass_vision_analyze_clip(&clip,"What is visible?",&cancel);
 assert(s&&strstr(s,"vision_output_limit"));free(s);scenario=0;use_mimo=false;
 before=requests;clip.frames[1].offset_ms=0;s=glass_vision_analyze_clip(&clip,"What is visible?",&cancel);
 assert(s&&strstr(s,"invalid_camera_samples")&&requests==before);free(s);clip.frames[1].offset_ms=2000;
 clip.frames[2].size=QPK_CLIP_FRAME_LIMIT+1;s=glass_vision_analyze_clip(&clip,"What is visible?",&cancel);
 assert(s&&strstr(s,"invalid_camera_samples")&&requests==before);free(s);clip.frames[2].size=sizeof(jpeg);
 for(unsigned backend=0;backend<2;backend++)for(unsigned n=1;n<140;n++){
   cJSON_Hooks hooks={fixture_allocate,free};cJSON_InitHooks(&hooks);allocation_count=0;fail_allocation=n;use_anthropic=backend;
   s=glass_vision_analyze_clip(&clip,"What is visible?",&cancel);free(s);fail_allocation=0;cJSON_InitHooks(NULL);
 }
 before=requests;atomic_store(&cancel,true);s=glass_vision_analyze_clip(&clip,"What is visible?",&cancel);assert(s&&strstr(s,"cancelled")&&requests==before);free(s);
 for(unsigned cancelled_case=0;cancelled_case<2;cancelled_case++)for(unsigned backend=0;backend<2;backend++){
   atomic_store(&cancel,cancelled_case);use_anthropic=backend;scenario=0;
   struct qpk_camera_clip owned={.count=3,.duration_ms=5000};
   for(unsigned i=0;i<3;i++){owned.frames[i]=(struct qpk_clip_frame){malloc(sizeof(jpeg)),sizeof(jpeg),i*2000};assert(owned.frames[i].jpeg);memcpy(owned.frames[i].jpeg,jpeg,sizeof(jpeg));}
   owned_request=&owned;before=requests;s=glass_vision_analyze_clip_take(&owned,"What is visible?",&cancel);
   assert(s&&strstr(s,cancelled_case?"cancelled":"a red cup")&&requests==before+!cancelled_case);
   for(unsigned i=0;i<3;i++)assert(!owned.frames[i].jpeg&&owned.frames[i].size==sizeof(jpeg));
   free(s);owned_request=NULL;
 }
 atomic_store(&cancel,false);
 for(unsigned backend=0;backend<2;backend++)for(unsigned failure=1;failure<140;failure++){
   struct qpk_camera_clip owned={.count=3,.duration_ms=5000};
   for(unsigned i=0;i<3;i++){owned.frames[i]=(struct qpk_clip_frame){malloc(sizeof(jpeg)),sizeof(jpeg),i*2000};assert(owned.frames[i].jpeg);memcpy(owned.frames[i].jpeg,jpeg,sizeof(jpeg));}
   cJSON_Hooks hooks={fixture_allocate,free};cJSON_InitHooks(&hooks);allocation_count=0;fail_allocation=failure;use_anthropic=backend;
   owned_request=&owned;s=glass_vision_analyze_clip_take(&owned,"What is visible?",&cancel);free(s);owned_request=NULL;
   fail_allocation=0;cJSON_InitHooks(NULL);
   for(unsigned i=0;i<3;i++)assert(!owned.frames[i].jpeg&&owned.frames[i].size==sizeof(jpeg));
 }
 puts("VISION_CLIP_OK: three timestamped JPEGs in one request, both provider adapters, sparse-video scope, malformed/timestamp/size/cancel rejection before network");
 puts("VISION_HOST_OK: OpenAI/Anthropic image payloads, byte-fragmented replies, failure/limit/redirect/cancel, no automatic retries or secret exposure");return 0;
}
