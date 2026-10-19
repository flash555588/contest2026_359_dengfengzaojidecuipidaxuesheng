/* SPDX-License-Identifier: Apache-2.0 */
static void about_clicked(lv_event_t *e)
{
  LV_UNUSED(e);
  lv_obj_t *card = panel_card("关于设备");
  if (!card) return;
  detail_page(card);
  detail_sidebar(card, "关于设备", LV_SYMBOL_SETTINGS, panel_hide);
  detail_text(card, "关于设备", 216, 26, 720, 28, detail_ink());
  detail_text(card, "Glass 桌面 · ESP32-P4", 216, 66, 720, 16, detail_muted());

  lv_obj_t *info = detail_tile(card, 216, 112, 784, 344);
  struct timespec ts = {0};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  char text[384];
  snprintf(text, sizeof(text),
           "作者：电子科技大学 闻家贤\n\n"
           "系统：Apache NuttX RTOS\n"
           "处理器：双核 RISC-V\n"
           "内存：内部 RAM + PSRAM\n"
           "运行时间：%lu 秒\n"
           "中文字体：阿里巴巴普惠体 3.0 55 Regular",
           (unsigned long)ts.tv_sec);
  lv_obj_t *body = detail_text(info, text, 24, 24, 736, 20, detail_ink());
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
}
