/* SPDX-License-Identifier: Apache-2.0 */
#include "hass_service.h"
#include "hass_transport.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static bool transport_busy;
static unsigned stops;
static bool slow_stop;
static char last_action[64],last_data[1024];

int hass_transport_get(const char *url, const char *token,
                       const char *resource)
{
  assert(url && token && resource);
  if (transport_busy) return -EBUSY;
  transport_busy = true;
  return 0;
}

int hass_transport_get_state(const char *url, const char *token,
                             const char *entity)
{
  return hass_transport_get(url, token, entity);
}

int hass_transport_call_service(const char *url, const char *token,
                                const char *domain, const char *service,
                                const char *data)
{
  assert(domain && data);
  snprintf(last_action,sizeof(last_action),"%s",service);
  snprintf(last_data,sizeof(last_data),"%s",data);
  return hass_transport_get(url, token, service);
}

void hass_transport_poll(struct hass_transport_result_s *result)
{
  memset(result, 0, sizeof(*result));
  result->busy = transport_busy;
}
bool hass_transport_busy(void) { return transport_busy; }

void hass_transport_stop(void)
{
  if (!slow_stop) transport_busy = false;
  stops++;
}

int main(void)
{
  int action_admin=hass_open(HASS_READ|HASS_CONTROL|HASS_CONFIGURE);
  assert(hass_configure(action_admin,"http://actions.local:8123","host-token",true)==0);
  int reader=hass_open(HASS_READ);
  assert(hass_call_service(reader,"light.desk","turn_on","{\"entity_id\":\"light.desk\",\"color_temp_kelvin\":4600}")==-EACCES);
  assert(hass_close(reader)==0);
  const char *invalid[]={
    "{\"entity_id\":\"light.other\",\"color_temp_kelvin\":4600}",
    "{\"entity_id\":[\"light.desk\"],\"color_temp_kelvin\":4600}",
    "{\"entity_id\":\"light.desk\",\"color_temp_kelvin\":999}",
    "{\"entity_id\":\"light.desk\",\"color_temp_kelvin\":\"4600\"}",
    "{\"entity_id\":\"light.desk\",\"area_id\":\"all\"}",
    "{\"entity_id\":\"light.desk\",\"color_temp_kelvin\":4600,\"color_temp_kelvin\":5000}",
    "{\"entity_id\":\"light.desk\",\"entity_id\":\"light.desk\"}",
    "{\"entity_id\":\"light.desk\"}trailing"};
  for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
    assert(hass_call_service(action_admin,"light.desk","turn_on",invalid[i])==-EINVAL);
  assert(hass_call_service(action_admin,"light.desk","../restart","{\"entity_id\":\"light.desk\"}")==-EINVAL);
  assert(hass_call_service(action_admin,"sensor.temp","turn_on","{\"entity_id\":\"sensor.temp\"}")==-EINVAL);
  assert(hass_call_service(action_admin,"number.limit","set_value","{\"entity_id\":\"number.limit\"}")==-EINVAL);
    assert(hass_call_service(action_admin,"scene.evening","turn_off","{\"entity_id\":\"scene.evening\"}")==-EINVAL);
    assert(hass_call_service(action_admin,"scene.evening","turn_on","{\"entity_id\":\"scene.evening\",\"brightness_pct\":50}")==-EINVAL);
    assert(hass_call_service(action_admin,"script.sleep","turn_on","{\"entity_id\":\"script.other\"}")==-EINVAL);
  assert(hass_close(action_admin)==0);
    const char *entities[]={"light.desk","number.limit","select.mode","button.test","fan.air","climate.room","cover.blind","humidifier.room","media_player.room","vacuum.bot","water_heater.tank","lock.door","text.label","scene.evening","script.sleep"};
    const char *actions[]={"turn_on","set_value","select_option","press","oscillate","set_temperature","set_cover_position","set_humidity","volume_set","return_to_base","set_operation_mode","lock","set_value","turn_on","turn_on"};
    const char *args[]={",\"color_temp_kelvin\":4600",",\"value\":7.5",",\"option\":\"安静\"","",",\"oscillating\":true",",\"temperature\":24.5",",\"position\":60",",\"humidity\":55",",\"volume_level\":0.5","",",\"operation_mode\":\"eco\"","",",\"value\":\"hello\"","",""};
  for(unsigned i=0;i<sizeof(entities)/sizeof(entities[0]);i++){
    int c=hass_open(HASS_READ|HASS_CONTROL);char body[256];
    snprintf(body,sizeof(body),"{\"entity_id\":\"%s\"%s}",entities[i],args[i]);
    assert(hass_call_service(c,entities[i],actions[i],body)>0);
    assert(!strcmp(last_action,actions[i])&&!strcmp(last_data,body));
    assert(hass_close(c)==0);
  }
  puts("HA_TYPED_ACTIONS_OK: 15 domains, bounds, grants, exact targets, duplicate rejection");
  {
    int c=hass_open(HASS_READ|HASS_CONTROL);char text_body[800];
    strcpy(text_body,"{\"entity_id\":\"select.mode\",\"option\":\"");
    for(unsigned i=0;i<128;i++)strcat(text_body,"暖");
    strcat(text_body,"\"}");
    assert(hass_call_service(c,"select.mode","select_option",text_body)>0);
    assert(hass_close(c)==0);
  }
  stops=0;
  int portal = hass_open(HASS_READ | HASS_CONTROL | HASS_CONFIGURE);
  int ui = hass_open(HASS_READ | HASS_CONTROL);
  assert(portal > 0 && ui > 0);
  uint32_t revision = hass_configuration_revision();
  assert(hass_configure(portal, "http://old.local:8123", "old-token", true) == 0);
  assert(hass_configuration_revision() == ++revision);
  int request = hass_get(ui, "states");
  assert(request > 0);
  assert(hass_configure(portal, "http://new.example.com:8123", "new-token", true) == 0);
  assert(stops == 1);
  assert(hass_configuration_revision() == ++revision);
  struct hass_result_s result;
  assert(hass_poll(ui, &result) == 0);
  assert(result.done && result.error == -ECANCELED);
  assert(result.request_id == (uint32_t)request);
  hass_result_free(&result);
  char url[128];
  assert(hass_get_url(ui, url, sizeof(url)) == 0);
  assert(strcmp(url, "http://new.example.com:8123") == 0);
  assert(hass_configure(portal, url, "replacement-token", true) == 0);
  assert(hass_configuration_revision() == ++revision);
  assert(hass_configure(ui, url, "forbidden", true) == -EACCES);
  assert(hass_configuration_revision() == revision);
  assert(hass_control(ui, "light.desk", false, -1) > 0);
  assert(hass_clear_configuration(portal) == 0);
  assert(hass_configuration_revision() == ++revision);
  bool configured, busy;
  hass_status(&configured, &busy, NULL);
  assert(!configured && !busy);
  assert(hass_get(ui, "states") == -ENOTCONN);
  assert(hass_configure(portal, url, "restored-token", true) == 0);
  assert(hass_get(ui, "states") > 0);
  assert(hass_close(ui) == 0);
  assert(hass_close(portal) == 0);
  /* A closed session's worker, and a worker cancelled by configuration,
   * still occupy the shared transport until cancellation actually finishes. */
  portal=hass_open(HASS_READ|HASS_CONFIGURE);
  ui=hass_open(HASS_READ|HASS_CONTROL);
  assert(hass_configure(portal,"http://old.local:8123","old-token",true)==0);
  assert(hass_get(ui,"states")>0);
  assert(hass_poll(portal,&result)==0 && result.busy && !result.done);
  slow_stop=true;assert(hass_close(ui)==0);
  ui=hass_open(HASS_READ|HASS_CONTROL);
  assert(hass_poll(ui,&result)==0 && result.busy);
  assert(hass_configure(portal,"http://new.local:8123","new-token",true)==0);
  hass_status(&configured,&busy,NULL);assert(configured&&busy);
  assert(hass_poll(ui,&result)==0 && result.busy);
  assert(hass_get(ui,"states")==-EBUSY);
  transport_busy=false;slow_stop=false;
  hass_status(&configured,&busy,NULL);assert(configured&&!busy);
  assert(hass_get(ui,"states")>0);
  assert(hass_close(ui)==0);assert(hass_close(portal)==0);
  puts("HA configuration host tests passed");
  return 0;
}
