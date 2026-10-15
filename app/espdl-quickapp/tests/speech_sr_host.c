/* SPDX-License-Identifier: Apache-2.0 */
/* Production registry/state machine with mock inference; RV32 kernels are
 * checked separately by the complete board firmware link. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "glass_speech_sr.c"
struct model_iface_data_t {int kind;};
static struct model_iface_data_t wake={1},multi={2};
static int created,destroyed,cleaned,adds,wake_result;
static esp_mn_state_t multi_result;
static esp_mn_results_t results;
void *glass_speech_asset(const char *root,const char *name,size_t *size)
{(void)root;(void)name;*size=64;return calloc(1,65);}
static model_iface_data_t *create_wake(const void *name,det_mode_t mode)
{assert(!strcmp(name,"wn9_hilexin")&&mode==DET_MODE_90);assert(get_static_srmodels()->num==2);created++;return &wake;}
static model_iface_data_t *create_multi(const char *name,int ms)
{assert(!strcmp(name,"mn7_cn")&&ms==6000);created++;return &multi;}
static int chunk(model_iface_data_t *m){(void)m;return 512;}
static int rate(model_iface_data_t *m){(void)m;return 16000;}
static void destroy(model_iface_data_t *m){assert(m==&wake||m==&multi);destroyed++;}
static void clean(model_iface_data_t *m){assert(m);cleaned++;}
static wakenet_state_t detect_wake(model_iface_data_t *m,int16_t *pcm)
{assert(m==&wake&&pcm);return wake_result;}
static esp_mn_state_t detect_multi(model_iface_data_t *m,int16_t *pcm)
{assert(m==&multi&&pcm);return multi_result;}
static esp_mn_results_t *get_results(model_iface_data_t *m){assert(m==&multi);return &results;}
const esp_wn_iface_t esp_sr_wakenet9_quantized={.create=create_wake,.get_samp_chunksize=chunk,.get_samp_rate=rate,.detect=detect_wake,.clean=clean,.destroy=destroy};
const esp_mn_iface_t esp_sr_multinet7_quantized={.create=create_multi,.get_samp_chunksize=chunk,.get_samp_rate=rate,.detect=detect_multi,.get_results=get_results,.clean=clean,.destroy=destroy};
esp_err_t esp_mn_commands_alloc(const esp_mn_iface_t *m,model_iface_data_t *d){assert(m&&d);return 0;}
esp_err_t esp_mn_commands_free(void){return 0;}
esp_err_t esp_mn_commands_add(int id,const char *text){assert(id>0&&text[0]);for(const char *p=text;*p;p++)assert((*p>='a'&&*p<='z')||*p==' ');adds++;return 0;}
esp_mn_error_t *esp_mn_commands_update(void){return NULL;}
int main(void)
{
  struct speech_command commands[]={{1,"打开灯光","da kai deng guang"},{2,"关闭灯光","guan bi deng guang"}};
  struct glass_sr *s=NULL,*other=NULL;int16_t pcm[1024]={0};int command=99;
  assert(!glass_sr_create("offline",commands,2,&s)&&adds==2);
  assert(glass_sr_create("offline",commands,2,&other)==-EBUSY);
  assert(glass_sr_feed(s,pcm,511,&command)==0&&s->used==511&&command==0);
  wake_result=WAKENET_DETECTED;assert(glass_sr_feed(s,pcm,1,&command)==1&&s->window==96000);
  results.num=1;results.command_id[0]=2;results.prob[0]=.9;
  multi_result=ESP_MN_STATE_DETECTED;assert(glass_sr_feed(s,pcm,512,&command)==2&&command==2&&!s->window);
  assert(glass_sr_feed(s,pcm,512,&command)==1);
  results.command_id[0]=99;assert(glass_sr_feed(s,pcm,512,&command)==0&&command==0&&s->window);
  results.command_id[0]=2;
  results.prob[0]=.1;assert(glass_sr_feed(s,pcm,512,&command)==0&&command==0&&s->window);
  multi_result=ESP_MN_STATE_TIMEOUT;assert(glass_sr_feed(s,pcm,512,&command)==3&&!s->window);
  assert(glass_sr_feed(s,pcm,4097,&command)==-EINVAL);
  multi_result=ESP_MN_STATE_DETECTING;
  assert(glass_sr_feed(s,pcm,700,&command)==1&&s->used==188);
  wake_result=0;assert(glass_sr_feed(s,pcm,324,&command)==0&&s->used==0);
  glass_sr_destroy(s);assert(!speech_models&&created==destroyed&&cleaned>0);
  commands[1].id=1;assert(glass_sr_create("offline",commands,2,&s)==-EINVAL);
  assert(!speech_models&&created==destroyed);
  puts("SR_ADAPTER_HOST_OK: partial frames, wake window, command threshold, timeout, duplicate rejection and cleanup (mock inference)");
}
