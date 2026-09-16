/* SPDX-License-Identifier: Apache-2.0 */
static struct
{
  lv_obj_t *location;
  lv_obj_t *summary;
  lv_obj_t *aqi;
  lv_obj_t *category;
  lv_obj_t *temperature;
  lv_obj_t *humidity;
  lv_obj_t *report;
  lv_obj_t *status;
  unsigned revision;
  bool stale;
  bool valid;
} g_weather_detail;

static void glass_weather_detail_deleted(lv_event_t *e)
{
  LV_UNUSED(e);
  memset(&g_weather_detail, 0, sizeof(g_weather_detail));
}

static void glass_weather_detail_update(const struct glass_weather_snapshot *weather, bool stale)
{
  if (!g_weather_detail.location ||
      (g_weather_detail.valid && g_weather_detail.revision == weather->revision &&
       g_weather_detail.stale == stale)) return;
  g_weather_detail.revision = weather->revision;
  g_weather_detail.stale = stale;
  g_weather_detail.valid = true;
  const struct glass_weather_data *data = &weather->data;
  lv_label_set_text(g_weather_detail.location, weather->valid ? data->location : "当地天气");
  lv_label_set_text(g_weather_detail.summary, weather->valid ? data->condition : "联网后自动获取当地天气");
  if (weather->valid && data->aqi >= 0)
    lv_label_set_text_fmt(g_weather_detail.aqi, "%d", data->aqi);
  else lv_label_set_text(g_weather_detail.aqi, "--");
  lv_label_set_text(g_weather_detail.category,
    weather->valid && data->aqi >= 0 ? data->category : "暂无空气质量数据");
  if (weather->valid && data->temperature != -999)
    lv_label_set_text_fmt(g_weather_detail.temperature, "%d C", data->temperature);
  else lv_label_set_text(g_weather_detail.temperature, "-- C");
  if (weather->valid && data->humidity >= 0)
    lv_label_set_text_fmt(g_weather_detail.humidity, "%d%%", data->humidity);
  else lv_label_set_text(g_weather_detail.humidity, "--%");
  if (weather->valid)
    lv_label_set_text_fmt(g_weather_detail.report, "天气报告 · %s", data->report_time[0] ? data->report_time : "时间未提供");
  else lv_label_set_text(g_weather_detail.report, "天气数据尚未获取");
  const char *status = weather->state == WEATHER_WAIT_NET ? "等待联网" :
    weather->state == WEATHER_WAIT_TIME ? "等待系统校时" :
    weather->state == WEATHER_FETCHING ? "正在更新天气" :
    weather->state == WEATHER_FAILED ? (weather->valid ? "暂时无法更新，显示上次数据" : "暂时无法获取，稍后自动重试") :
    stale ? "数据待更新" : "按网络位置定位 · 每小时自动更新";
  lv_label_set_text(g_weather_detail.status, status);
}

static void glass_weather_refresh_clicked(lv_event_t *e)
{
  LV_UNUSED(e);
  glass_weather_refresh();
  if (g_weather_detail.status)
    lv_label_set_text(g_weather_detail.status, "已请求刷新 · 距上次请求至少一分钟");
}

static void glass_weather_open(lv_event_t *e)
{
  LV_UNUSED(e);
  lv_obj_t *card = panel_card("当地天气");
  memset(&g_weather_detail, 0, sizeof(g_weather_detail));
  lv_obj_add_event_cb(card, glass_weather_detail_deleted, LV_EVENT_DELETE, NULL);
  uint32_t primary = theme_primary(), secondary = theme_secondary();
  g_weather_detail.location = glass_text(card, "当地天气", 28, 80, 650, 24, primary);
  g_weather_detail.summary = glass_text(card, "", 28, 118, 650, 20, secondary);
  const char *names[] = {"空气质量 / AQI", "温度 / 摄氏度", "相对湿度"};
  lv_obj_t *values[3];
  for (int i = 0; i < 3; i++)
    {
      lv_obj_t *tile = glass_card(card, 28 + i * 290, 168, 270, 172, theme_surface());
      glass_text(tile, names[i], 20, 18, 230, 16, secondary);
      values[i] = glass_number(tile, "--", 20, 48, 230, false, primary);
      if (i == 0) g_weather_detail.category = glass_text(tile, "", 20, 128, 230, 16, secondary);
    }
  g_weather_detail.aqi = values[0];
  g_weather_detail.temperature = values[1];
  g_weather_detail.humidity = values[2];
  g_weather_detail.report = glass_text(card, "", 28, 362, 840, 16, secondary);
  g_weather_detail.status = glass_text(card, "", 28, 396, 840, 16, secondary);
  glass_button(card, "刷新天气", 28, 446, 180, theme_surface(), glass_weather_refresh_clicked, NULL);
  struct glass_weather_snapshot weather;
  glass_weather_get(&weather);
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  uint64_t ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
  bool stale = weather.valid && ms >= weather.received_ms && ms - weather.received_ms >= 7200000;
  glass_weather_detail_update(&weather, stale);
}
