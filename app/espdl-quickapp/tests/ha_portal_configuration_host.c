/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_portal.h"
#include "hass_service.h"
#include "hass_transport.h"
#include "qpk_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char saved[8193], wire_url[128], wire_token[512];
static bool write_fail, wire_busy;
static unsigned writes;
int qpk_storage_read(const char *root,const char *package,const char *key,
                     char *data,size_t capacity,size_t *length)
{
  (void)root;(void)package;
  if(strcmp(key,"homeassistant")||!*saved) return -ENOENT;
  assert(strlen(saved)<capacity);strcpy(data,saved);*length=strlen(saved);return 0;
}
int qpk_storage_write(const char *root,const char *package,const char *key,
                      const char *data,size_t size)
{
  (void)root;(void)package;assert(!strcmp(key,"homeassistant"));
  if(write_fail) return -ENOSPC;
  assert(size<sizeof(saved));memcpy(saved,data,size);saved[size]=0;writes++;return 0;
}
int hass_transport_get(const char *url,const char *token,const char *resource)
{
  assert(resource);strcpy(wire_url,url);strcpy(wire_token,token);wire_busy=true;return 0;
}
int hass_transport_get_state(const char *u,const char *t,const char *r)
{return hass_transport_get(u,t,r);}
int hass_transport_call_service(const char *u,const char *t,const char *d,
                                const char *s,const char *b)
{(void)d;(void)b;return hass_transport_get(u,t,s);}
void hass_transport_poll(struct hass_transport_result_s *out)
{memset(out,0,sizeof(*out));out->busy=wire_busy;}
void hass_transport_stop(void){wire_busy=false;}
bool hass_transport_busy(void){return wire_busy;}

static int patch(const char *json)
{cJSON *o=cJSON_Parse(json);assert(o);int ret=portal_config_save("homeassistant",o);cJSON_Delete(o);return ret;}
static void check(int client,const char *url,const char *token)
{
  assert(hass_get(client,"config")>0);
  assert(!strcmp(wire_url,url)&&!strcmp(wire_token,token));
  assert(hass_close(client)==0);
}
int main(void)
{
  assert(patch("{\"url\":\"http://old.local:8123\",\"token\":\"old-token\"}")==0);
  uint32_t revision=hass_configuration_revision();unsigned before=writes;
  int ui=hass_open(HASS_READ|HASS_CONTROL);assert(ui>0);
  assert(hass_get(ui,"states")>0);
  assert(patch("{\"url\":\"http://124.223.30.246/8123\"}")==-EINVAL);
  assert(writes==before&&hass_configuration_revision()==revision);
  assert(patch("{\"url\":\"https://new.local:8123\"}")==-EINVAL);
  assert(patch("{\"url\":\"http://new.local:0\"}")==-EINVAL);
  assert(patch("{\"token\":\"bad token\"}")==-EINVAL);
  char large[600];memset(large,'a',sizeof(large));large[sizeof(large)-1]=0;
  assert(portal_ha_save("http://new.local:8123",large)==-EINVAL);
  write_fail=true;assert(patch("{\"url\":\"http://new.local:8123\"}")==-ENOSPC);
  assert(hass_configuration_revision()==revision);write_fail=false;
  assert(patch("{\"url\":\"http://124.223.30.246:8123\"}")==0);
  assert(hass_configuration_revision()==++revision);
  struct hass_result_s result;assert(hass_poll(ui,&result)==0);
  assert(result.done&&result.error==-ECANCELED);hass_result_free(&result);
  check(ui,"http://124.223.30.246:8123","old-token");
  assert(patch("{\"token\":\"replacement-token\"}")==0);
  assert(hass_configuration_revision()==++revision);
  assert(portal_ha_apply()==0&&hass_configuration_revision()==revision);
  char url[128],token[512];assert(portal_ha_load(url,sizeof(url),token,sizeof(token))==0);
  assert(!strcmp(url,"http://124.223.30.246:8123")&&!strcmp(token,"replacement-token"));
  cJSON *public=portal_config_get("homeassistant");
  assert(!cJSON_GetObjectItemCaseSensitive(public,"token"));
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(public,"token_set")));cJSON_Delete(public);
  check(hass_open(HASS_READ),url,token);
  assert(patch("{\"token\":\"\"}")==0);
  bool configured,busy;hass_status(&configured,&busy,NULL);assert(!configured&&!busy);
  assert(patch("{\"token\":\"restored-token\"}")==0);
  assert(portal_ha_apply()==0);
  check(hass_open(HASS_READ),url,"restored-token");
  puts("HA portal tests passed: validation, failed-save preservation, immediate URL/token apply, cancellation, bootstrap, secret redaction");
}
