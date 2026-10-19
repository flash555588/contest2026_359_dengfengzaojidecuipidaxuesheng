/* SPDX-License-Identifier: Apache-2.0 */
/* Bounded HA capabilities for AI discovery and fresh-state preflight.
 * Never export arbitrary attributes, URLs, templates or device credentials. */
static unsigned home_action_feature(const char *domain,const char *service,const char *key)
{
  if(!strcmp(domain,"fan")){
    if(!strcmp(service,"turn_on"))return 32;
    if(!strcmp(service,"turn_off"))return 16;
    if(!strcmp(service,"set_percentage"))return 1;
    if(!strcmp(service,"oscillate"))return 2;
    if(!strcmp(service,"set_direction"))return 4;
    if(!strcmp(service,"set_preset_mode"))return 8;
  }
  if(!strcmp(domain,"climate")){
    if(!strcmp(service,"turn_on"))return 256;
    if(!strcmp(service,"turn_off"))return 128;
    if(!strcmp(service,"set_temperature"))return key&&!strcmp(key,"temperature")?1:2;
    if(!strcmp(service,"set_humidity"))return 4;
    if(!strcmp(service,"set_fan_mode"))return 8;
    if(!strcmp(service,"set_preset_mode"))return 16;
    if(!strcmp(service,"set_swing_mode"))return 32;
    if(!strcmp(service,"set_swing_horizontal_mode"))return 512;
  }
  if(!strcmp(domain,"cover")){
    if(!strcmp(service,"open_cover"))return 1;
    if(!strcmp(service,"close_cover"))return 2;
    if(!strcmp(service,"set_cover_position"))return 4;
    if(!strcmp(service,"stop_cover"))return 8;
    if(!strcmp(service,"open_cover_tilt"))return 16;
    if(!strcmp(service,"close_cover_tilt"))return 32;
    if(!strcmp(service,"stop_cover_tilt"))return 64;
    if(!strcmp(service,"set_cover_tilt_position"))return 128;
  }
  if(!strcmp(domain,"water_heater")){
    if(!strcmp(service,"set_temperature"))return 1;
    if(!strcmp(service,"set_operation_mode"))return 2;
    if(!strcmp(service,"set_away_mode"))return 4;
    if(!strcmp(service,"turn_on")||!strcmp(service,"turn_off"))return 8;
  }
  if(!strcmp(domain,"humidifier")&&!strcmp(service,"set_mode"))return 1;
  if(!strcmp(domain,"vacuum")){
    if(!strcmp(service,"start"))return 8192;
    if(!strcmp(service,"pause"))return 4;
    if(!strcmp(service,"stop"))return 8;
    if(!strcmp(service,"return_to_base"))return 16;
    if(!strcmp(service,"set_fan_speed"))return 32;
  }
  if(!strcmp(domain,"media_player")){
    if(!strcmp(service,"turn_on"))return 128;
    if(!strcmp(service,"turn_off"))return 256;
    if(!strcmp(service,"volume_set"))return 4;
    if(!strcmp(service,"volume_mute"))return 8;
    if(!strcmp(service,"media_play"))return 16384;
    if(!strcmp(service,"media_pause"))return 1;
    if(!strcmp(service,"media_stop"))return 4096;
    if(!strcmp(service,"media_next_track"))return 32;
    if(!strcmp(service,"media_previous_track"))return 16;
    if(!strcmp(service,"select_source"))return 2048;
  }
  return 0;
}
static bool home_registered_service(const cJSON *services,const char *domain,const char *service)
{
  if(!services)return true;
  const cJSON *row;cJSON_ArrayForEach(row,services)
    if(!strcmp(home_string(row,"domain"),domain)&&
       cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(row,"services"),service)))return true;
  return false;
}
static double home_bound(const cJSON *attrs,const char *key,double fallback)
{
  const cJSON *v=cJSON_GetObjectItemCaseSensitive(attrs,key);
  return cJSON_IsNumber(v)&&isfinite(v->valuedouble)?v->valuedouble:fallback;
}
static const char *home_option_attribute(const char *key)
{
  static const char *const pairs[][2]={{"option","options"},{"effect","effect_list"},
    {"hvac_mode","hvac_modes"},{"fan_mode","fan_modes"},{"preset_mode","preset_modes"},
    {"swing_mode","swing_modes"},{"swing_horizontal_mode","swing_horizontal_modes"},
    {"mode","available_modes"},{"operation_mode","operation_list"},{"source","source_list"},{"fan_speed","fan_speed_list"}};
  for(unsigned i=0;i<sizeof(pairs)/sizeof(*pairs);i++)if(!strcmp(key,pairs[i][0]))return pairs[i][1];
  return NULL;
}
static cJSON *home_parameter(const struct hass_action_rule_s *rule,const cJSON *attrs)
{
  const char *key=rule->key;if(!key)return NULL;
  cJSON *out=cJSON_CreateObject();if(!out)return NULL;
  cJSON_AddStringToObject(out,"type",rule->type=='n'?"number":rule->type=='b'?"boolean":rule->type=='c'?"array":"string");
  if(rule->type=='n'){
    double low=rule->minimum,high=rule->maximum,step=0;
    const char *minimum=NULL,*maximum=NULL,*step_key=NULL;
    if(!strcmp(key,"value")){minimum="min";maximum="max";step_key="step";}
    else if(!strcmp(key,"temperature")||!strcmp(key,"target_temp_low")||!strcmp(key,"target_temp_high")){
      minimum="min_temp";maximum="max_temp";step_key="target_temp_step";
    }else if(!strcmp(key,"humidity")){minimum="min_humidity";maximum="max_humidity";step_key="target_humidity_step";}
    else if(!strcmp(key,"color_temp_kelvin")){minimum="min_color_temp_kelvin";maximum="max_color_temp_kelvin";}
    else if(!strcmp(key,"percentage"))step_key="percentage_step";
    if(minimum)low=fmax(low,home_bound(attrs,minimum,low));
    if(maximum)high=fmin(high,home_bound(attrs,maximum,high));
    if(step_key)step=home_bound(attrs,step_key,0);
    if(low>high)goto unavailable;
    cJSON_AddNumberToObject(out,"minimum",low);cJSON_AddNumberToObject(out,"maximum",high);
    if(step>0)cJSON_AddNumberToObject(out,"step",step);
  }else if(rule->type=='s'){
    const char *options=home_option_attribute(key);
    if(options||!strcmp(key,"direction")){
      cJSON *list=cJSON_AddArrayToObject(out,"enum");
      if(!list)goto unavailable;
      if(!strcmp(key,"direction")){cJSON_AddItemToArray(list,cJSON_CreateString("forward"));cJSON_AddItemToArray(list,cJSON_CreateString("reverse"));}
      else{
        const cJSON *source=cJSON_GetObjectItemCaseSensitive(attrs,options);
        if(!cJSON_IsArray(source))goto unavailable;
        const cJSON *v;cJSON_ArrayForEach(v,source){
          if(cJSON_GetArraySize(list)>=64)break;
          if(cJSON_IsString(v)&&v->valuestring[0]&&strlen(v->valuestring)<=512)cJSON_AddItemToArray(list,cJSON_CreateString(v->valuestring));
        }
      }
      if(!cJSON_GetArraySize(list))goto unavailable;
    }else{cJSON_AddNumberToObject(out,"minLength",1);cJSON_AddNumberToObject(out,"maxLength",128);}
  }else if(rule->type=='c'){
    cJSON_AddNumberToObject(out,"minItems",3);cJSON_AddNumberToObject(out,"maxItems",3);
    cJSON *items=cJSON_AddObjectToObject(out,"items");cJSON_AddStringToObject(items,"type","integer");
    cJSON_AddNumberToObject(items,"minimum",0);cJSON_AddNumberToObject(items,"maximum",255);
  }
  return out;
unavailable:cJSON_Delete(out);return NULL;
}
static cJSON *home_entity_actions(const cJSON *body,const cJSON *services)
{
  cJSON *actions=cJSON_CreateArray();if(!actions)return NULL;
  const char *entity=home_string(body,"entity_id"),*state=home_string(body,"state");char domain[24];home_domain(entity,domain,sizeof(domain));
  if(!home_entity_valid(entity)||!strcmp(domain,"lock")||!strcmp(domain,"alarm_control_panel")||
     !state[0]||!strcmp(state,"unavailable")||(!strcmp(state,"unknown")&&strcmp(domain,"button")&&strcmp(domain,"input_button")&&strcmp(domain,"scene")&&strcmp(domain,"script")))return actions;
  const cJSON *attrs=cJSON_GetObjectItemCaseSensitive(body,"attributes");
  const cJSON *flags=cJSON_GetObjectItemCaseSensitive(attrs,"supported_features");
  bool has_flags=cJSON_IsNumber(flags)&&isfinite(flags->valuedouble)&&flags->valuedouble>=0&&flags->valuedouble<=0xffffffffu;
  unsigned features=has_flags?(unsigned)flags->valuedouble:0;
  for(unsigned i=0;i<sizeof(hass_action_rules)/sizeof(*hass_action_rules);i++){
    const struct hass_action_rule_s *r=&hass_action_rules[i];
    if(strcmp(r->domain,domain)||!home_registered_service(services,domain,r->service))continue;
    unsigned bit=home_action_feature(domain,r->service,r->key);
    if(bit&&has_flags&&!(features&bit))continue;
    if(bit&&!has_flags){
      const char *field=r->key;
      if(field&&!strcmp(field,"position"))field="current_position";
      if(field&&!strcmp(field,"tilt_position"))field="current_tilt_position";
      if(field&&!strcmp(field,"hvac_mode"))field="hvac_modes";
      bool basic=!strcmp(r->service,"turn_on")||!strcmp(r->service,"turn_off")||
        (!strcmp(domain,"cover")&&bit<=8&&!r->key);
      const char *options=field?home_option_attribute(field):NULL;
      if(!basic&&(!field||(!cJSON_GetObjectItemCaseSensitive(attrs,field)&&
           (!options||!cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(attrs,options))))))continue;
    }
    if(r->key&&!strcmp(domain,"light")){
      const cJSON *modes=cJSON_GetObjectItemCaseSensitive(attrs,"supported_color_modes");
      bool brightness=cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attrs,"brightness"));
      bool color=false,temperature=cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attrs,"color_temp_kelvin"))||cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attrs,"color_temp"));
      const cJSON *mode;cJSON_ArrayForEach(mode,modes)if(cJSON_IsString(mode)){
        const char *m=mode->valuestring;if(strcmp(m,"onoff"))brightness=true;
        if(!strcmp(m,"color_temp"))temperature=true;
        if(!strcmp(m,"rgb")||!strcmp(m,"rgbw")||!strcmp(m,"rgbww")||!strcmp(m,"hs")||!strcmp(m,"xy"))color=true;
      }
      if(!strcmp(r->key,"brightness_pct")&&!brightness)continue;
      if(!strcmp(r->key,"color_temp_kelvin")&&!temperature)continue;
      if(!strcmp(r->key,"rgb_color")&&!color)continue;
    }
    if(r->key&&!strcmp(r->key,"value")&&r->type=='n'&&
       (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attrs,"min"))||!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(attrs,"max"))))continue;
    cJSON *parameter=r->key?home_parameter(r,attrs):NULL;
    if(r->key&&!parameter)continue;
    cJSON *action=NULL,*entry;
    cJSON_ArrayForEach(entry,actions)if(!strcmp(home_string(entry,"service"),r->service)){action=entry;break;}
    if(!action){action=cJSON_CreateObject();if(!action){cJSON_Delete(parameter);continue;}
      cJSON_AddStringToObject(action,"service",r->service);cJSON_AddObjectToObject(action,"parameters");
      if(!strcmp(domain,"climate")&&!strcmp(r->service,"set_temperature"))cJSON_AddStringToObject(action,"argument_rule","temperature OR both target_temp_low and target_temp_high; never mix");
      cJSON_AddItemToArray(actions,action);
    }
    if(parameter)cJSON_AddItemToObject(cJSON_GetObjectItemCaseSensitive(action,"parameters"),r->key,parameter);
  }
  return actions;
}
static bool home_parameters_match(const cJSON *action,const cJSON *args)
{
  const cJSON *parameters=cJSON_GetObjectItemCaseSensitive(action,"parameters");
  unsigned count=0;
  for(const cJSON *p=args?args->child:NULL;p;p=p->next){
    const cJSON *spec=cJSON_GetObjectItemCaseSensitive(parameters,p->string);if(!spec)return false;
    if(cJSON_GetObjectItemCaseSensitive(args,p->string)!=p)return false;
    const cJSON *options=cJSON_GetObjectItemCaseSensitive(spec,"enum");
    if(options){bool found=false;const cJSON *v;cJSON_ArrayForEach(v,options)if(cJSON_IsString(p)&&cJSON_IsString(v)&&!strcmp(p->valuestring,v->valuestring))found=true;if(!found)return false;}
    if(cJSON_IsNumber(p)){
      double low=home_bound(spec,"minimum",-1e9),high=home_bound(spec,"maximum",1e9);
      if(!isfinite(p->valuedouble)||p->valuedouble<low||p->valuedouble>high)return false;
      double step=home_bound(spec,"step",0);
      if(step>0&&strcmp(p->string,"percentage")){double n=(p->valuedouble-low)/step;if(fabs(n-round(n))>1e-6)return false;}
    }
    count++;
  }
  return count>0||cJSON_GetArraySize(parameters)==0;
}
