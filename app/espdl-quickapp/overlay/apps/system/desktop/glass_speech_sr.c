/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_speech_sr.h"
#include "glass_speech_assets.h"
#include "esp_wn_iface.h"
#include "esp_mn_iface.h"
#include "esp_mn_speech_commands.h"
#define ESP_PLATFORM 1
#include "model_path.h"
#undef ESP_PLATFORM
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if UINTPTR_MAX==UINT32_MAX
_Static_assert(offsetof(srmodel_list_t,model_data)==20&&sizeof(srmodel_list_t)==24,"Pinned ESP-SR registry ABI");
#endif
/* Direct handles avoid pulling unselected WakeNet10/English model families. */
extern const esp_wn_iface_t esp_sr_wakenet9_quantized;
extern const esp_mn_iface_t esp_sr_multinet7_quantized;
static srmodel_list_t *speech_models;
srmodel_list_t *get_static_srmodels(void){return speech_models;}
char *get_model_base_path(void){return "/sdcard/speech/esp-sr";}
struct glass_sr {
  srmodel_list_t models;
  srmodel_data_t data[2];srmodel_data_t *data_ptr[2];
  char *names[2],*infos[2];
  char *files[2][3],*blobs[2][3];int sizes[2][3];
  model_iface_data_t *wake,*multi;
  int16_t *buffer;unsigned chunk,used,window;
  bool commands;
  int command_ids[GLASS_SPEECH_MAX_COMMANDS];size_t command_count;
};

void glass_sr_destroy(struct glass_sr *s)
{
  if(!s)return;
  if(s->commands)esp_mn_commands_free();
  if(s->multi)esp_sr_multinet7_quantized.destroy(s->multi);
  if(s->wake)esp_sr_wakenet9_quantized.destroy(s->wake);
  if(speech_models==&s->models)speech_models=NULL;
  for(unsigned i=0;i<2;i++){
    free(s->infos[i]);for(unsigned j=0;j<3;j++)free(s->blobs[i][j]);}
  free(s->buffer);free(s);
}
int glass_sr_create(const char *root,const struct speech_command *commands,size_t count,struct glass_sr **out)
{
  if(!out||!root||!commands||!count||count>GLASS_SPEECH_MAX_COMMANDS)return -EINVAL;
  *out=NULL;if(speech_models)return -EBUSY;
  struct glass_sr *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
  int ret=-EBADMSG;
  s->names[0]="wn9_hilexin";s->names[1]="mn7_cn";
  const char *files[2][3]={{"wn9_index","wn9_data",NULL},{"mn7_index","mn7_data","vocab"}};
  s->models.model_name=s->names;s->models.model_info=s->infos;
  s->models.model_data=s->data_ptr;s->models.num=2;
  for(unsigned i=0;i<2;i++){
    char relative[128];size_t size;
    snprintf(relative,sizeof(relative),"esp-sr/%s/_MODEL_INFO_",s->names[i]);
    s->infos[i]=glass_speech_asset(root,relative,&size);if(!s->infos[i])goto fail;
    s->data_ptr[i]=&s->data[i];s->data[i].files=s->files[i];s->data[i].data=s->blobs[i];s->data[i].sizes=s->sizes[i];
    for(unsigned j=0;j<3&&files[i][j];j++){
      s->files[i][j]=(char *)files[i][j];snprintf(relative,sizeof(relative),"esp-sr/%s/%s",s->names[i],files[i][j]);
      s->blobs[i][j]=glass_speech_asset(root,relative,&size);if(!s->blobs[i][j])goto fail;
      s->sizes[i][j]=size;s->data[i].num++;
    }
  }
  speech_models=&s->models;
  s->wake=esp_sr_wakenet9_quantized.create(s->names[0],DET_MODE_90);
  if(!s->wake){ret=-ENOMEM;goto fail;}
  s->multi=esp_sr_multinet7_quantized.create(s->names[1],6000);
  if(!s->multi){ret=-ENOMEM;goto fail;}
  ret=-EBADMSG;
  int chunk=esp_sr_wakenet9_quantized.get_samp_chunksize(s->wake);
  if(chunk<=0||chunk>2048||chunk!=esp_sr_multinet7_quantized.get_samp_chunksize(s->multi)||
    esp_sr_wakenet9_quantized.get_samp_rate(s->wake)!=16000||esp_sr_multinet7_quantized.get_samp_rate(s->multi)!=16000)goto fail;
  s->chunk=chunk;s->buffer=calloc(chunk,sizeof(int16_t));if(!s->buffer){ret=-ENOMEM;goto fail;}
  if(esp_mn_commands_alloc(&esp_sr_multinet7_quantized,s->multi))goto fail;
  s->commands=true;
  for(size_t i=0;i<count;i++){
    if(commands[i].id<=0||!memchr(commands[i].text,0,sizeof(commands[i].text))||!commands[i].text[0]||
      !memchr(commands[i].pinyin,0,sizeof(commands[i].pinyin))||!commands[i].pinyin[0]){ret=-EINVAL;goto fail;}
    for(const char *p=commands[i].pinyin;*p;p++)if((*p<'a'||*p>'z')&&*p!=' '){ret=-EINVAL;goto fail;}
    for(size_t j=0;j<i;j++)if(commands[i].id==commands[j].id||!strcmp(commands[i].text,commands[j].text)||!strcmp(commands[i].pinyin,commands[j].pinyin)){ret=-EINVAL;goto fail;}
    if(esp_mn_commands_add(commands[i].id,commands[i].pinyin))goto fail;
    s->command_ids[s->command_count++]=commands[i].id;
  }
  esp_mn_error_t *error=esp_mn_commands_update();if(error&&error->num)goto fail;
  *out=s;return 0;
fail:glass_sr_destroy(s);return ret;
}
int glass_sr_feed(struct glass_sr *s,const int16_t *pcm,size_t frames,int *command)
{
  if(!s||!pcm||!command||frames>4096)return -EINVAL;
  *command=0;int event=0;
  while(frames){
    unsigned n=s->chunk-s->used;if(n>frames)n=frames;
    memcpy(s->buffer+s->used,pcm,n*sizeof(*pcm));s->used+=n;pcm+=n;frames-=n;
    if(s->used<s->chunk)continue;s->used=0;
    if(!s->window){
      if(esp_sr_wakenet9_quantized.detect(s->wake,s->buffer)>0){
        s->window=16000*6;esp_sr_multinet7_quantized.clean(s->multi);if(event!=2)event=1;
      }
    }else{
      s->window=s->window>s->chunk?s->window-s->chunk:0;
      esp_mn_state_t result=esp_sr_multinet7_quantized.detect(s->multi,s->buffer);
      if(result==ESP_MN_STATE_DETECTED){
        esp_mn_results_t *r=esp_sr_multinet7_quantized.get_results(s->multi);
        if(r&&r->num>0&&r->num<=ESP_MN_RESULT_MAX_NUM&&r->prob[0]>=0.6f){
          for(size_t i=0;i<s->command_count;i++)if(r->command_id[0]==s->command_ids[i]){
            *command=r->command_id[0];event=2;s->window=0;break;
          }
        }
      }else if(result==ESP_MN_STATE_TIMEOUT)s->window=0;
      if(!s->window){if(event!=2)event=3;esp_sr_multinet7_quantized.clean(s->multi);esp_sr_wakenet9_quantized.clean(s->wake);}
    }
    /* Consume every supplied sample, including a partial frame after an event. */
  }
  return event;
}
