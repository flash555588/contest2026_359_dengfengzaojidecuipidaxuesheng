/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_system_monitor.h"

static struct
{
  lv_obj_t *card;
  lv_obj_t *labels[6];
  lv_timer_t *timer;
  struct glass_system_monitor *monitor;
  char displayed[6][96];
} g_resources;

static void glass_resources_text(unsigned field, const char *text)
{
  /* Cache our source string: LONG_DOT is allowed to modify the label buffer. */
  if (strcmp(g_resources.displayed[field], text))
    {
      strlcpy(g_resources.displayed[field], text, sizeof(g_resources.displayed[field]));
      lv_label_set_text(g_resources.labels[field], text);
    }
}

static void glass_resources_closed(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_resources.timer) lv_timer_delete(g_resources.timer);
  glass_system_release(g_resources.monitor);
  memset(&g_resources, 0, sizeof(g_resources));
}

static void glass_resources_show(const struct glass_system_sample *sample)
{
  char text[96];
  if (!sample->cpu_error)
    snprintf(text, sizeof(text), "%u.%u%%", sample->cpu_tenths / 10,
             sample->cpu_tenths % 10);
  else snprintf(text, sizeof(text), "%s", sample->cpu_error == ENOTSUP ?
                "未启用" : "暂不可用");
  glass_resources_text(0, text);
  const uint64_t values[] =
    {sample->heap_free, sample->heap_largest, sample->heap_peak};
  for (unsigned i = 0; i < 3; i++)
    {
      if (sample->heap_error) snprintf(text, sizeof(text), "暂不可用");
      else snprintf(text, sizeof(text), "%llu KiB",
                    (unsigned long long)(values[i] / 1024));
      glass_resources_text(i + 1, text);
    }
  if (sample->heap_error) snprintf(text, sizeof(text), "无法读取系统堆");
  else snprintf(text, sizeof(text), "总堆 %llu KiB",
                (unsigned long long)(sample->heap_total / 1024));
  glass_resources_text(4, text);
  glass_resources_text(5, sample->cpu_error == ENOTSUP ?
      "CPU 统计未启用 · 内存统计范围为系统堆" :
      sample->cpu_error || sample->heap_error ?
      "部分数据暂不可用 · 每秒重试" :
      "每秒采样 · 内存统计范围为系统堆");
}

static void glass_resources_update(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_resources.monitor)
    {
      int error = glass_system_start(&g_resources.monitor);
      if (error == EBUSY)
        {
          glass_resources_text(5, "等待上一次采样结束");
          return;
        }
      if (error)
        {
          char text[96];
          snprintf(text, sizeof(text), "采样启动失败 (%d)", error);
          glass_resources_text(5, text);
          lv_timer_delete(g_resources.timer);
          g_resources.timer = NULL;
          return;
        }
    }
  struct glass_system_status status;
  int error = glass_system_read(g_resources.monitor, &status);
  if (error || status.stopped)
    {
      char text[96];
      snprintf(text, sizeof(text), "采样已停止 (%d)", error ? error : status.error);
      glass_resources_text(5, text);
      lv_timer_delete(g_resources.timer);
      g_resources.timer = NULL;
      glass_system_release(g_resources.monitor);
      g_resources.monitor = NULL;
      return;
    }
  if (status.ready) glass_resources_show(&status.sample);
}

static void glass_resources_page(void *unused)
{
  LV_UNUSED(unused);
  lv_obj_t *card = panel_card("系统性能");
  if (!card) return;
  g_resources.card = card;
  lv_obj_add_event_cb(card, glass_resources_closed, LV_EVENT_DELETE, NULL);
  glass_box(card, 24, 80, 420, 160, theme_surface(), 16);
  glass_box(card, 468, 80, 420, 160, theme_surface(), 16);
  glass_text(card, "CPU 总占用", 44, 96, 360, 20, theme_secondary());
  glass_text(card, "可用堆", 488, 96, 360, 20, theme_secondary());
  g_resources.labels[0] = glass_number(card, "--", 44, 134, 360, false, theme_primary());
  g_resources.labels[1] = glass_number(card, "--", 488, 134, 360, false, theme_primary());
  glass_text(card, "整体负载 0-100%", 44, 208, 360, 16, theme_secondary());
  g_resources.labels[4] = glass_text(card, "", 488, 208, 360, 16, theme_secondary());
  glass_text(card, "最大连续空闲", 44, 280, 360, 20, theme_secondary());
  glass_text(card, "堆使用峰值", 488, 280, 360, 20, theme_secondary());
  g_resources.labels[2] = glass_text(card, "--", 44, 322, 360, 28, theme_primary());
  g_resources.labels[3] = glass_text(card, "--", 488, 322, 360, 28, theme_primary());
  g_resources.labels[5] = glass_text(card, "", 24, 432, 864, 16, theme_secondary());
  glass_resources_text(5, "正在采样");
  g_resources.timer = lv_timer_create(glass_resources_update, 1000, NULL);
  if (!g_resources.timer) glass_resources_text(5, "采样启动失败：内存不足");
  else glass_resources_update(NULL);
}
