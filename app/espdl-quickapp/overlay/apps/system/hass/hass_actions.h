/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cJSON.h"
#include <math.h>
#include <string.h>

/* Single-entity, typed actions. Never accept extra targets, arbitrary routes,
 * nested payloads or parameters from a different service. */
struct hass_action_rule_s
{
  const char *domain, *service, *key;
  char type; /* n = number, s = bounded string, b = bool, c = RGB, 0 = none */
  double minimum, maximum;
};

static const struct hass_action_rule_s hass_action_rules[] = {
  {"light","turn_on","brightness_pct",'n',1,100},
  {"light","turn_on","color_temp_kelvin",'n',1000,20000},
  {"light","turn_on","rgb_color",'c',0,255},
  {"light","turn_on","effect",'s',0,128},
  {"light","turn_off",NULL,0,0,0},
  {"switch","turn_on",NULL,0,0,0},{"switch","turn_off",NULL,0,0,0},
  {"input_boolean","turn_on",NULL,0,0,0},{"input_boolean","turn_off",NULL,0,0,0},
  {"scene","turn_on",NULL,0,0,0},{"script","turn_on",NULL,0,0,0},
  {"fan","turn_on",NULL,0,0,0},{"fan","turn_off",NULL,0,0,0},
  {"fan","set_percentage","percentage",'n',0,100},
  {"fan","set_preset_mode","preset_mode",'s',0,128},
  {"fan","oscillate","oscillating",'b',0,0},
  {"fan","set_direction","direction",'s',0,128},
  {"number","set_value","value",'n',-1e9,1e9},
  {"input_number","set_value","value",'n',-1e9,1e9},
  {"select","select_option","option",'s',0,128},
  {"input_select","select_option","option",'s',0,128},
  {"text","set_value","value",'s',0,128},
  {"input_text","set_value","value",'s',0,128},
  {"button","press",NULL,0,0,0},{"input_button","press",NULL,0,0,0},
  {"cover","open_cover",NULL,0,0,0},{"cover","close_cover",NULL,0,0,0},
  {"cover","stop_cover",NULL,0,0,0},
  {"cover","set_cover_position","position",'n',0,100},
  {"cover","open_cover_tilt",NULL,0,0,0},{"cover","close_cover_tilt",NULL,0,0,0},
  {"cover","stop_cover_tilt",NULL,0,0,0},
  {"cover","set_cover_tilt_position","tilt_position",'n',0,100},
  {"climate","turn_on",NULL,0,0,0},{"climate","turn_off",NULL,0,0,0},
  {"climate","set_temperature","temperature",'n',-100,200},
  {"climate","set_temperature","target_temp_low",'n',-100,200},
  {"climate","set_temperature","target_temp_high",'n',-100,200},
  {"climate","set_humidity","humidity",'n',0,100},
  {"climate","set_hvac_mode","hvac_mode",'s',0,128},
  {"climate","set_fan_mode","fan_mode",'s',0,128},
  {"climate","set_preset_mode","preset_mode",'s',0,128},
  {"climate","set_swing_mode","swing_mode",'s',0,128},
  {"climate","set_swing_horizontal_mode","swing_horizontal_mode",'s',0,128},
  {"humidifier","turn_on",NULL,0,0,0},{"humidifier","turn_off",NULL,0,0,0},
  {"humidifier","set_humidity","humidity",'n',0,100},
  {"humidifier","set_mode","mode",'s',0,128},
  {"media_player","turn_on",NULL,0,0,0},{"media_player","turn_off",NULL,0,0,0},
  {"media_player","volume_set","volume_level",'n',0,1},
  {"media_player","volume_mute","is_volume_muted",'b',0,0},
  {"media_player","media_play",NULL,0,0,0},{"media_player","media_pause",NULL,0,0,0},
  {"media_player","media_stop",NULL,0,0,0},{"media_player","media_next_track",NULL,0,0,0},
  {"media_player","media_previous_track",NULL,0,0,0},
  {"media_player","select_source","source",'s',0,128},
  {"vacuum","start",NULL,0,0,0},{"vacuum","pause",NULL,0,0,0},
  {"vacuum","stop",NULL,0,0,0},{"vacuum","return_to_base",NULL,0,0,0},
  {"vacuum","set_fan_speed","fan_speed",'s',0,128},
  {"water_heater","set_temperature","temperature",'n',0,200},
  {"water_heater","set_operation_mode","operation_mode",'s',0,128},
  {"water_heater","turn_on",NULL,0,0,0},{"water_heater","turn_off",NULL,0,0,0},
  {"water_heater","set_away_mode","away_mode",'b',0,0},
  {"lock","lock",NULL,0,0,0},{"lock","unlock",NULL,0,0,0}
};

static bool hass_action_valid(const char *domain, const char *service,
                              const char *entity, const char *body)
{
  if (!service || !body || strlen(body) > 1023) return false;
  const char *end = NULL;
  cJSON *root = cJSON_ParseWithOpts(body, &end, true);
  if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
  bool known = false, valid = true;
  unsigned targets = 0, params = 0, colors = 0;
  for (unsigned i = 0; i < sizeof(hass_action_rules)/sizeof(hass_action_rules[0]); i++)
    if (!strcmp(domain,hass_action_rules[i].domain) &&
        !strcmp(service,hass_action_rules[i].service)) known = true;
  const cJSON *field;
  cJSON_ArrayForEach(field,root)
    {
      if (!field->string) {valid = false; break;}
      if (!strcmp(field->string,"entity_id"))
        {targets++; valid &= cJSON_IsString(field) && !strcmp(field->valuestring,entity); continue;}
      bool matched = false;
      for (unsigned i = 0; i < sizeof(hass_action_rules)/sizeof(hass_action_rules[0]); i++)
        {
          const struct hass_action_rule_s *r = &hass_action_rules[i];
          if (strcmp(domain,r->domain) || strcmp(service,r->service) || !r->key || strcmp(field->string,r->key)) continue;
          matched = r->type=='n' ? cJSON_IsNumber(field) && isfinite(field->valuedouble) && field->valuedouble>=r->minimum && field->valuedouble<=r->maximum :
                    /* Frontend limit is 128 characters; UTF-8 needs up to four
                     * bytes per character. The complete body remains bounded. */
                    r->type=='s' ? cJSON_IsString(field) && strlen(field->valuestring)>0 && strlen(field->valuestring)<=4*r->maximum :
                    r->type=='c' ? cJSON_IsArray(field) && cJSON_GetArraySize(field)==3 : cJSON_IsBool(field);
          if (r->type=='c')
            {
              const cJSON *channel;
              cJSON_ArrayForEach(channel,field)
                matched &= cJSON_IsNumber(channel) && isfinite(channel->valuedouble) &&
                  channel->valuedouble>=0 && channel->valuedouble<=255 &&
                  channel->valuedouble==(int)channel->valuedouble;
            }
          if (!strcmp(r->key,"rgb_color") || !strcmp(r->key,"color_temp_kelvin")) colors++;
          /* Duplicate parameters cannot override a validated earlier value. */
          matched &= cJSON_GetObjectItemCaseSensitive(root,field->string)==field;
          break;
        }
      params++; valid &= matched;
    }
  valid &= known && targets==1 && params<=3 && colors<=1;
  if(!strcmp(domain,"fan")&&!strcmp(service,"set_direction")){
    const cJSON *direction=cJSON_GetObjectItemCaseSensitive(root,"direction");
    valid &= cJSON_IsString(direction)&&(!strcmp(direction->valuestring,"forward")||!strcmp(direction->valuestring,"reverse"));
  }
  if(!strcmp(domain,"climate")&&!strcmp(service,"set_temperature")){
    const cJSON *single=cJSON_GetObjectItemCaseSensitive(root,"temperature");
    const cJSON *low=cJSON_GetObjectItemCaseSensitive(root,"target_temp_low");
    const cJSON *high=cJSON_GetObjectItemCaseSensitive(root,"target_temp_high");
    valid &= single?(!low&&!high):(cJSON_IsNumber(low)&&cJSON_IsNumber(high)&&low->valuedouble<=high->valuedouble);
  }
  /* Parameterized services require their argument; plain turn_on is valid. */
  if (!params && strcmp(service,"turn_on"))
    {
      bool empty = false;
      for (unsigned i=0;i<sizeof(hass_action_rules)/sizeof(hass_action_rules[0]);i++)
        if (!strcmp(domain,hass_action_rules[i].domain) && !strcmp(service,hass_action_rules[i].service) && !hass_action_rules[i].key) empty=true;
      valid &= empty;
    }
  cJSON_Delete(root);
  return valid;
}
