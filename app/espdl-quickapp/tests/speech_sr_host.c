/* SPDX-License-Identifier: Apache-2.0 */
/* Production registry/state machine with mock inference; RV32 kernels are
 * checked separately by the complete board firmware link. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "glass_speech_sr.c"
struct model_iface_data_t {int kind;};
static struct model_iface_data_t multi={2};
struct glass_custom_wake {int unused;};
static struct glass_custom_wake wake;
static int created,destroyed,cleaned,adds,wake_result;
static esp_mn_state_t multi_result;
static esp_mn_results_t results;
void *glass_speech_asset(const char *root,const char *name,size_t *size)
{(void)root;(void)name;*size=64;return calloc(1,65);}
int glass_custom_wake_create(const char *root,struct glass_custom_wake **out)
{assert(root&&get_static_srmodels()->num==1);created++;*out=&wake;return 0;}
void glass_custom_wake_destroy(struct glass_custom_wake *s){if(s){assert(s==&wake);destroyed++;}}
void glass_custom_wake_reset(struct glass_custom_wake *s){assert(s==&wake);cleaned++;}
int glass_custom_wake_feed(struct glass_custom_wake *s,const int16_t *pcm,size_t n)
{assert(s==&wake&&pcm&&n==512);return wake_result;}
unsigned glass_custom_wake_evaluations(const struct glass_custom_wake *s){assert(s==&wake);return 1;}
float glass_custom_wake_score(const struct glass_custom_wake *s){assert(s==&wake);return .9f;}
static model_iface_data_t *create_multi(const char *name,int ms)
{assert(!strcmp(name,"mn7_cn")&&ms==6000);created++;return &multi;}
static int chunk(model_iface_data_t *m){(void)m;return 512;}
static int rate(model_iface_data_t *m){(void)m;return 16000;}
static void destroy(model_iface_data_t *m){assert(m==&multi);destroyed++;}
static void clean(model_iface_data_t *m){assert(m);cleaned++;}
static esp_mn_state_t detect_multi(model_iface_data_t *m,int16_t *pcm)
{assert(m==&multi&&pcm);return multi_result;}
static esp_mn_results_t *get_results(model_iface_data_t *m){assert(m==&multi);return &results;}
const esp_mn_iface_t esp_sr_multinet7_quantized={.create=create_multi,.get_samp_chunksize=chunk,.get_samp_rate=rate,.detect=detect_multi,.get_results=get_results,.clean=clean,.destroy=destroy};
esp_err_t esp_mn_commands_alloc(const esp_mn_iface_t *m,model_iface_data_t *d){assert(m&&d);return 0;}
esp_err_t esp_mn_commands_free(void){return 0;}
esp_err_t esp_mn_commands_add(int id,const char *text){assert(id>0&&text[0]);for(const char *p=text;*p;p++)assert((*p>='a'&&*p<='z')||*p==' ');adds++;return 0;}
esp_mn_error_t *esp_mn_commands_update(void){return NULL;}
int main(int argc,char **argv)
{
  assert(argc==1||argc==2);
  if(argc==2){
    FILE *canonical=fopen(argv[1],"rb");assert(canonical);
    char bytes[sizeof(glass_speech_bootstrap_commands)];
    size_t got=fread(bytes,1,sizeof(bytes),canonical);
    assert(!ferror(canonical)&&feof(canonical));fclose(canonical);
    assert(got==sizeof(glass_speech_bootstrap_commands)-1);
    assert(!memcmp(bytes,glass_speech_bootstrap_commands,got));
  }
  struct speech_command commands[]={{1,"打开灯光","da kai deng guang"},{2,"关闭灯光","guan bi deng guang"}};
  struct glass_sr *s=NULL,*other=NULL;int16_t pcm[1024]={0};int command=99;
  char root[]="/tmp/sr-inputs-XXXXXX";assert(mkdtemp(root));
  char base[320],fst[320],model[320],command_path[320],vocab[320];
  snprintf(base,sizeof(base),"%s/esp-sr",root);assert(!mkdir(base,0700));
  snprintf(fst,sizeof(fst),"%s/fst",base);assert(!mkdir(fst,0700));
  snprintf(model,sizeof(model),"%s/mn7_cn",base);assert(!mkdir(model,0700));
  snprintf(command_path,sizeof(command_path),"%s/commands_cn.txt",fst);
  snprintf(vocab,sizeof(vocab),"%s/vocab",model);
  assert(glass_sr_create(root,commands,2,&s)==-ENOENT&&!s&&!created);
  FILE *f=fopen(command_path,"w");assert(f);fclose(f);
  assert(glass_sr_create(root,commands,2,&s)==-EBADMSG&&!s&&!created);
  f=fopen(command_path,"w");assert(f);fputs("test fixture",f);fclose(f);
  assert(glass_sr_create(root,commands,2,&s)==-EBADMSG&&!s&&!created);
  f=fopen(command_path,"w");assert(f);fwrite(glass_speech_bootstrap_commands,1,sizeof(glass_speech_bootstrap_commands)-1,f);fclose(f);
  assert(glass_sr_create(root,commands,2,&s)==-ENOENT&&!s&&!created);
  f=fopen(vocab,"w");assert(f);fputs("fixture",f);fclose(f);
  f=fopen(command_path,"r+");assert(f);fputc('x',f);fclose(f);
  assert(glass_sr_create(root,commands,2,&s)==-EBADMSG&&!s&&!created);
  f=fopen(command_path,"w");assert(f);fwrite(glass_speech_bootstrap_commands,1,sizeof(glass_speech_bootstrap_commands)-1,f);fclose(f);
  assert(!glass_sr_create(root,commands,2,&s)&&adds==2);
  assert(!strcmp(get_model_base_path(),base));
  assert(glass_sr_create(root,commands,2,&other)==-EBUSY);
  assert(glass_sr_feed(s,pcm,511,&command)==0&&s->used==511&&command==0);
  wake_result=1;assert(glass_sr_feed(s,pcm,1,&command)==1&&s->window==96000);
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
  commands[1].id=1;assert(glass_sr_create(root,commands,2,&s)==-EINVAL);
  assert(!speech_models&&created==destroyed);
  unlink(command_path);unlink(vocab);rmdir(model);rmdir(fst);rmdir(base);rmdir(root);
  puts("SR_ADAPTER_HOST_OK: missing/empty/malformed grammar rejected before inference; partial frames, wake window, command threshold, timeout, duplicate rejection and cleanup (mock inference)");
}
