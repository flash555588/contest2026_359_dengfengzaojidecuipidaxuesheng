/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_MUSIC_PV_ASSETS_H
#define GLASS_MUSIC_PV_ASSETS_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <cJSON.h>
#ifndef MUSIC_PV_STYLE_FILE
#define MUSIC_PV_STYLE_FILE "/sdcard/jizura/styles.json"
#endif
#define MUSIC_PV_STYLES_MAX 32
struct music_pv_colors {uint32_t background,foreground,secondary,accent;};
struct music_pv_style {char name[48];struct music_pv_colors light,dark;};
struct music_pv_assets {unsigned count;struct music_pv_style styles[MUSIC_PV_STYLES_MAX];};
static bool music_pv_asset_depth(const char *s)
{
  unsigned depth=0;bool quoted=false,escape=false;
  for(;*s;s++){
    if(quoted){if(escape)escape=false;else if(*s=='\\')escape=true;else if(*s=='"')quoted=false;continue;}
    if(*s=='"')quoted=true;
    else if(*s=='{'||*s=='['){if(++depth>6)return false;}
    else if(*s=='}'||*s==']'){if(!depth)return false;depth--;}
  }
  return !quoted&&!depth;
}
static bool music_pv_asset_color(const cJSON *object,const char *key,uint32_t *out)
{
  const cJSON *v=cJSON_GetObjectItemCaseSensitive(object,key);
  if(!cJSON_IsString(v)||!v->valuestring||strlen(v->valuestring)!=7||v->valuestring[0]!='#')return false;
  for(unsigned i=1;i<7;i++){char c=v->valuestring[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;}
  *out=(uint32_t)strtoul(v->valuestring+1,NULL,16);return true;
}
static bool music_pv_asset_colors(const cJSON *v,struct music_pv_colors *out)
{
  return cJSON_IsObject(v)&&music_pv_asset_color(v,"background",&out->background)&&
    music_pv_asset_color(v,"foreground",&out->foreground)&&music_pv_asset_color(v,"secondary",&out->secondary)&&music_pv_asset_color(v,"accent",&out->accent);
}
static bool music_pv_assets_load(struct music_pv_assets *out,const char *path)
{
  out->count=0;FILE *f=fopen(path,"rb");if(!f)return false;
  char *data=malloc(32769);if(!data){fclose(f);return false;}
  size_t n=fread(data,1,32769,f);bool read_ok=!ferror(f)&&n>0&&n<=32768;fclose(f);
  if(!read_ok||memchr(data,0,n)){free(data);return false;}data[n]=0;
  if(!music_pv_asset_depth(data)){free(data);return false;}
  cJSON *root=cJSON_ParseWithOpts(data,NULL,true);free(data);if(!root)return false;
  const cJSON *version=cJSON_GetObjectItemCaseSensitive(root,"version"),*styles=cJSON_GetObjectItemCaseSensitive(root,"styles");
  int count=cJSON_GetArraySize(styles);bool ok=cJSON_IsNumber(version)&&version->valuedouble==1&&cJSON_IsArray(styles)&&count>0&&count<=MUSIC_PV_STYLES_MAX;
  for(int i=0;ok&&i<count;i++){
    const cJSON *entry=cJSON_GetArrayItem(styles,i),*name=cJSON_GetObjectItemCaseSensitive(entry,"name");
    struct music_pv_style *s=&out->styles[i];
    ok=cJSON_IsString(name)&&name->valuestring&&name->valuestring[0]&&strlen(name->valuestring)<sizeof(s->name);
    if(ok){
      for(const unsigned char *p=(const unsigned char *)name->valuestring;*p;p++)if(*p<32||*p==127)ok=false;
      if(ok)snprintf(s->name,sizeof(s->name),"%s",name->valuestring);
      ok=ok&&music_pv_asset_colors(cJSON_GetObjectItemCaseSensitive(entry,"light"),&s->light)&&music_pv_asset_colors(cJSON_GetObjectItemCaseSensitive(entry,"dark"),&s->dark);
    }
  }
  cJSON_Delete(root);if(ok)out->count=(unsigned)count;return ok;
}
#endif
