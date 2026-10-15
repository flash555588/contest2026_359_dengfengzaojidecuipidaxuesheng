/* SPDX-License-Identifier: Apache-2.0 */
static struct {
  lv_obj_t *location,*summary,*aqi,*category,*temperature,*humidity,*pm25;
  lv_obj_t *report,*status,*icon,*data_state;
  unsigned revision;enum glass_weather_state state;
  bool stale,valid,data_valid;
} g_weather_detail;
static void glass_weather_detail_deleted(lv_event_t *e)
{LV_UNUSED(e);memset(&g_weather_detail,0,sizeof(g_weather_detail));}
static const char *weather_detail_icon(const char *condition)
{
  if(strstr(condition,"雪"))return GLASS_ICON_CLOUD_SNOW;
  if(strstr(condition,"雨"))return GLASS_ICON_CLOUD_RAIN;
  if(strstr(condition,"阴")||strstr(condition,"雾"))return GLASS_ICON_CLOUD;
  if(strstr(condition,"晴")&&!strstr(condition,"云"))return GLASS_ICON_SUN;
  return GLASS_ICON_CLOUD_SUN;
}
static void glass_weather_detail_update(const struct glass_weather_snapshot *weather,bool stale)
{
  if(!g_weather_detail.location||(g_weather_detail.valid&&g_weather_detail.revision==weather->revision&&
     g_weather_detail.stale==stale&&g_weather_detail.state==weather->state&&g_weather_detail.data_valid==weather->valid))return;
  g_weather_detail.revision=weather->revision;g_weather_detail.state=weather->state;
  g_weather_detail.stale=stale;g_weather_detail.valid=true;g_weather_detail.data_valid=weather->valid;
  const struct glass_weather_data *data=&weather->data;
  lv_label_set_text(g_weather_detail.location,weather->valid?data->location:"当地天气");
  lv_label_set_text(g_weather_detail.summary,weather->valid?data->condition:"联网后自动获取天气");
  lv_label_set_text(g_weather_detail.icon,weather_detail_icon(weather->valid?data->condition:""));
  if(weather->valid&&data->temperature!=-999)lv_label_set_text_fmt(g_weather_detail.temperature,"%d°",data->temperature);
  else lv_label_set_text(g_weather_detail.temperature,"--°");
  if(weather->valid&&data->aqi>=0)lv_label_set_text_fmt(g_weather_detail.aqi,"%d",data->aqi);else lv_label_set_text(g_weather_detail.aqi,"--");
  lv_label_set_text(g_weather_detail.category,weather->valid&&data->aqi>=0?data->category:"暂无数据");
  uint32_t air=g_desktop.light_theme?0x3a8c79:0x83cdb6;
  if(weather->valid&&data->level>=3)air=g_desktop.light_theme?0xb98245:0xe6b67c;
  lv_obj_set_style_text_color(g_weather_detail.category,lv_color_hex(weather->valid?air:detail_muted()),0);
  if(weather->valid&&data->humidity>=0)lv_label_set_text_fmt(g_weather_detail.humidity,"%d%%",data->humidity);else lv_label_set_text(g_weather_detail.humidity,"--%");
  if(weather->valid&&data->pm25>=0)lv_label_set_text_fmt(g_weather_detail.pm25,"%d",data->pm25);else lv_label_set_text(g_weather_detail.pm25,"--");
  if(weather->valid)lv_label_set_text_fmt(g_weather_detail.report,"天气报告 · %s",data->report_time[0]?data->report_time:"时间未提供");
  else lv_label_set_text(g_weather_detail.report,"天气数据尚未获取");
  const char *status=weather->state==WEATHER_WAIT_NET?"连接网络后会自动更新":weather->state==WEATHER_WAIT_TIME?"等待系统校时":
    weather->state==WEATHER_FETCHING?"正在更新天气":weather->state==WEATHER_FAILED?(weather->valid?"暂时无法更新，显示上次数据":"暂时无法获取，稍后自动重试"):
    stale?"数据待更新":"按网络位置定位 · 每小时自动更新";
  lv_label_set_text(g_weather_detail.status,status);
  const char *state=weather->state==WEATHER_FETCHING?"更新中":weather->state==WEATHER_WAIT_NET?"等待联网":
    weather->state==WEATHER_WAIT_TIME?"等待校时":weather->state==WEATHER_FAILED?"更新失败":stale?"待更新":weather->valid?"已更新":"暂无数据";
  lv_label_set_text(g_weather_detail.data_state,state);
}
static void glass_weather_refresh_clicked(lv_event_t *e)
{
  LV_UNUSED(e);glass_weather_refresh();
  if(g_weather_detail.status)lv_label_set_text(g_weather_detail.status,"已请求刷新 · 距上次请求至少一分钟");
}
static void glass_weather_open(lv_event_t *e)
{
  LV_UNUSED(e);lv_obj_t *card=panel_card("当地天气");if(!card)return;
  detail_page(card);memset(&g_weather_detail,0,sizeof(g_weather_detail));
  lv_obj_add_event_cb(card,glass_weather_detail_deleted,LV_EVENT_DELETE,NULL);
  detail_text(card,"天气与环境",32,26,500,28,detail_ink());
  detail_text(card,"当前天气 · 空气质量与环境指标",32,66,600,14,detail_muted());
  detail_button(card,"返回桌面",872,24,120,panel_hide,NULL);
  lv_obj_t *hero=detail_tile(card,32,104,588,326);
  lv_obj_set_style_bg_color(hero,lv_color_hex(g_desktop.light_theme?0xf5f0f7:0x2d2937),0);
  lv_obj_set_style_border_width(hero,0,0);
  lv_obj_t *halo=glass_box(hero,416,88,126,126,g_desktop.light_theme?0xfcf4e7:0x463b42,63);
  lv_obj_set_style_bg_opa(halo,180,0);
  g_weather_detail.location=detail_text(hero,"当地天气",24,24,368,20,detail_ink());
  detail_text(hero,"当前天气",442,27,122,12,detail_muted());
  detail_text(hero,"温度 / 摄氏度",24,78,320,14,detail_muted());
  g_weather_detail.temperature=detail_text(hero,"--°",24,106,326,64,detail_ink());
  g_weather_detail.summary=detail_text(hero,"",24,194,430,20,detail_ink());
  g_weather_detail.icon=home_icon(hero,GLASS_ICON_CLOUD_SUN,448,108,64,g_desktop.light_theme?0xb99561:0xe1bf84);
  glass_box(hero,24,248,540,1,g_desktop.light_theme?0xe4dfe8:0x514b60,0);
  detail_text(hero,"当地网络位置",24,275,300,14,detail_muted());
  detail_text(hero,"每小时自动更新",380,276,184,12,detail_muted());
  const char *titles[]={"空气质量 / AQI","相对湿度","PM2.5","数据状态"};
  const char *icons[]={GLASS_ICON_SPARKLES,"\xef\x81\x83",GLASS_ICON_GRID,LV_SYMBOL_REFRESH};
  lv_obj_t *values[4];
  for(unsigned i=0;i<4;i++){
    lv_obj_t *tile=detail_tile(card,636+(i%2)*186,104+(i/2)*172,170,154);
    home_icon(tile,icons[i],18,18,20,i==1?0x5c9f95:detail_accent());
    detail_text(tile,titles[i],18,49,134,12,detail_muted());
    values[i]=detail_text(tile,"--",18,76,134,i==3?20:34,detail_ink());
    if(i==0)g_weather_detail.category=detail_text(tile,"",18,124,134,12,detail_muted());
    if(i==1)detail_text(tile,"环境湿度",18,124,134,12,detail_muted());
    if(i==2)detail_text(tile,"微克 / 立方米",18,124,134,12,detail_muted());
    if(i==3)detail_text(tile,"保留最近数据",18,124,134,12,detail_muted());
  }
  g_weather_detail.aqi=values[0];g_weather_detail.humidity=values[1];g_weather_detail.pm25=values[2];g_weather_detail.data_state=values[3];
  lv_obj_t *report=detail_tile(card,32,448,960,118);
  home_icon(report,LV_SYMBOL_REFRESH,24,26,20,detail_accent());
  g_weather_detail.report=detail_text(report,"",62,24,666,14,detail_ink());
  g_weather_detail.status=detail_text(report,"",62,63,666,12,detail_muted());
  lv_obj_t *refresh=detail_button(report,"刷新天气",792,34,144,glass_weather_refresh_clicked,NULL);
  lv_obj_set_style_bg_color(refresh,lv_color_hex(detail_accent()),0);
  lv_obj_set_style_text_color(lv_obj_get_child(refresh,0),lv_color_hex(g_desktop.light_theme?0xffffff:0x272130),0);
  struct glass_weather_snapshot weather;glass_weather_get(&weather);
  struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
  uint64_t ms=(uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
  glass_weather_detail_update(&weather,weather.valid&&ms>=weather.received_ms&&ms-weather.received_ms>=7200000);
}
