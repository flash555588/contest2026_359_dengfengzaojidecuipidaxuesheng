/* SPDX-License-Identifier: Apache-2.0
 * NSH diagnostics enqueue work; only the desktop thread calls LVGL.
 */
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nuttx/video/fb.h>

static pthread_mutex_t g_ui_probe_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_ui_probe_command[24];
static bool g_ui_probe_pending;
static bool g_ui_probe_done;
static char g_ui_probe_report[32768];
static size_t g_ui_probe_length;
static unsigned g_ui_probe_id;
uint16_t *g_ui_probe_frame;
volatile size_t g_ui_probe_frame_bytes;

/* Lossless run-length capture makes USB JTAG readback practical. The UI
 * thread reads the current displayed page; GDB only reads the saved buffer.
 */
static int desktop_ui_probe_frame(void)
{
  struct fb_planeinfo_s plane;
  memset(&plane, 0, sizeof(plane));
  int fd = open("/dev/fb0", O_RDONLY);
  if (fd < 0) return -errno;
  int ret = ioctl(fd, FBIOGET_PLANEINFO, (unsigned long)&plane);
  int error = errno;
  close(fd);
  if (ret < 0) return -error;
  if (plane.bpp != 16 || plane.stride < 2048 ||
      (size_t)(plane.yoffset + 600) * plane.stride > plane.fblen) return -EINVAL;
  free(g_ui_probe_frame);
  g_ui_probe_frame_bytes = 0;
  g_ui_probe_frame = malloc(1024 * 600 * 4);
  if (!g_ui_probe_frame) return -ENOMEM;
  size_t pairs = 0;
  uint16_t value = 0;
  uint16_t count = 0;
  for (unsigned y = 0; y < 600; y++)
    {
      const uint16_t *row = (const uint16_t *)((const uint8_t *)plane.fbmem +
                            (size_t)(plane.yoffset + y) * plane.stride);
      for (unsigned x = 0; x < 1024; x++)
        {
          if (count && (row[x] != value || count == UINT16_MAX))
            {
              g_ui_probe_frame[pairs++] = count;
              g_ui_probe_frame[pairs++] = value;
              count = 0;
            }
          value = row[x];
          count++;
        }
    }
  if (count)
    { g_ui_probe_frame[pairs++] = count; g_ui_probe_frame[pairs++] = value; }
  g_ui_probe_frame_bytes = pairs * sizeof(uint16_t);
  return 0;
}

static void desktop_ui_probe_print(const char *format, ...)
{
  size_t available = sizeof(g_ui_probe_report) - g_ui_probe_length;
  va_list args;
  va_start(args, format);
  int size = vsnprintf(g_ui_probe_report + g_ui_probe_length, available, format, args);
  va_end(args);
  if (size > 0) g_ui_probe_length += (size_t)size < available ? (size_t)size : available - 1;
}

static void desktop_ui_probe_string(const char *text)
{
  desktop_ui_probe_print("\"");
  for (size_t i = 0; text && text[i] && i < 192; i++)
    {
      unsigned char c = text[i];
      if (c == '"' || c == '\\') desktop_ui_probe_print("\\%c", c);
      else if (c < 32) desktop_ui_probe_print("\\u%04x", c);
      else desktop_ui_probe_print("%c", c);
    }
  desktop_ui_probe_print("\"");
}

static void desktop_ui_probe_tree(lv_obj_t *object, unsigned parent)
{
  if (!lv_obj_is_visible(object)) return;
  unsigned id = ++g_ui_probe_id;
  const char *type = "container";
  const char *text = "";
  bool input = lv_obj_check_type(object, &lv_textarea_class);
  if (lv_obj_check_type(object, &lv_label_class))
    { type = "label"; text = lv_label_get_text(object); }
  else if (input)
    { type = "input"; text = lv_textarea_get_placeholder_text(object); }
  else if (lv_obj_check_type(object, &lv_button_class)) type = "button";
  else if (lv_obj_check_type(object, &lv_switch_class)) type = "switch";
  else if (lv_obj_check_type(object, &lv_slider_class)) type = "slider";
  else if (lv_obj_check_type(object, &lv_dropdown_class)) type = "dropdown";
  else if (lv_obj_check_type(object, &lv_keyboard_class)) type = "keyboard";
  else if (lv_obj_check_type(object, &lv_image_class)) type = "image";
  lv_area_t area;
  lv_obj_get_coords(object, &area);
  desktop_ui_probe_print("%s{\"id\":%u,\"parent\":%u,\"type\":\"%s\",\"x\":%ld,\"y\":%ld,\"w\":%ld,\"h\":%ld,\"scroll\":%s,\"text\":",
      id > 1 ? ",\n" : "", id, parent, type, (long)area.x1, (long)area.y1,
      (long)lv_area_get_width(&area), (long)lv_area_get_height(&area),
      lv_obj_has_flag(object, LV_OBJ_FLAG_SCROLLABLE) ? "true" : "false");
  desktop_ui_probe_string(text);
  desktop_ui_probe_print("}");
  /* Never expose input values, including passwords, through diagnostics. */
  if (input) return;
  for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++)
    desktop_ui_probe_tree(lv_obj_get_child(object, i), id);
}

static bool desktop_ui_probe_click(lv_obj_t *object, const char *text)
{
  if (!object || !lv_obj_is_visible(object)) return false;
  if (lv_obj_check_type(object, &lv_button_class))
    {
      lv_obj_t *label = lv_obj_get_child(object, 0);
      if (label && lv_obj_check_type(label, &lv_label_class) &&
          (text[0] == '*' ? strstr(lv_label_get_text(label), text + 1) != NULL :
                           !strcmp(lv_label_get_text(label), text)))
        { lv_obj_send_event(object, LV_EVENT_CLICKED, NULL); return true; }
    }
  for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++)
    if (desktop_ui_probe_click(lv_obj_get_child(object, i), text)) return true;
  return false;
}

static lv_obj_t *desktop_ui_probe_form(lv_obj_t *object)
{
  if (!object || !lv_obj_is_visible(object)) return NULL;
  if (lv_obj_check_type(object, &lv_keyboard_class))
    return lv_obj_get_parent(object);
  for (uint32_t i = lv_obj_get_child_count(object); i > 0; i--)
    {
      lv_obj_t *form = desktop_ui_probe_form(lv_obj_get_child(object, i - 1));
      if (form) return form;
    }
  return NULL;
}

static void desktop_ui_probe_service(void)
{
  char command[sizeof(g_ui_probe_command)];
  pthread_mutex_lock(&g_ui_probe_lock);
  if (!g_ui_probe_pending || g_ui_probe_done)
    { pthread_mutex_unlock(&g_ui_probe_lock); return; }
  memcpy(command, g_ui_probe_command, sizeof(command));
  pthread_mutex_unlock(&g_ui_probe_lock);

  g_ui_probe_length = 0;
  if (!strcmp(command, "frame"))
    {
      int ret = desktop_ui_probe_frame();
      desktop_ui_probe_print("desktop: frame result=%d bytes=%lu\n", ret,
                             (unsigned long)g_ui_probe_frame_bytes);
    }
  else if (!strcmp(command, "frame-free"))
    {
      free(g_ui_probe_frame);
      g_ui_probe_frame = NULL;
      g_ui_probe_frame_bytes = 0;
      desktop_ui_probe_print("desktop: frame released\n");
    }
  else if (strcmp(command, "audit") == 0)
    {
      lv_obj_update_layout(g_desktop.screen);
      g_ui_probe_id = 0;
      desktop_ui_probe_print("UI_LAYOUT_BEGIN\n[");
      lv_obj_t *root = g_desktop.toast ? g_desktop.toast :
                       g_desktop.dialog ? g_desktop.dialog :
                       lv_obj_has_flag(g_desktop.panel, LV_OBJ_FLAG_HIDDEN) ?
                       g_glass.home : g_desktop.current_card;
      /* Input shades cover the app below; audit the visible form itself. */
      lv_obj_t *form = desktop_ui_probe_form(root);
      if (form && root == g_desktop.current_card &&
          lv_obj_get_width(root) == 1024) root = form;
      if (root) desktop_ui_probe_tree(root, 0);
      desktop_ui_probe_print("\n]\nUI_LAYOUT_END\n");
    }
  else
    {
      if (!strcmp(command, "camera-start"))
        desktop_ui_probe_print("camera start clicked=%d\n",
          desktop_ui_probe_click(g_desktop.current_card, "开始预览"));
      else if (!strcmp(command, "camera-mode"))
        desktop_ui_probe_print("camera mode clicked=%d\n",
          desktop_ui_probe_click(g_desktop.current_card, "*MJPEG"));
      else if (!strcmp(command, "camera-photo"))
        desktop_ui_probe_print("camera capture accepted=%d\n", qpk_runtime_camera_capture());
      else if (!strcmp(command, "home")) panel_hide(NULL);
      else if (!strcmp(command, "settings")) glass_settings(NULL);
      else if (!strcmp(command, "wifi")) glass_wifi_page(NULL);
      else if (!strcmp(command, "bluetooth")) glass_bluetooth_page(NULL);
      else if (!strcmp(command, "ble-scan"))
        { glass_bluetooth_page(NULL); (void)glass_ble_scan(); }
      else if (!strcmp(command, "wifi-scan"))
        { glass_wifi_page(NULL); lv_obj_send_event(g_wifi_scan, LV_EVENT_CLICKED, NULL); }
      else if (!strcmp(command, "files")) glass_storage_page(NULL);
      else if (!strcmp(command, "resources")) glass_resources_page(NULL);
      else if (!strcmp(command, "music")) glass_music(NULL);
      else if (!strcmp(command, "apps")) glass_apps(NULL);
      else if (!strcmp(command, "about")) about_clicked(NULL);
      else if (!strcmp(command, "environment")) glass_environment(NULL);
      else if (!strcmp(command, "hello")) launch_builtin_qapp(NULL);
      else if (!strcmp(command, "toast"))
        { launch_builtin_qapp(NULL); show_toast("操作完成，中文提示清晰可读"); }
      else if (!strcmp(command, "dialog"))
        qpk_show_dialog("提示\n中文内容应清晰显示，正文与确认按钮保持独立间距。\n较长的内容可以在正文区域滚动查看。");
      else if (!strcmp(command, "2048")) launch_builtin_2048_qapp(NULL);
      else if (!strcmp(command, "ouo")) launch_builtin_ouo_qapp(NULL);
      else if (!strcmp(command, "ha")) launch_builtin_homeassistant_qapp(NULL);
      else if (!strcmp(command, "ha-settings") || !strcmp(command, "ha-address") ||
               !strcmp(command, "ha-advanced"))
        {
          launch_builtin_homeassistant_qapp(NULL);
          lv_obj_update_layout(g_desktop.screen);
          desktop_ui_probe_click(g_desktop.current_card, "设置");
          lv_obj_update_layout(g_desktop.screen);
          if (!strcmp(command, "ha-address"))
            desktop_ui_probe_click(g_desktop.current_card, "地址");
          if (!strcmp(command, "ha-advanced"))
            desktop_ui_probe_click(g_desktop.current_card, "高级服务");
        }
      else if (!strcmp(command, "dropdown"))
        { glass_settings(NULL); lv_dropdown_open(g_glass.widget_dropdown); }
      else if (!strcmp(command, "files-new"))
        { glass_storage_page(NULL); g_files_operation = 1; glass_files_dialog_async(NULL); }
      else if (!strcmp(command, "light") || !strcmp(command, "dark"))
        { panel_hide(NULL); g_desktop.light_theme = !strcmp(command, "light"); apply_theme(); }
      desktop_ui_probe_print("desktop: ui %s ready\n", command);
    }
  pthread_mutex_lock(&g_ui_probe_lock);
  g_ui_probe_done = true;
  pthread_mutex_unlock(&g_ui_probe_lock);
}

static int desktop_ui_probe_request(const char *command)
{
  const char *commands[] = {"home", "settings", "wifi", "bluetooth", "files", "resources",
    "music", "apps", "about", "environment", "hello", "2048", "ouo", "ha", "dropdown",
    "files-new", "light", "dark", "audit", "frame", "frame-free", "wifi-scan", "ble-scan", "dialog",
    "ha-settings", "ha-address", "ha-advanced", "toast", "camera-start", "camera-photo", "camera-mode"};
  bool found = false;
  for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
    if (!strcmp(command, commands[i])) found = true;
  if (!found || !lv_is_initialized())
    { puts("desktop: unknown UI page or desktop not running"); return 1; }
  pthread_mutex_lock(&g_ui_probe_lock);
  if (g_ui_probe_pending)
    { pthread_mutex_unlock(&g_ui_probe_lock); puts("desktop: UI request busy"); return 1; }
  strlcpy(g_ui_probe_command, command, sizeof(g_ui_probe_command));
  g_ui_probe_pending = true;
  g_ui_probe_done = false;
  pthread_mutex_unlock(&g_ui_probe_lock);
  for (unsigned i = 0; i < 250; i++)
    {
      usleep(20000);
      pthread_mutex_lock(&g_ui_probe_lock);
      if (g_ui_probe_done)
        {
          char *report = strdup(g_ui_probe_report);
          g_ui_probe_pending = false;
          pthread_mutex_unlock(&g_ui_probe_lock);
          if (!report) return 1;
          fputs(report, stdout);
          free(report);
          return 0;
        }
      pthread_mutex_unlock(&g_ui_probe_lock);
    }
  puts("desktop: UI request timed out");
  return 1;
}
