/****************************************************************************
 * apps/system/desktop/qpk_runtime.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/cache.h>
#include <nuttx/video/fb.h>
#include <nuttx/video/video.h>
#include <lvgl/lvgl.h>
#include <quickjs.h>

#include "qpk_runtime.h"
#include "qpk_homeassistant.h"
#include "qpk_pomodoro.h"

#define QPK_MEMORY_LIMIT  (2 * 1024 * 1024)
#define QPK_HA_MEMORY_LIMIT (8 * 1024 * 1024)
#define QPK_STACK_LIMIT   (16 * 1024)
#define QPK_MAX_WIDGETS   64
#define QPK_MAX_EVENTS    16
#define QPK_MAX_TIMERS    8
#define QPK_EVAL_BUDGET   250
#define QPK_EVENT_BUDGET  80
#define QPK_DIR CONFIG_SYSTEM_DESKTOP_QPK_DIR
#define QPK_STORAGE_KEY_MAX 64
#define QPK_STORAGE_VALUE_MAX 8192
#define QPK_STORAGE_PATH_MAX 256
#define QPK_STORAGE_ROOT QPK_DIR "/.data"
#define QPK_CAMERA_DEVICE "/dev/video0"
#define QPK_CAMERA_RAW_WIDTH 1024
#define QPK_CAMERA_RAW_HEIGHT 600
#define QPK_CAMERA_VIEW_WIDTH 512
#define QPK_CAMERA_VIEW_HEIGHT 300
#define QPK_CAMERA_FRAME_SIZE \
  (QPK_CAMERA_RAW_WIDTH * QPK_CAMERA_RAW_HEIGHT * sizeof(uint16_t))
#define QPK_CAMERA_BUFFER_COUNT 3
#define QPK_CAMERA_DISPLAY_BUFFER_COUNT 2
#define QPK_CAMERA_FRAMEBUFFER_COUNT 3
#define QPK_CAMERA_PREVIEW_PERIOD_MS 20
#define QPK_CAMERA_PROFILE_FRAMES 60
#define QPK_CAMERA_HEALTH_FRAMES 300
#define QPK_CAMERA_MAX_DQ_ERRORS 8
#define QPK_CAMERA_MAX_PAN_ERRORS 3

#ifdef CONFIG_ESPRESSIF_MIPI_DSI
FAR void *esp_mipi_dsi_noncache_addr(FAR void *addr);
#endif

enum qpk_widget_type_e
{
  QPK_WIDGET_LABEL = 0,
  QPK_WIDGET_NUMBER,
  QPK_WIDGET_PANEL,
  QPK_WIDGET_BUTTON,
  QPK_WIDGET_RECT,
};

struct qpk_event_s
{
  JSValue function;
  bool used;
};

struct qpk_timer_s
{
  JSValue function;
  lv_timer_t *timer;
  int id;
  bool used;
};

struct qpk_camera_s
{
  int fd;
  int fb_fd;
  lv_obj_t *canvas;
  lv_timer_t *timer;
  FAR uint8_t *raw;
  FAR uint16_t *rgb565[2];
  uint64_t fps_started_ms;
  uint64_t display_fps_started_ms;
  uint64_t profile_started_us;
  uint64_t profile_dq_us;
  uint64_t profile_invalidate_us;
  uint64_t profile_convert_us;
  uint64_t profile_clean_us;
  uint64_t profile_qbuf_us;
  uint32_t captured_frames;
  uint32_t dropped_frames;
  uint32_t frames;
  uint32_t profile_samples;
  uint32_t profile_converted;
  uint32_t dq_errors;
  uint32_t qbuf_errors;
  uint32_t pan_errors;
  atomic_bool stop_requested;
  volatile bool thread_alive;
  volatile int thread_error;
  bool thread_running;
  bool lock_initialized;
  int ready_buffer;
  int display_buffer;
  pthread_t thread;
  pthread_mutex_t lock;
  bool blank_mode;
  bool opened;
  bool streaming;
  bool direct_preview;
  FAR uint8_t *mmap_buffers[QPK_CAMERA_BUFFER_COUNT];
  size_t mmap_lengths[QPK_CAMERA_BUFFER_COUNT];
  unsigned int mmap_count;
  uint32_t memory_type;
  struct fb_videoinfo_s fb_video;
  struct fb_planeinfo_s fb_plane;
  unsigned int fb_page;
};

struct qpk_runtime_s
{
  JSRuntime *runtime;
  JSContext *context;
  lv_obj_t *root;
  lv_obj_t *widgets[QPK_MAX_WIDGETS];
  uint8_t widget_types[QPK_MAX_WIDGETS];
  struct qpk_event_s events[QPK_MAX_EVENTS];
  struct qpk_event_s swipe_event;
  struct qpk_timer_s timers[QPK_MAX_TIMERS];
  lv_obj_t *input_shade;
  lv_obj_t *input_textarea;
  JSValue input_callback;
  qpk_font_cb_t font_cb;
  qpk_message_cb_t toast_cb;
  qpk_message_cb_t dialog_cb;
  char name[48];
  char package[48];
  char version[24];
  char error[160];
  uint64_t deadline_ms;
  uint32_t primary_color;
  uint32_t secondary_color;
  uint32_t surface_color;
  int next_timer_id;
  struct qpk_camera_s camera;
};

static struct qpk_runtime_s g_qpk =
{
  .camera =
  {
    .fd = -1,
    .fb_fd = -1,
  },
};

static uint64_t qpk_now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint64_t qpk_now_us(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int qpk_interrupt(JSRuntime *runtime, void *opaque)
{
  struct qpk_runtime_s *qpk = opaque;

  (void)runtime;
  return qpk->deadline_ms != 0 && qpk_now_ms() > qpk->deadline_ms;
}

static void qpk_deadline_begin(unsigned int budget_ms)
{
  g_qpk.deadline_ms = qpk_now_ms() + budget_ms;
  JS_UpdateStackTop(g_qpk.runtime);
}

static void qpk_deadline_end(void)
{
  g_qpk.deadline_ms = 0;
}

static void qpk_show_error(const char *prefix)
{
  JSValue exception;
  JSValue stack;
  const char *message;
  const char *trace;

  exception = JS_GetException(g_qpk.context);
  message = JS_ToCString(g_qpk.context, exception);
  snprintf(g_qpk.error, sizeof(g_qpk.error), "%s%s%s",
           prefix ? prefix : "JavaScript 错误",
           message ? ": " : "", message ? message : "未知异常");
  stack = JS_GetPropertyStr(g_qpk.context, exception, "stack");
  trace = JS_IsUndefined(stack) ? NULL :
          JS_ToCString(g_qpk.context, stack);
  printf("[qpk] %s\n", trace ? trace : g_qpk.error);
  if (g_qpk.toast_cb != NULL)
    {
      g_qpk.toast_cb(g_qpk.error);
    }

  if (trace != NULL)
    {
      JS_FreeCString(g_qpk.context, trace);
    }

  JS_FreeValue(g_qpk.context, stack);
  if (message != NULL)
    {
      JS_FreeCString(g_qpk.context, message);
    }

  JS_FreeValue(g_qpk.context, exception);
}

static int qpk_run_jobs(void)
{
  JSContext *context;
  int ret;

  do
    {
      ret = JS_ExecutePendingJob(g_qpk.runtime, &context);
    }
  while (ret > 0);

  if (ret < 0)
    {
      qpk_show_error("异步任务错误");
      return -1;
    }

  return 0;
}

static JSValue qpk_call(JSValueConst function, unsigned int budget_ms)
{
  JSValue result;

  qpk_deadline_begin(budget_ms);
  result = JS_Call(g_qpk.context, function, JS_UNDEFINED, 0, NULL);
  qpk_deadline_end();
  if (JS_IsException(result))
    {
      qpk_show_error("事件执行错误");
      return result;
    }

  qpk_deadline_begin(budget_ms);
  qpk_run_jobs();
  qpk_deadline_end();
  return result;
}

static JSValue qpk_call_args(JSValueConst function, unsigned int budget_ms,
                             int argc, JSValueConst *argv)
{
  JSValue result;

  qpk_deadline_begin(budget_ms);
  result = JS_Call(g_qpk.context, function, JS_UNDEFINED, argc, argv);
  qpk_deadline_end();
  if (JS_IsException(result))
    {
      qpk_show_error("事件执行错误");
      return result;
    }

  qpk_deadline_begin(budget_ms);
  qpk_run_jobs();
  qpk_deadline_end();
  return result;
}

static int qpk_add_widget(lv_obj_t *object, enum qpk_widget_type_e type)
{
  int i;

  for (i = 0; i < QPK_MAX_WIDGETS; i++)
    {
      if (g_qpk.widgets[i] == NULL)
        {
          g_qpk.widgets[i] = object;
          g_qpk.widget_types[i] = type;
          return i + 1;
        }
    }

  return 0;
}

static int qpk_arg_int(JSContext *context, int argc,
                       JSValueConst *argv, int index, int fallback)
{
  int32_t value;

  if (index >= argc || JS_ToInt32(context, &value, argv[index]) < 0)
    {
      return fallback;
    }

  return value;
}

static uint32_t qpk_arg_color(JSContext *context, int argc,
                              JSValueConst *argv, int index,
                              uint32_t fallback)
{
  uint32_t value;

  if (index >= argc || JS_ToUint32(context, &value, argv[index]) < 0)
    {
      return fallback;
    }

  return value;
}

static const char *qpk_arg_string(JSContext *context, int argc,
                                  JSValueConst *argv, int index)
{
  return index < argc ? JS_ToCString(context, argv[index]) : NULL;
}

static void qpk_event_clicked(lv_event_t *event)
{
  struct qpk_event_s *binding = lv_event_get_user_data(event);
  JSValue result;

  if (g_qpk.context == NULL || binding == NULL || !binding->used)
    {
      return;
    }

  result = qpk_call(binding->function, QPK_EVENT_BUDGET);
  JS_FreeValue(g_qpk.context, result);
}

static void qpk_event_swiped(lv_event_t *event)
{
  lv_indev_t *indev = lv_event_get_indev(event);
  const char *direction;
  JSValue argument;
  JSValue result;

  if (g_qpk.context == NULL || !g_qpk.swipe_event.used || indev == NULL)
    {
      return;
    }

  switch (lv_indev_get_gesture_dir(indev))
    {
      case LV_DIR_LEFT:
        direction = "left";
        break;
      case LV_DIR_RIGHT:
        direction = "right";
        break;
      case LV_DIR_TOP:
        direction = "up";
        break;
      case LV_DIR_BOTTOM:
        direction = "down";
        break;
      default:
        return;
    }

  argument = JS_NewString(g_qpk.context, direction);
  result = qpk_call_args(g_qpk.swipe_event.function, QPK_EVENT_BUDGET,
                         1, &argument);
  JS_FreeValue(g_qpk.context, argument);
  JS_FreeValue(g_qpk.context, result);
}

static JSValue js_ui_on_swipe(JSContext *context, JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  (void)this_value;
  if (argc < 1 || !JS_IsFunction(context, argv[0]))
    {
      return JS_ThrowTypeError(context, "swipe handler must be a function");
    }

  if (g_qpk.swipe_event.used)
    {
      JS_FreeValue(context, g_qpk.swipe_event.function);
    }

  g_qpk.swipe_event.function = JS_DupValue(context, argv[0]);
  g_qpk.swipe_event.used = true;
  return JS_UNDEFINED;
}

static JSValue js_ui_text(JSContext *context, JSValueConst this_value,
                          int argc, JSValueConst *argv)
{
  const char *text;
  lv_obj_t *label;
  int size;
  int handle;

  (void)this_value;
  text = qpk_arg_string(context, argc, argv, 0);
  if (text == NULL)
    {
      return JS_EXCEPTION;
    }

  size = qpk_arg_int(context, argc, argv, 3, 20);
  label = lv_label_create(g_qpk.root);
  if (label == NULL)
    {
      JS_FreeCString(context, text);
      return JS_ThrowOutOfMemory(context);
    }

  lv_label_set_text(label, text);
  lv_obj_set_pos(label, qpk_arg_int(context, argc, argv, 1, 24),
                 qpk_arg_int(context, argc, argv, 2, 80));
  lv_obj_set_style_text_color(label,
      lv_color_hex(qpk_arg_color(context, argc, argv, 4, 0xffffff)), 0);
  if (g_qpk.font_cb != NULL)
    {
      lv_obj_set_style_text_font(label, g_qpk.font_cb(size), 0);
    }

  handle = qpk_add_widget(label, QPK_WIDGET_LABEL);
  JS_FreeCString(context, text);
  if (handle == 0)
    {
      lv_obj_delete(label);
      return JS_ThrowInternalError(context, "too many widgets");
    }

  return JS_NewInt32(context, handle);
}

static void qpk_number_style(lv_obj_t *container, const char *text)
{
  lv_obj_t *label = lv_obj_get_child(container, 0);

  if (label != NULL)
    {
      lv_label_set_text(label, text);
      lv_obj_center(label);
    }
}

static JSValue js_ui_number(JSContext *context, JSValueConst this_value,
                            int argc, JSValueConst *argv)
{
  const char *text;
  lv_obj_t *container;
  lv_obj_t *label;
  int handle;

  (void)this_value;
  text = qpk_arg_string(context, argc, argv, 0);
  if (text == NULL)
    {
      return JS_EXCEPTION;
    }

  container = lv_obj_create(g_qpk.root);
  if (container == NULL)
    {
      JS_FreeCString(context, text);
      return JS_ThrowInternalError(context, "cannot create number");
    }

  lv_obj_set_pos(container, qpk_arg_int(context, argc, argv, 1, 0),
                 qpk_arg_int(context, argc, argv, 2, 0));
  lv_obj_set_size(container, qpk_arg_int(context, argc, argv, 3, 64),
                  qpk_arg_int(context, argc, argv, 4, 64));
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_remove_flag(container, LV_OBJ_FLAG_CLICKABLE |
                             LV_OBJ_FLAG_SCROLLABLE);

  label = lv_label_create(container);
  if (label == NULL)
    {
      lv_obj_delete(container);
      JS_FreeCString(context, text);
      return JS_ThrowOutOfMemory(context);
    }

  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label,
      lv_color_hex(qpk_arg_color(context, argc, argv, 5, 0xffffff)), 0);
  if (g_qpk.font_cb != NULL)
    {
      lv_obj_set_style_text_font(label, g_qpk.font_cb(32), 0);
    }

  qpk_number_style(container, text);
  handle = qpk_add_widget(container, QPK_WIDGET_NUMBER);
  JS_FreeCString(context, text);
  if (handle == 0)
    {
      lv_obj_delete(container);
      return JS_ThrowInternalError(context, "too many widgets");
    }

  return JS_NewInt32(context, handle);
}

static JSValue js_ui_set_text(JSContext *context,
                              JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  int handle;
  const char *text;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  text = qpk_arg_string(context, argc, argv, 1);
  if (text == NULL)
    {
      return JS_EXCEPTION;
    }

  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      JS_FreeCString(context, text);
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  if (g_qpk.widget_types[handle - 1] == QPK_WIDGET_BUTTON)
    {
      lv_obj_t *label = lv_obj_get_child(g_qpk.widgets[handle - 1], 0);

      if (label == NULL)
        {
          JS_FreeCString(context, text);
          return JS_ThrowInternalError(context, "button label is missing");
        }

      lv_label_set_text(label, text);
    }
  else if (g_qpk.widget_types[handle - 1] == QPK_WIDGET_NUMBER)
    {
      qpk_number_style(g_qpk.widgets[handle - 1], text);
    }
  else if (g_qpk.widget_types[handle - 1] == QPK_WIDGET_LABEL)
    {
      lv_label_set_text(g_qpk.widgets[handle - 1], text);
    }
  else
    {
      JS_FreeCString(context, text);
      return JS_ThrowTypeError(context, "widget does not contain text");
    }
  JS_FreeCString(context, text);
  return JS_UNDEFINED;
}

static JSValue js_ui_set_hidden(JSContext *context, JSValueConst this_value,
                                int argc, JSValueConst *argv)
{
  int handle;
  int hidden;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  hidden = qpk_arg_int(context, argc, argv, 1, 0);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  if (hidden)
    {
      lv_obj_add_flag(g_qpk.widgets[handle - 1], LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_remove_flag(g_qpk.widgets[handle - 1], LV_OBJ_FLAG_HIDDEN);
    }

  return JS_UNDEFINED;
}

static JSValue js_ui_background(JSContext *context,
                                JSValueConst this_value,
                                int argc, JSValueConst *argv)
{
  (void)this_value;
  lv_obj_set_style_bg_color(g_qpk.root,
      lv_color_hex(qpk_arg_color(context, argc, argv, 0, 0xffffff)), 0);
  lv_obj_set_style_bg_opa(g_qpk.root, LV_OPA_COVER, 0);
  return JS_UNDEFINED;
}

static JSValue js_ui_get_size(JSContext *context, JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  JSValue size;

  (void)this_value;
  (void)argc;
  (void)argv;
  lv_obj_update_layout(g_qpk.root);
  size = JS_NewObject(context);
  JS_SetPropertyStr(context, size, "width",
                    JS_NewInt32(context, lv_obj_get_width(g_qpk.root)));
  JS_SetPropertyStr(context, size, "height",
                    JS_NewInt32(context, lv_obj_get_height(g_qpk.root)));
  return size;
}

static JSValue js_ui_set_color(JSContext *context, JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  int handle;
  uint32_t color;
  lv_obj_t *widget;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  color = qpk_arg_color(context, argc, argv, 1, 0xffffff);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  widget = g_qpk.widgets[handle - 1];
  if (g_qpk.widget_types[handle - 1] == QPK_WIDGET_LABEL ||
      g_qpk.widget_types[handle - 1] == QPK_WIDGET_NUMBER)
    {
      lv_obj_t *text = g_qpk.widget_types[handle - 1] == QPK_WIDGET_NUMBER ?
                       lv_obj_get_child(widget, 0) : widget;
      if (text == NULL)
        {
          return JS_ThrowInternalError(context, "widget text is missing");
        }

      lv_obj_set_style_text_color(text, lv_color_hex(color), 0);
    }
  else
    {
      lv_obj_set_style_bg_color(widget, lv_color_hex(color), 0);
    }

  return JS_UNDEFINED;
}

static JSValue js_ui_panel(JSContext *context, JSValueConst this_value,
                           int argc, JSValueConst *argv)
{
  lv_obj_t *panel;
  int handle;
  int opacity;

  (void)this_value;
  panel = lv_obj_create(g_qpk.root);
  if (panel == NULL)
    {
      return JS_ThrowInternalError(context, "cannot create panel");
    }

  lv_obj_set_pos(panel, qpk_arg_int(context, argc, argv, 0, 0),
                 qpk_arg_int(context, argc, argv, 1, 0));
  lv_obj_set_size(panel, qpk_arg_int(context, argc, argv, 2, 100),
                  qpk_arg_int(context, argc, argv, 3, 100));
  lv_obj_set_style_bg_color(panel,
      lv_color_hex(qpk_arg_color(context, argc, argv, 4, 0xffffff)), 0);
  opacity = qpk_arg_int(context, argc, argv, 6, LV_OPA_COVER);
  lv_obj_set_style_bg_opa(panel, opacity, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x344144), 0);
  lv_obj_set_style_radius(panel, 8, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  handle = qpk_add_widget(panel, QPK_WIDGET_PANEL);
  if (handle == 0)
    {
      lv_obj_delete(panel);
      return JS_ThrowInternalError(context, "too many widgets");
    }

  return JS_NewInt32(context, handle);
}

static JSValue js_ui_set_pos(JSContext *context, JSValueConst this_value,
                             int argc, JSValueConst *argv)
{
  int handle;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  lv_obj_set_pos(g_qpk.widgets[handle - 1],
                 qpk_arg_int(context, argc, argv, 1, 0),
                 qpk_arg_int(context, argc, argv, 2, 0));
  return JS_UNDEFINED;
}

static JSValue js_ui_set_size(JSContext *context, JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  int handle;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  lv_obj_set_size(g_qpk.widgets[handle - 1],
                  qpk_arg_int(context, argc, argv, 1, 0),
                  qpk_arg_int(context, argc, argv, 2, 0));
  return JS_UNDEFINED;
}

static JSValue js_ui_set_opacity(JSContext *context,
                                 JSValueConst this_value,
                                 int argc, JSValueConst *argv)
{
  int handle;
  int opacity;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  opacity = qpk_arg_int(context, argc, argv, 1, LV_OPA_COVER);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  if (opacity < LV_OPA_TRANSP)
    {
      opacity = LV_OPA_TRANSP;
    }
  else if (opacity > LV_OPA_COVER)
    {
      opacity = LV_OPA_COVER;
    }

  lv_obj_set_style_opa(g_qpk.widgets[handle - 1], (lv_opa_t)opacity, 0);
  return JS_UNDEFINED;
}

static JSValue js_ui_show(JSContext *context, JSValueConst this_value,
                          int argc, JSValueConst *argv)
{
  int handle;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  lv_obj_remove_flag(g_qpk.widgets[handle - 1], LV_OBJ_FLAG_HIDDEN);
  return JS_UNDEFINED;
}

static JSValue js_ui_hide(JSContext *context, JSValueConst this_value,
                          int argc, JSValueConst *argv)
{
  int handle;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  lv_obj_add_flag(g_qpk.widgets[handle - 1], LV_OBJ_FLAG_HIDDEN);
  return JS_UNDEFINED;
}

static JSValue js_ui_remove(JSContext *context, JSValueConst this_value,
                            int argc, JSValueConst *argv)
{
  int handle;

  (void)this_value;
  handle = qpk_arg_int(context, argc, argv, 0, 0);
  if (handle <= 0 || handle > QPK_MAX_WIDGETS ||
      g_qpk.widgets[handle - 1] == NULL)
    {
      return JS_ThrowRangeError(context, "invalid widget handle");
    }

  lv_obj_delete(g_qpk.widgets[handle - 1]);
  g_qpk.widgets[handle - 1] = NULL;
  return JS_UNDEFINED;
}

static JSValue js_ui_rect(JSContext *context, JSValueConst this_value,
                          int argc, JSValueConst *argv)
{
  lv_obj_t *rect;
  int handle;

  (void)this_value;
  rect = lv_obj_create(g_qpk.root);
  if (rect == NULL)
    {
      return JS_ThrowInternalError(context, "cannot create rect");
    }

  lv_obj_remove_style_all(rect);
  lv_obj_set_pos(rect, qpk_arg_int(context, argc, argv, 0, 0),
                 qpk_arg_int(context, argc, argv, 1, 0));
  lv_obj_set_size(rect, qpk_arg_int(context, argc, argv, 2, 100),
                  qpk_arg_int(context, argc, argv, 3, 100));
  lv_obj_set_style_bg_color(rect,
      lv_color_hex(qpk_arg_color(context, argc, argv, 4, 0x1a1a2e)), 0);
  lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rect, 0, 0);
  lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
  handle = qpk_add_widget(rect, QPK_WIDGET_RECT);
  if (handle == 0)
    {
      lv_obj_delete(rect);
      return JS_ThrowInternalError(context, "too many widgets");
    }

  return JS_NewInt32(context, handle);
}

static void qpk_input_finish(bool submit)
{
  JSContext *context = g_qpk.context;
  JSValue callback;
  JSValue argument = JS_UNDEFINED;
  JSValue result;

  if (context == NULL || g_qpk.input_shade == NULL)
    {
      return;
    }

  callback = JS_DupValue(context, g_qpk.input_callback);
  if (submit)
    {
      argument = JS_NewString(context,
                              lv_textarea_get_text(g_qpk.input_textarea));
    }

  JS_FreeValue(context, g_qpk.input_callback);
  g_qpk.input_callback = JS_UNDEFINED;
  lv_obj_delete(g_qpk.input_shade);
  g_qpk.input_shade = NULL;
  g_qpk.input_textarea = NULL;

  if (submit)
    {
      result = qpk_call_args(callback, QPK_EVENT_BUDGET, 1, &argument);
      JS_FreeValue(context, result);
      JS_FreeValue(context, argument);
    }

  JS_FreeValue(context, callback);
}

static void qpk_input_cancel(lv_event_t *event)
{
  (void)event;
  qpk_input_finish(false);
}

static void qpk_input_submit(lv_event_t *event)
{
  (void)event;
  qpk_input_finish(true);
}

static lv_obj_t *qpk_input_button(lv_obj_t *parent, const char *text,
                                  int x, uint32_t color,
                                  lv_event_cb_t callback)
{
  lv_obj_t *button;
  lv_obj_t *label;

  button = lv_button_create(parent);
  if (button == NULL)
    {
      return NULL;
    }

  lv_obj_set_pos(button, x, 16);
  lv_obj_set_size(button, 92, 44);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x3b484b), 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(button);
  if (label == NULL)
    {
      lv_obj_delete(button);
      return NULL;
    }

  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
  if (g_qpk.font_cb != NULL)
    {
      lv_obj_set_style_text_font(label, g_qpk.font_cb(20), 0);
    }

  lv_obj_center(label);
  return button;
}

static JSValue js_prompt_input(JSContext *context,
                               JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  JSValue title_value = JS_UNDEFINED;
  JSValue placeholder_value = JS_UNDEFINED;
  JSValue text_value = JS_UNDEFINED;
  JSValue max_value = JS_UNDEFINED;
  JSValue password_value = JS_UNDEFINED;
  const char *title = "输入";
  const char *placeholder = "请输入内容";
  const char *text = "";
  const char *converted_title = NULL;
  const char *converted_placeholder = NULL;
  const char *converted_text = NULL;
  lv_obj_t *box;
  lv_obj_t *label;
  lv_obj_t *keyboard;
  int32_t max_length = 64;
  bool password = false;
  int root_width;
  int root_height;

  (void)this_value;
  if (argc < 2 || !JS_IsObject(argv[0]) ||
      !JS_IsFunction(context, argv[1]))
    {
      return JS_ThrowTypeError(context,
                               "prompt.input requires options and callback");
    }

  if (g_qpk.input_shade != NULL)
    {
      return JS_ThrowInternalError(context, "an input dialog is already open");
    }

  title_value = JS_GetPropertyStr(context, argv[0], "title");
  placeholder_value = JS_GetPropertyStr(context, argv[0], "placeholder");
  text_value = JS_GetPropertyStr(context, argv[0], "value");
  max_value = JS_GetPropertyStr(context, argv[0], "maxLength");
  password_value = JS_GetPropertyStr(context, argv[0], "password");
  if (!JS_IsUndefined(title_value) && !JS_IsNull(title_value))
    {
      converted_title = JS_ToCString(context, title_value);
      if (converted_title != NULL)
        {
          title = converted_title;
        }
    }

  if (!JS_IsUndefined(placeholder_value) && !JS_IsNull(placeholder_value))
    {
      converted_placeholder = JS_ToCString(context, placeholder_value);
      if (converted_placeholder != NULL)
        {
          placeholder = converted_placeholder;
        }
    }

  if (!JS_IsUndefined(text_value) && !JS_IsNull(text_value))
    {
      converted_text = JS_ToCString(context, text_value);
      if (converted_text != NULL)
        {
          text = converted_text;
        }
    }

  if (!JS_IsUndefined(max_value))
    {
      (void)JS_ToInt32(context, &max_length, max_value);
    }

  password = JS_ToBool(context, password_value) > 0;

  if (max_length < 1)
    {
      max_length = 1;
    }
  else if (max_length > 2048)
    {
      max_length = 2048;
    }

  lv_obj_update_layout(g_qpk.root);
  root_width = lv_obj_get_width(g_qpk.root);
  root_height = lv_obj_get_height(g_qpk.root);
  g_qpk.input_shade = lv_obj_create(g_qpk.root);
  if (g_qpk.input_shade == NULL)
    {
      goto err_input;
    }

  lv_obj_set_size(g_qpk.input_shade, root_width, root_height);
  lv_obj_set_style_bg_color(g_qpk.input_shade, lv_color_hex(0x02090d), 0);
  lv_obj_set_style_bg_opa(g_qpk.input_shade, LV_OPA_80, 0);
  lv_obj_set_style_border_width(g_qpk.input_shade, 0, 0);
  lv_obj_set_style_pad_all(g_qpk.input_shade, 0, 0);
  lv_obj_remove_flag(g_qpk.input_shade, LV_OBJ_FLAG_SCROLLABLE);

  box = lv_obj_create(g_qpk.input_shade);
  if (box == NULL)
    {
      goto err_input;
    }

  lv_obj_set_size(box, root_width, root_height);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x102129), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  label = lv_label_create(box);
  if (label == NULL)
    {
      goto err_input;
    }

  lv_label_set_text(label, title);
  lv_obj_set_pos(label, 24, 22);
  lv_obj_set_style_text_color(label, lv_color_hex(0xf4fbfc), 0);
  if (g_qpk.font_cb != NULL)
    {
      lv_obj_set_style_text_font(label, g_qpk.font_cb(28), 0);
    }

  if (qpk_input_button(box, "取消", root_width - 208, 0x40515a,
                       qpk_input_cancel) == NULL ||
      qpk_input_button(box, "确定", root_width - 108, 0x16758a,
                       qpk_input_submit) == NULL)
    {
      goto err_input;
    }

  g_qpk.input_textarea = lv_textarea_create(box);
  if (g_qpk.input_textarea == NULL)
    {
      goto err_input;
    }

  lv_obj_set_pos(g_qpk.input_textarea, 24, 76);
  lv_obj_set_size(g_qpk.input_textarea, root_width - 48, 52);
  lv_textarea_set_one_line(g_qpk.input_textarea, true);
  lv_obj_set_style_bg_color(g_qpk.input_textarea,
                            lv_color_hex(0x252c2f), 0);
  lv_obj_set_style_border_width(g_qpk.input_textarea, 1, 0);
  lv_obj_set_style_border_color(g_qpk.input_textarea,
                                lv_color_hex(0x54d3a4), 0);
  lv_obj_set_style_radius(g_qpk.input_textarea, 8, 0);
  lv_textarea_set_max_length(g_qpk.input_textarea, max_length);
  lv_textarea_set_placeholder_text(g_qpk.input_textarea, placeholder);
  lv_textarea_set_text(g_qpk.input_textarea, text);
  lv_textarea_set_password_mode(g_qpk.input_textarea, password);
  if (g_qpk.font_cb != NULL)
    {
      lv_obj_set_style_text_font(g_qpk.input_textarea,
                                 g_qpk.font_cb(20), 0);
    }

  keyboard = lv_keyboard_create(g_qpk.input_shade);
  if (keyboard == NULL)
    {
      goto err_input;
    }

  lv_obj_set_size(keyboard, lv_pct(100), 255);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_keyboard_set_textarea(keyboard, g_qpk.input_textarea);
  lv_obj_add_event_cb(keyboard, qpk_input_submit, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(keyboard, qpk_input_cancel, LV_EVENT_CANCEL, NULL);
  g_qpk.input_callback = JS_DupValue(context, argv[1]);
  lv_obj_move_foreground(g_qpk.input_shade);

  if (converted_title != NULL)
    {
      JS_FreeCString(context, converted_title);
    }
  if (converted_placeholder != NULL)
    {
      JS_FreeCString(context, converted_placeholder);
    }
  if (converted_text != NULL)
    {
      JS_FreeCString(context, converted_text);
    }
  JS_FreeValue(context, title_value);
  JS_FreeValue(context, placeholder_value);
  JS_FreeValue(context, text_value);
  JS_FreeValue(context, max_value);
  JS_FreeValue(context, password_value);
  return JS_UNDEFINED;

err_input:
  if (g_qpk.input_shade != NULL)
    {
      lv_obj_delete(g_qpk.input_shade);
      g_qpk.input_shade = NULL;
      g_qpk.input_textarea = NULL;
    }

  if (converted_title != NULL)
    {
      JS_FreeCString(context, converted_title);
    }

  if (converted_placeholder != NULL)
    {
      JS_FreeCString(context, converted_placeholder);
    }

  if (converted_text != NULL)
    {
      JS_FreeCString(context, converted_text);
    }

  JS_FreeValue(context, title_value);
  JS_FreeValue(context, placeholder_value);
  JS_FreeValue(context, text_value);
  JS_FreeValue(context, max_value);
  JS_FreeValue(context, password_value);
  return JS_ThrowOutOfMemory(context);
}

static bool qpk_storage_component_valid(const char *text, size_t max_len)
{
  const char *p;
  size_t length;

  if (text == NULL)
    {
      return false;
    }

  length = strlen(text);
  if (length == 0 || length > max_len)
    {
      return false;
    }

  for (p = text; *p != '\0'; p++)
    {
      if (!(('a' <= *p && *p <= 'z') || ('A' <= *p && *p <= 'Z') ||
            ('0' <= *p && *p <= '9') || *p == '_' || *p == '-'))
        {
          return false;
        }
    }

  return true;
}

static int qpk_storage_package(char *encoded, size_t encoded_len)
{
  static const char hex[] = "0123456789abcdef";
  const char *source;
  size_t index = 0;

  if (g_qpk.package[0] == '\0')
    {
      return -EINVAL;
    }

  for (source = g_qpk.package; *source != '\0'; source++)
    {
      char value = *source;

      if (!(('a' <= value && value <= 'z') ||
            ('A' <= value && value <= 'Z') ||
            ('0' <= value && value <= '9') ||
            value == '_' || value == '-' || value == '.'))
        {
          return -EINVAL;
        }

      if (index + 2 >= encoded_len)
        {
          return -ENAMETOOLONG;
        }

      encoded[index++] = hex[((uint8_t)value >> 4) & 0x0f];
      encoded[index++] = hex[(uint8_t)value & 0x0f];
    }

  encoded[index] = '\0';
  return OK;
}

static int qpk_storage_mkdir(const char *path)
{
  struct stat info;

  if (mkdir(path, 0777) == OK)
    {
      return OK;
    }

  if (errno != EEXIST || stat(path, &info) < 0 || !S_ISDIR(info.st_mode))
    {
      return -errno;
    }

  return OK;
}

static int qpk_storage_path(const char *key, char *path, size_t path_len)
{
  char package[sizeof(g_qpk.package) * 2 + 1];
  int length;
  int ret;

  if (!qpk_storage_component_valid(key, QPK_STORAGE_KEY_MAX))
    {
      return -EINVAL;
    }

  ret = qpk_storage_package(package, sizeof(package));
  if (ret < 0)
    {
      return ret;
    }

  length = snprintf(path, path_len, "%s/%s_%s.txt", QPK_STORAGE_ROOT,
                    package, key);
  return length < 0 || (size_t)length >= path_len ? -ENAMETOOLONG : 0;
}

static int qpk_storage_prepare(void)
{
  int ret;

  ret = qpk_storage_mkdir(QPK_DIR);
  if (ret < 0)
    {
      return ret;
    }

  return qpk_storage_mkdir(QPK_STORAGE_ROOT);
}

static JSValue js_storage_get(JSContext *context, JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  char path[QPK_STORAGE_PATH_MAX];
  char *buffer;
  const char *key;
  FILE *stream;
  size_t length;
  int ret;

  (void)this_value;
  key = qpk_arg_string(context, argc, argv, 0);
  if (key == NULL)
    {
      return JS_EXCEPTION;
    }

  ret = qpk_storage_path(key, path, sizeof(path));
  JS_FreeCString(context, key);
  if (ret < 0)
    {
      return JS_ThrowRangeError(context, "invalid storage key");
    }

  buffer = malloc(QPK_STORAGE_VALUE_MAX + 1);
  if (buffer == NULL)
    {
      return JS_ThrowOutOfMemory(context);
    }

  stream = fopen(path, "rb");
  if (stream == NULL)
    {
      ret = errno;
      free(buffer);
      return ret == ENOENT ? JS_NULL :
             JS_ThrowInternalError(context, "storage unavailable: %d", ret);
    }

  length = fread(buffer, 1, QPK_STORAGE_VALUE_MAX + 1, stream);
  ret = ferror(stream) ? errno : 0;
  if (fclose(stream) != 0 && ret == 0)
    {
      ret = errno == 0 ? EIO : errno;
    }
  if (ret != 0 || length > QPK_STORAGE_VALUE_MAX)
    {
      free(buffer);
      return JS_ThrowInternalError(context, "storage read failed");
    }

  {
    JSValue result = JS_NewStringLen(context, buffer, length);
    free(buffer);
    return result;
  }
}

static JSValue js_storage_set(JSContext *context, JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  char path[QPK_STORAGE_PATH_MAX];
  char temporary[QPK_STORAGE_PATH_MAX + 5];
  const char *key;
  const char *value;
  FILE *stream;
  size_t length;
  int ret;

  (void)this_value;
  if (argc < 2)
    {
      return JS_ThrowTypeError(context, "storage.set requires key and value");
    }

  key = JS_ToCString(context, argv[0]);
  value = JS_ToCStringLen(context, &length, argv[1]);
  if (key == NULL || value == NULL)
    {
      JS_FreeCString(context, key);
      JS_FreeCString(context, value);
      return JS_EXCEPTION;
    }

  ret = qpk_storage_path(key, path, sizeof(path));
  JS_FreeCString(context, key);
  if (ret < 0 || length > QPK_STORAGE_VALUE_MAX)
    {
      JS_FreeCString(context, value);
      return JS_ThrowRangeError(context, "invalid storage key or value");
    }

  ret = qpk_storage_prepare();
  if (ret == 0)
    {
      int length_tmp = snprintf(temporary, sizeof(temporary), "%s.tmp", path);

      if (length_tmp < 0 || (size_t)length_tmp >= sizeof(temporary))
        {
          ret = -ENAMETOOLONG;
        }
    }

  if (ret == 0)
    {
      stream = fopen(temporary, "wb");
      if (stream == NULL || fwrite(value, 1, length, stream) != length)
        {
          ret = stream == NULL ? -errno : -EIO;
        }
      if (stream != NULL)
        {
          if (fclose(stream) != 0 && ret == 0)
            {
              ret = errno == 0 ? -EIO : -errno;
            }
        }
      if (ret == 0 && rename(temporary, path) < 0)
        {
          ret = -errno;
        }
      if (ret < 0)
        {
          unlink(temporary);
        }
    }

  JS_FreeCString(context, value);
  return ret < 0 ? JS_ThrowInternalError(context, "storage write failed") :
                   JS_UNDEFINED;
}

static JSValue js_storage_delete(JSContext *context, JSValueConst this_value,
                                 int argc, JSValueConst *argv)
{
  char path[QPK_STORAGE_PATH_MAX];
  const char *key;
  int ret;

  (void)this_value;
  key = qpk_arg_string(context, argc, argv, 0);
  if (key == NULL)
    {
      return JS_EXCEPTION;
    }

  ret = qpk_storage_path(key, path, sizeof(path));
  JS_FreeCString(context, key);
  if (ret < 0)
    {
      return JS_ThrowRangeError(context, "invalid storage key");
    }

  ret = unlink(path);
  return ret < 0 && errno != ENOENT ?
         JS_ThrowInternalError(context, "storage delete failed") :
         JS_UNDEFINED;
}

static JSValue js_ui_button(JSContext *context, JSValueConst this_value,
                            int argc, JSValueConst *argv)
{
  const char *text;
  lv_obj_t *button;
  lv_obj_t *label;
  struct qpk_event_s *binding = NULL;
  int handle;
  int i;
  uint32_t button_color;
  uint32_t text_color;
  unsigned int brightness;

  (void)this_value;
  text = qpk_arg_string(context, argc, argv, 0);
  if (text == NULL)
    {
      return JS_EXCEPTION;
    }

  if (argc < 6 || !JS_IsFunction(context, argv[5]))
    {
      JS_FreeCString(context, text);
      return JS_ThrowTypeError(context, "button handler must be a function");
    }

  for (i = 0; i < QPK_MAX_EVENTS; i++)
    {
      if (!g_qpk.events[i].used)
        {
          binding = &g_qpk.events[i];
          break;
        }
    }

  if (binding == NULL)
    {
      JS_FreeCString(context, text);
      return JS_ThrowInternalError(context, "too many event handlers");
    }

  button = lv_button_create(g_qpk.root);
  if (button == NULL)
    {
      JS_FreeCString(context, text);
      return JS_ThrowOutOfMemory(context);
    }

  lv_obj_set_pos(button, qpk_arg_int(context, argc, argv, 1, 220),
                 qpk_arg_int(context, argc, argv, 2, 160));
  lv_obj_set_size(button, qpk_arg_int(context, argc, argv, 3, 300),
                  qpk_arg_int(context, argc, argv, 4, 54));
  button_color = qpk_arg_color(context, argc, argv, 6, 0x6677f5);
  brightness = ((button_color >> 16) & 0xff) +
               ((button_color >> 8) & 0xff) + (button_color & 0xff);
  text_color = brightness > 510 ? 0x172033 : 0xffffff;
  lv_obj_set_style_bg_color(button, lv_color_hex(button_color), 0);
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
  label = lv_label_create(button);
  if (label == NULL)
    {
      lv_obj_delete(button);
      JS_FreeCString(context, text);
      return JS_ThrowOutOfMemory(context);
    }

  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(text_color), 0);
  if (g_qpk.font_cb != NULL)
    {
      lv_obj_set_style_text_font(label, g_qpk.font_cb(20), 0);
    }

  lv_obj_center(label);
  binding->function = JS_DupValue(context, argv[5]);
  binding->used = true;
  lv_obj_add_event_cb(button, qpk_event_clicked, LV_EVENT_CLICKED, binding);
  handle = qpk_add_widget(button, QPK_WIDGET_BUTTON);
  JS_FreeCString(context, text);
  if (handle == 0)
    {
      JS_FreeValue(context, binding->function);
      binding->used = false;
      lv_obj_delete(button);
      return JS_ThrowInternalError(context, "too many widgets");
    }

  return JS_NewInt32(context, handle);
}

static const char *qpk_message_arg(JSContext *context, int argc,
                                   JSValueConst *argv, JSValue *holder)
{
  if (argc < 1)
    {
      return NULL;
    }

  if (JS_IsObject(argv[0]))
    {
      *holder = JS_GetPropertyStr(context, argv[0], "message");
      return JS_ToCString(context, *holder);
    }

  *holder = JS_UNDEFINED;
  return JS_ToCString(context, argv[0]);
}

static JSValue js_prompt_toast(JSContext *context,
                               JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  JSValue holder;
  const char *message;

  (void)this_value;
  message = qpk_message_arg(context, argc, argv, &holder);
  if (message == NULL)
    {
      return JS_EXCEPTION;
    }

  if (g_qpk.toast_cb != NULL)
    {
      g_qpk.toast_cb(message);
    }

  JS_FreeCString(context, message);
  JS_FreeValue(context, holder);
  return JS_UNDEFINED;
}

static JSValue js_prompt_dialog(JSContext *context,
                                JSValueConst this_value,
                                int argc, JSValueConst *argv)
{
  JSValue title_value = JS_UNDEFINED;
  JSValue message_value = JS_UNDEFINED;
  const char *title = NULL;
  const char *message = NULL;
  char text[256];

  (void)this_value;
  if (argc > 0 && JS_IsObject(argv[0]))
    {
      title_value = JS_GetPropertyStr(context, argv[0], "title");
      message_value = JS_GetPropertyStr(context, argv[0], "message");
      title = JS_ToCString(context, title_value);
      message = JS_ToCString(context, message_value);
    }
  else if (argc > 0)
    {
      message = JS_ToCString(context, argv[0]);
    }

  snprintf(text, sizeof(text), "%s%s%s", title ? title : "快应用",
           message ? "\n" : "", message ? message : "");
  if (g_qpk.dialog_cb != NULL)
    {
      g_qpk.dialog_cb(text);
    }

  if (title != NULL)
    {
      JS_FreeCString(context, title);
    }

  if (message != NULL)
    {
      JS_FreeCString(context, message);
    }

  JS_FreeValue(context, title_value);
  JS_FreeValue(context, message_value);
  return JS_UNDEFINED;
}

static void qpk_timer_cb(lv_timer_t *timer)
{
  struct qpk_timer_s *binding = lv_timer_get_user_data(timer);
  JSValue result;

  if (g_qpk.context == NULL || binding == NULL || !binding->used)
    {
      return;
    }

  result = qpk_call(binding->function, QPK_EVENT_BUDGET);
  if (JS_IsException(result))
    {
      JS_FreeValue(g_qpk.context, binding->function);
      binding->used = false;
      binding->timer = NULL;
      lv_timer_delete(timer);
    }

  JS_FreeValue(g_qpk.context, result);
}

static JSValue js_set_interval(JSContext *context,
                               JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  struct qpk_timer_s *binding = NULL;
  int interval;
  int i;

  (void)this_value;
  if (argc < 1 || !JS_IsFunction(context, argv[0]))
    {
      return JS_ThrowTypeError(context, "callback must be a function");
    }

  interval = qpk_arg_int(context, argc, argv, 1, 1000);
  if (interval < 20)
    {
      interval = 20;
    }

  for (i = 0; i < QPK_MAX_TIMERS; i++)
    {
      if (!g_qpk.timers[i].used)
        {
          binding = &g_qpk.timers[i];
          break;
        }
    }

  if (binding == NULL)
    {
      return JS_ThrowInternalError(context, "too many timers");
    }

  binding->id = ++g_qpk.next_timer_id;
  binding->function = JS_DupValue(context, argv[0]);
  binding->used = true;
  binding->timer = lv_timer_create(qpk_timer_cb, interval, binding);
  if (binding->timer == NULL)
    {
      JS_FreeValue(context, binding->function);
      binding->used = false;
      return JS_ThrowInternalError(context, "cannot create timer");
    }

  return JS_NewInt32(context, binding->id);
}

static JSValue js_clear_interval(JSContext *context,
                                 JSValueConst this_value,
                                 int argc, JSValueConst *argv)
{
  int id;
  int i;

  (void)this_value;
  id = qpk_arg_int(context, argc, argv, 0, 0);
  for (i = 0; i < QPK_MAX_TIMERS; i++)
    {
      struct qpk_timer_s *binding = &g_qpk.timers[i];

      if (binding->used && binding->id == id)
        {
          lv_timer_delete(binding->timer);
          JS_FreeValue(context, binding->function);
          binding->timer = NULL;
          binding->used = false;
          break;
        }
    }

  return JS_UNDEFINED;
}

static void qpk_camera_stop(void)
{
  struct qpk_camera_s *camera = &g_qpk.camera;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  unsigned int i;

  if (camera->timer != NULL)
    {
      lv_timer_delete(camera->timer);
      camera->timer = NULL;
    }

  camera->stop_requested = true;
  if (camera->streaming)
    {
      ioctl(camera->fd, VIDIOC_STREAMOFF, (uintptr_t)&type);
      camera->streaming = false;
    }

  if (camera->thread_running)
    {
      pthread_join(camera->thread, NULL);
      camera->thread_running = false;
    }

  for (i = 0; camera->memory_type == V4L2_MEMORY_MMAP &&
              i < camera->mmap_count; i++)
    {
      if (camera->mmap_buffers[i] != NULL &&
          camera->mmap_buffers[i] != MAP_FAILED)
        {
          munmap(camera->mmap_buffers[i], camera->mmap_lengths[i]);
        }
    }

  if (camera->fb_fd >= 0)
    {
      close(camera->fb_fd);
      camera->fb_fd = -1;
    }

  if (camera->opened)
    {
      close(camera->fd);
      camera->opened = false;
    }

  if (camera->canvas != NULL)
    {
      lv_obj_delete(camera->canvas);
      camera->canvas = NULL;
    }

  free(camera->raw);
  free(camera->rgb565[0]);
  free(camera->rgb565[1]);
  if (camera->lock_initialized)
    {
      pthread_mutex_destroy(&camera->lock);
    }

  memset(camera, 0, sizeof(*camera));
  camera->fd = -1;
  camera->fb_fd = -1;
}

static void qpk_camera_profile(FAR struct qpk_camera_s *camera)
{
  uint64_t elapsed;
  uint32_t fps_x10;
  uint32_t converted;
  uint32_t samples;

  if (camera->profile_samples < QPK_CAMERA_PROFILE_FRAMES)
    {
      return;
    }

  elapsed = qpk_now_us() - camera->profile_started_us;
  samples = camera->profile_samples;
  converted = camera->profile_converted > 0 ?
              camera->profile_converted : 1;
  fps_x10 = elapsed > 0 ?
            (uint32_t)((uint64_t)samples * 10000000 / elapsed) : 0;
  printf("[qpk] camera stages: capture=%lu.%lu fps "
         "dq=%lu us invalidate=%lu us copy=%lu us clean=%lu us "
         "qbuf=%lu us "
         "converted=%lu/%lu\n",
         (unsigned long)(fps_x10 / 10),
         (unsigned long)(fps_x10 % 10),
         (unsigned long)(camera->profile_dq_us / samples),
         (unsigned long)(camera->profile_invalidate_us / samples),
         (unsigned long)(camera->profile_convert_us / converted),
         (unsigned long)(camera->profile_clean_us / converted),
         (unsigned long)(camera->profile_qbuf_us / samples),
         (unsigned long)camera->profile_converted,
         (unsigned long)samples);

  camera->profile_started_us = qpk_now_us();
  camera->profile_dq_us = 0;
  camera->profile_invalidate_us = 0;
  camera->profile_convert_us = 0;
  camera->profile_clean_us = 0;
  camera->profile_qbuf_us = 0;
  camera->profile_samples = 0;
  camera->profile_converted = 0;
}

static void *qpk_camera_thread(pthread_addr_t arg)
{
  struct qpk_camera_s *camera = (struct qpk_camera_s *)arg;
  struct v4l2_buffer buffer;
  struct mallinfo memory;
  FAR uint8_t *source;
  FAR uint8_t *target;
  FAR uint8_t *released;
  struct pollfd pfd;
  size_t page_size = 0;
  uint64_t started;
  uintptr_t target_offset;
  unsigned int dq_failures = 0;
  unsigned int next_page;
  unsigned int old_page = 0;
  unsigned int pan_failures = 0;
  int fatal_error = OK;
  int pan_error;
  int ret;

  pfd.fd = camera->fd;
  pfd.events = POLLIN;
  while (!camera->stop_requested)
    {
      ret = poll(&pfd, 1, 100);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          fatal_error = -errno;
          printf("[qpk] camera poll failed: %d\n", errno);
          break;
        }

      if (ret == 0)
        {
          continue;
        }

      memset(&buffer, 0, sizeof(buffer));
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = camera->memory_type;
      started = qpk_now_us();
      ret = ioctl(camera->fd, VIDIOC_DQBUF, (uintptr_t)&buffer);
      if (ret < 0)
        {
          if (errno != EAGAIN && errno != EINTR)
            {
              camera->dq_errors++;
              dq_failures++;
              printf("[qpk] camera DQBUF failed: %d (%u/%u)\n",
                     errno, dq_failures, QPK_CAMERA_MAX_DQ_ERRORS);
              if (dq_failures >= QPK_CAMERA_MAX_DQ_ERRORS)
                {
                  fatal_error = -errno;
                  break;
                }
            }

          continue;
        }

      dq_failures = 0;
      camera->profile_dq_us += qpk_now_us() - started;
      camera->captured_frames++;
      camera->profile_samples++;
      if (camera->profile_started_us == 0)
        {
          camera->profile_started_us = started;
        }

      if (buffer.index >= camera->mmap_count)
        {
          printf("[qpk] camera returned invalid buffer %lu\n",
                 (unsigned long)buffer.index);
          fatal_error = -EIO;
          break;
        }

      if (camera->memory_type == V4L2_MEMORY_USERPTR)
        {
          if (buffer.m.userptr !=
              (uintptr_t)camera->mmap_buffers[buffer.index])
            {
              printf("[qpk] camera returned unexpected USERPTR %p\n",
                     (FAR void *)buffer.m.userptr);
              fatal_error = -EIO;
              break;
            }

          target = camera->mmap_buffers[buffer.index];
          page_size = QPK_CAMERA_RAW_HEIGHT * camera->fb_plane.stride;
          target_offset = (uintptr_t)target -
                          (uintptr_t)camera->fb_plane.fbmem;
          if (target_offset % page_size != 0 ||
              target_offset / page_size >= QPK_CAMERA_FRAMEBUFFER_COUNT)
            {
              printf("[qpk] camera USERPTR outside display pages: %p\n",
                     target);
              fatal_error = -EIO;
              break;
            }

          old_page = camera->fb_page;
          next_page = target_offset / page_size;
          if (next_page == old_page)
            {
              printf("[qpk] camera attempted capture into active page %u\n",
                     next_page);
              fatal_error = -EBUSY;
              break;
            }
        }
      else
        {
          source = camera->mmap_buffers[buffer.index];
          started = qpk_now_us();
          up_invalidate_dcache((uintptr_t)source,
                               (uintptr_t)source + QPK_CAMERA_FRAME_SIZE);
          camera->profile_invalidate_us += qpk_now_us() - started;

          next_page = 1 - camera->fb_page;
          target = (FAR uint8_t *)camera->fb_plane.fbmem +
                   next_page * QPK_CAMERA_RAW_HEIGHT *
                   camera->fb_plane.stride;
          started = qpk_now_us();
          memcpy(target, source, QPK_CAMERA_FRAME_SIZE);
          camera->profile_convert_us += qpk_now_us() - started;
        }

      started = qpk_now_us();
      if (camera->memory_type == V4L2_MEMORY_USERPTR)
        {
          up_invalidate_dcache((uintptr_t)target,
                               (uintptr_t)target + QPK_CAMERA_FRAME_SIZE);
        }
      else
        {
          up_clean_dcache((uintptr_t)target,
                          (uintptr_t)target + QPK_CAMERA_FRAME_SIZE);
        }

      camera->profile_clean_us += qpk_now_us() - started;
      camera->profile_converted++;

      camera->fb_plane.xoffset = 0;
      camera->fb_plane.yoffset =
        next_page * QPK_CAMERA_RAW_HEIGHT;
      ret = ioctl(camera->fb_fd, FBIOPAN_DISPLAY,
                  (uintptr_t)&camera->fb_plane);
      if (ret < 0)
        {
          pan_error = errno;
          camera->pan_errors++;
          camera->dropped_frames++;
          pan_failures++;
          printf("[qpk] framebuffer pan failed: %d (%u/%u)\n",
                 pan_error, pan_failures, QPK_CAMERA_MAX_PAN_ERRORS);
        }
      else
        {
          pan_failures = 0;
          camera->fb_page = next_page;
          camera->frames++;
          if (camera->frames == 1)
            {
              camera->fps_started_ms = qpk_now_ms();
              camera->display_fps_started_ms = camera->fps_started_ms;
              printf("[qpk] camera first frame displayed: %lu bytes\n",
                     (unsigned long)buffer.bytesused);
            }
          else if (camera->frames == 31)
            {
              uint64_t elapsed =
                qpk_now_ms() - camera->display_fps_started_ms;
              uint32_t fps_x10 = elapsed > 0 ? 300000 / elapsed : 0;

              printf("[qpk] camera preview fps: %lu.%lu "
                     "captured=%lu dropped=%lu\n",
                     (unsigned long)(fps_x10 / 10),
                     (unsigned long)(fps_x10 % 10),
                     (unsigned long)camera->captured_frames,
                     (unsigned long)camera->dropped_frames);
            }
        }

      if (camera->memory_type == V4L2_MEMORY_USERPTR)
        {
          /* On a successful flip, only the page that just left scanout may
           * return to CSI.  If the flip failed, the newly captured page was
           * never displayed and remains the safe capture target.
           */

          released = ret == OK ?
            (FAR uint8_t *)camera->fb_plane.fbmem + old_page * page_size :
            target;
          camera->mmap_buffers[buffer.index] = released;
          buffer.m.userptr = (uintptr_t)released;
          buffer.length = QPK_CAMERA_FRAME_SIZE;
        }

      started = qpk_now_us();
      if (ioctl(camera->fd, VIDIOC_QBUF, (uintptr_t)&buffer) < 0)
        {
          camera->qbuf_errors++;
          fatal_error = -errno;
          printf("[qpk] camera QBUF failed: %d\n", errno);
          break;
        }

      camera->profile_qbuf_us += qpk_now_us() - started;
      qpk_camera_profile(camera);

      if (camera->captured_frames % QPK_CAMERA_HEALTH_FRAMES == 0)
        {
          memory = mallinfo();
          printf("[qpk] camera health: captured=%lu displayed=%lu "
                 "dropped=%lu heap_free=%lu heap_largest=%lu "
                 "dqerr=%lu qbuferr=%lu panerr=%lu\n",
                 (unsigned long)camera->captured_frames,
                 (unsigned long)camera->frames,
                 (unsigned long)camera->dropped_frames,
                 (unsigned long)memory.fordblks,
                 (unsigned long)memory.mxordblk,
                 (unsigned long)camera->dq_errors,
                 (unsigned long)camera->qbuf_errors,
                 (unsigned long)camera->pan_errors);
        }

      if (pan_failures >= QPK_CAMERA_MAX_PAN_ERRORS)
        {
          fatal_error = -pan_error;
          break;
        }
    }

  camera->thread_error = fatal_error;
  camera->thread_alive = false;
  if (fatal_error < 0)
    {
      printf("[qpk] camera worker stopped at frame %lu: %d\n",
             (unsigned long)camera->frames, fatal_error);
    }

  return NULL;
}

static void qpk_camera_poll(lv_timer_t *timer)
{
  struct qpk_camera_s *camera = &g_qpk.camera;
  uint64_t elapsed;
  uint64_t now;
  uint32_t fps_x10;
  int ready;

  (void)timer;
  if (camera->canvas == NULL)
    {
      return;
    }

  if (camera->blank_mode)
    {
      camera->display_buffer = 1 - camera->display_buffer;
      lv_canvas_set_buffer(camera->canvas,
                           camera->rgb565[camera->display_buffer],
                           QPK_CAMERA_VIEW_WIDTH,
                           QPK_CAMERA_VIEW_HEIGHT,
                           LV_COLOR_FORMAT_RGB565);
      lv_obj_invalidate(camera->canvas);
      camera->frames++;

      now = qpk_now_ms();
      if (camera->frames == 1)
        {
          camera->display_fps_started_ms = now;
          printf("[qpk] blank canvas baseline started\n");
        }
      else if ((camera->frames - 1) % 100 == 0)
        {
          elapsed = now - camera->display_fps_started_ms;
          fps_x10 = elapsed > 0 ? 1000000 / elapsed : 0;
          printf("[qpk] blank canvas fps: %lu.%lu frames=%lu\n",
                 (unsigned long)(fps_x10 / 10),
                 (unsigned long)(fps_x10 % 10),
                 (unsigned long)camera->frames);
          camera->display_fps_started_ms = now;
        }

      return;
    }

  if (!camera->thread_running)
    {
      return;
    }

  pthread_mutex_lock(&camera->lock);
  ready = camera->ready_buffer;
  if (ready >= 0 && ready != camera->display_buffer)
    {
      camera->display_buffer = ready;
      lv_canvas_set_buffer(camera->canvas, camera->rgb565[ready],
                           QPK_CAMERA_VIEW_WIDTH, QPK_CAMERA_VIEW_HEIGHT,
                           LV_COLOR_FORMAT_RGB565);
      camera->ready_buffer = -1;
      lv_obj_invalidate(camera->canvas);
    }
  pthread_mutex_unlock(&camera->lock);
}

static int qpk_camera_blank_start(int x, int y)
{
  struct qpk_camera_s *camera = &g_qpk.camera;
  size_t frame_bytes = QPK_CAMERA_VIEW_WIDTH * QPK_CAMERA_VIEW_HEIGHT *
                       sizeof(uint16_t);
  size_t i;
  int ret;

  qpk_camera_stop();
  camera->rgb565[0] = memalign(64, frame_bytes);
  camera->rgb565[1] = memalign(64, frame_bytes);
  if (camera->rgb565[0] == NULL || camera->rgb565[1] == NULL)
    {
      ret = -ENOMEM;
      goto error;
    }

  for (i = 0; i < frame_bytes / sizeof(uint16_t); i++)
    {
      camera->rgb565[0][i] = 0x0841;
      camera->rgb565[1][i] = 0x1082;
    }

  camera->display_buffer = 0;
  camera->canvas = lv_canvas_create(g_qpk.root);
  if (camera->canvas == NULL)
    {
      ret = -ENOMEM;
      goto error;
    }

  lv_canvas_set_buffer(camera->canvas, camera->rgb565[0],
                       QPK_CAMERA_VIEW_WIDTH, QPK_CAMERA_VIEW_HEIGHT,
                       LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(camera->canvas, x, y);
  lv_obj_remove_flag(camera->canvas,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(camera->canvas, 8, 0);
  camera->blank_mode = true;
  camera->timer = lv_timer_create(qpk_camera_poll,
                                  QPK_CAMERA_PREVIEW_PERIOD_MS, NULL);
  if (camera->timer == NULL)
    {
      ret = -ENOMEM;
      goto error;
    }

  printf("[qpk] blank canvas ready: %dx%d period=%d ms\n",
         QPK_CAMERA_VIEW_WIDTH, QPK_CAMERA_VIEW_HEIGHT,
         QPK_CAMERA_PREVIEW_PERIOD_MS);
  return OK;

error:
  qpk_camera_stop();
  return ret;
}

int qpk_native_camera_stop(void);

static int qpk_camera_start(int x, int y)
{
  struct qpk_camera_s *camera = &g_qpk.camera;
  struct v4l2_requestbuffers request;
  struct v4l2_format format;
  struct v4l2_streamparm parm;
  struct v4l2_buffer buffer;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  int i;
  int ret;

  (void)x;
  (void)y;
  qpk_camera_stop();
  camera->stop_requested = false;
  camera->fb_fd = -1;
  camera->fd = open(QPK_CAMERA_DEVICE, O_RDWR | O_NONBLOCK);
  if (camera->fd < 0)
    {
      ret = -errno;
      printf("[qpk] camera open %s failed: %d\n",
             QPK_CAMERA_DEVICE, -ret);
      goto error;
    }

  camera->opened = true;
  memset(&format, 0, sizeof(format));
  format.type = type;
  format.fmt.pix.width = QPK_CAMERA_RAW_WIDTH;
  format.fmt.pix.height = QPK_CAMERA_RAW_HEIGHT;
  format.fmt.pix.field = V4L2_FIELD_ANY;
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  ret = ioctl(camera->fd, VIDIOC_S_FMT, (uintptr_t)&format);
  if (ret < 0)
    {
      ret = -errno;
      printf("[qpk] camera S_FMT failed: %d\n", -ret);
      goto error;
    }

  /* Do not rely on the driver's default interval.  The V4L2 upper-half can
   * otherwise retain its low-rate default even though SC2336 advertises
   * 30 fps. */
  memset(&parm, 0, sizeof(parm));
  parm.type = type;
  parm.parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
  parm.parm.capture.timeperframe.numerator = 1;
  parm.parm.capture.timeperframe.denominator = 30;
  ret = ioctl(camera->fd, VIDIOC_S_PARM, (uintptr_t)&parm);
  if (ret < 0)
    {
      ret = -errno;
      printf("[qpk] camera S_PARM failed: %d\n", -ret);
      goto error;
    }

  camera->fb_fd = open("/dev/fb0", O_RDWR);
  if (camera->fb_fd < 0)
    {
      ret = -errno;
      printf("[qpk] camera framebuffer open failed: %d\n", -ret);
      goto error;
    }

  memset(&camera->fb_video, 0, sizeof(camera->fb_video));
  memset(&camera->fb_plane, 0, sizeof(camera->fb_plane));
  ret = ioctl(camera->fb_fd, FBIOGET_VIDEOINFO,
              (uintptr_t)&camera->fb_video);
  if (ret < 0)
    {
      ret = -errno;
      printf("[qpk] camera FB VIDEOINFO failed: %d\n", -ret);
      goto error;
    }

  camera->fb_plane.display = 0;
  ret = ioctl(camera->fb_fd, FBIOGET_PLANEINFO,
              (uintptr_t)&camera->fb_plane);
  if (ret < 0)
    {
      ret = -errno;
      printf("[qpk] camera FB PLANEINFO failed: %d\n", -ret);
      goto error;
    }

  if (camera->fb_video.fmt != FB_FMT_RGB16_565 ||
      camera->fb_video.xres != QPK_CAMERA_RAW_WIDTH ||
      camera->fb_video.yres != QPK_CAMERA_RAW_HEIGHT ||
      camera->fb_plane.yres_virtual <
        QPK_CAMERA_RAW_HEIGHT * QPK_CAMERA_FRAMEBUFFER_COUNT ||
      camera->fb_plane.fblen <
        QPK_CAMERA_FRAME_SIZE * QPK_CAMERA_FRAMEBUFFER_COUNT)
    {
      ret = -ENOTSUP;
      printf("[qpk] camera framebuffer geometry unsupported\n");
      goto error;
    }

  memset(&request, 0, sizeof(request));
  request.type = type;
  request.memory = V4L2_MEMORY_USERPTR;
  request.count = QPK_CAMERA_DISPLAY_BUFFER_COUNT;
  request.mode = V4L2_BUF_MODE_RING;
  ret = ioctl(camera->fd, VIDIOC_REQBUFS, (uintptr_t)&request);
  if (ret < 0 || request.count < QPK_CAMERA_DISPLAY_BUFFER_COUNT)
    {
      ret = ret < 0 ? -errno : -ENOMEM;
      printf("[qpk] camera USERPTR REQBUFS failed: %d count=%lu\n",
             -ret, (unsigned long)request.count);
      goto error;
    }

  camera->memory_type = V4L2_MEMORY_USERPTR;
  camera->mmap_count = request.count;
  camera->fb_page = camera->fb_plane.yoffset / QPK_CAMERA_RAW_HEIGHT;
  if (camera->fb_page >= QPK_CAMERA_FRAMEBUFFER_COUNT)
    {
      camera->fb_page = 0;
    }

  for (i = 0; i < camera->mmap_count; i++)
    {
      camera->mmap_lengths[i] = QPK_CAMERA_FRAME_SIZE;
      camera->mmap_buffers[i] =
        (FAR uint8_t *)camera->fb_plane.fbmem +
        ((camera->fb_page + 1 + i) % QPK_CAMERA_FRAMEBUFFER_COUNT) *
        QPK_CAMERA_RAW_HEIGHT *
        camera->fb_plane.stride;

      memset(&buffer, 0, sizeof(buffer));
      buffer.type = type;
      buffer.memory = camera->memory_type;
      buffer.index = i;
      buffer.length = camera->mmap_lengths[i];
      buffer.m.userptr = (uintptr_t)camera->mmap_buffers[i];
      ret = ioctl(camera->fd, VIDIOC_QBUF, (uintptr_t)&buffer);
      if (ret < 0)
        {
          ret = -errno;
          printf("[qpk] camera USERPTR QBUF %d failed: %d\n", i, -ret);
          goto error;
        }
    }

  ret = ioctl(camera->fd, VIDIOC_STREAMON, (uintptr_t)&type);
  if (ret < 0)
    {
      ret = -errno;
      printf("[qpk] camera STREAMON failed: %d\n", -ret);
      goto error;
    }

  camera->streaming = true;
  camera->direct_preview = true;
  camera->thread_alive = true;
  camera->thread_error = OK;
  printf("[qpk] camera USERPTR stream started, framebuffer zero-copy\n");

  ret = pthread_create(&camera->thread, NULL, qpk_camera_thread, camera);
  if (ret != 0)
    {
      camera->thread_alive = false;
      ret = -ret;
      goto error;
    }

  camera->thread_running = true;

  printf("[qpk] camera direct preview ready\n");
  return OK;

error:
  /* Keep ownership on cleanup failure; the native controller can retry. */
  (void)qpk_native_camera_stop();
  return ret;
}

static JSValue js_camera_start(JSContext *context,
                               JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  int x;
  int y;
  int ret;

  (void)this_value;
  x = qpk_arg_int(context, argc, argv, 0, 124);
  y = qpk_arg_int(context, argc, argv, 1, 12);
  ret = qpk_camera_start(x, y);
  if (ret < 0)
    {
      printf("[qpk] camera start failed: %d\n", ret);
      if (g_qpk.toast_cb != NULL)
        {
          g_qpk.toast_cb("摄像头启动失败");
        }
    }

  return JS_NewBool(context, ret >= 0);
}

static JSValue js_camera_stop(JSContext *context,
                              JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  (void)context;
  (void)this_value;
  (void)argc;
  (void)argv;
  qpk_camera_stop();
  return JS_UNDEFINED;
}

static JSValue js_camera_blank(JSContext *context,
                               JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  int x;
  int y;
  int ret;

  (void)this_value;
  x = qpk_arg_int(context, argc, argv, 0, 124);
  y = qpk_arg_int(context, argc, argv, 1, 12);
  ret = qpk_camera_blank_start(x, y);
  if (ret < 0)
    {
      printf("[qpk] blank canvas start failed: %d\n", ret);
    }

  return JS_NewBool(context, ret >= 0);
}

static JSValue js_camera_frames(JSContext *context,
                                JSValueConst this_value,
                                int argc, JSValueConst *argv)
{
  uint32_t frames;
  int error;

  (void)this_value;
  (void)argc;
  (void)argv;
  frames = g_qpk.camera.frames;
  if (g_qpk.camera.thread_running && !g_qpk.camera.thread_alive &&
      !g_qpk.camera.stop_requested)
    {
      error = g_qpk.camera.thread_error;
      qpk_camera_stop();
      printf("[qpk] camera failure cleaned up: %d\n", error);
      if (g_qpk.toast_cb != NULL)
        {
          g_qpk.toast_cb("摄像头流异常，已安全停止");
        }
    }

  return JS_NewUint32(context, frames);
}

static JSValue js_ha_get_state(JSContext *context, JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  const char *url;
  const char *token;
  const char *entity;
  int ret = -EINVAL;

  (void)this_value;
  if (argc < 3)
    {
      return JS_NewBool(context, false);
    }

  url = JS_ToCString(context, argv[0]);
  token = JS_ToCString(context, argv[1]);
  entity = JS_ToCString(context, argv[2]);
  if (url != NULL && token != NULL && entity != NULL)
    {
      ret = qpk_ha_get_state(url, token, entity);
    }

  JS_FreeCString(context, url);
  JS_FreeCString(context, token);
  JS_FreeCString(context, entity);
  return JS_NewBool(context, ret == 0);
}

static JSValue js_ha_get(JSContext *context, JSValueConst this_value,
                         int argc, JSValueConst *argv)
{
  const char *url;
  const char *token;
  const char *resource;
  int ret = -EINVAL;

  (void)this_value;
  if (argc < 3)
    {
      return JS_NewBool(context, false);
    }

  url = JS_ToCString(context, argv[0]);
  token = JS_ToCString(context, argv[1]);
  resource = JS_ToCString(context, argv[2]);
  if (url != NULL && token != NULL && resource != NULL)
    {
      ret = qpk_ha_get(url, token, resource);
    }

  JS_FreeCString(context, url);
  JS_FreeCString(context, token);
  JS_FreeCString(context, resource);
  return JS_NewBool(context, ret == 0);
}

static JSValue js_ha_call_service(JSContext *context,
                                  JSValueConst this_value,
                                  int argc, JSValueConst *argv)
{
  const char *url;
  const char *token;
  const char *domain;
  const char *service;
  const char *data;
  int ret = -EINVAL;

  (void)this_value;
  if (argc < 5)
    {
      return JS_NewBool(context, false);
    }

  url = JS_ToCString(context, argv[0]);
  token = JS_ToCString(context, argv[1]);
  domain = JS_ToCString(context, argv[2]);
  service = JS_ToCString(context, argv[3]);
  data = JS_ToCString(context, argv[4]);
  if (url != NULL && token != NULL && domain != NULL && service != NULL &&
      data != NULL)
    {
      ret = qpk_ha_call_service(url, token, domain, service, data);
    }

  JS_FreeCString(context, url);
  JS_FreeCString(context, token);
  JS_FreeCString(context, domain);
  JS_FreeCString(context, service);
  JS_FreeCString(context, data);
  return JS_NewBool(context, ret == 0);
}

static JSValue js_ha_poll(JSContext *context, JSValueConst this_value,
                          int argc, JSValueConst *argv)
{
  struct qpk_ha_result_s result;
  JSValue object;

  (void)this_value;
  (void)argc;
  (void)argv;
  qpk_ha_poll(&result);
  object = JS_NewObject(context);
  JS_SetPropertyStr(context, object, "busy",
                    JS_NewBool(context, result.busy));
  JS_SetPropertyStr(context, object, "done",
                    JS_NewBool(context, result.done));
  JS_SetPropertyStr(context, object, "status",
                    JS_NewInt32(context, result.status));
  JS_SetPropertyStr(context, object, "error",
                    JS_NewInt32(context, result.error));
  JS_SetPropertyStr(context, object, "body",
                    JS_NewString(context,
                                 result.body == NULL ? "" : result.body));
  if (result.body != NULL)
    {
      memset(result.body, 0, strlen(result.body));
      free(result.body);
    }
  return object;
}

static JSValue js_app_get_info(JSContext *context,
                               JSValueConst this_value,
                               int argc, JSValueConst *argv)
{
  JSValue info;

  (void)this_value;
  (void)argc;
  (void)argv;
  info = JS_NewObject(context);
  JS_SetPropertyStr(context, info, "name",
                    JS_NewString(context, g_qpk.name));
  JS_SetPropertyStr(context, info, "packageName",
                    JS_NewString(context, g_qpk.package));
  JS_SetPropertyStr(context, info, "versionName",
                    JS_NewString(context, g_qpk.version));
  return info;
}

static JSValue js_console_log(JSContext *context,
                              JSValueConst this_value,
                              int argc, JSValueConst *argv)
{
  int i;

  (void)this_value;
  printf("[qpk]");
  for (i = 0; i < argc; i++)
    {
      const char *text = JS_ToCString(context, argv[i]);
      if (text == NULL)
        {
          return JS_EXCEPTION;
        }

      printf(" %s", text);
      JS_FreeCString(context, text);
    }

  printf("\n");
  return JS_UNDEFINED;
}

static void qpk_install_api(JSContext *context)
{
  JSValue global;
  JSValue object;

  global = JS_GetGlobalObject(context);
  object = JS_NewObject(context);
  JS_SetPropertyStr(context, object, "text",
                    JS_NewCFunction(context, js_ui_text, "text", 5));
  JS_SetPropertyStr(context, object, "number",
                    JS_NewCFunction(context, js_ui_number, "number", 6));
  JS_SetPropertyStr(context, object, "setText",
                    JS_NewCFunction(context, js_ui_set_text, "setText", 2));
  JS_SetPropertyStr(context, object, "setHidden",
                    JS_NewCFunction(context, js_ui_set_hidden,
                                    "setHidden", 2));
  JS_SetPropertyStr(context, object, "background",
                    JS_NewCFunction(context, js_ui_background,
                                    "background", 1));
  JS_SetPropertyStr(context, object, "getSize",
                    JS_NewCFunction(context, js_ui_get_size,
                                    "getSize", 0));
  JS_SetPropertyStr(context, object, "setColor",
                    JS_NewCFunction(context, js_ui_set_color,
                                    "setColor", 2));
  JS_SetPropertyStr(context, object, "panel",
                    JS_NewCFunction(context, js_ui_panel, "panel", 7));
  JS_SetPropertyStr(context, object, "button",
                    JS_NewCFunction(context, js_ui_button, "button", 7));
  JS_SetPropertyStr(context, object, "onSwipe",
                    JS_NewCFunction(context, js_ui_on_swipe,
                                    "onSwipe", 1));
  JS_SetPropertyStr(context, object, "setPos",
                    JS_NewCFunction(context, js_ui_set_pos, "setPos", 3));
  JS_SetPropertyStr(context, object, "setSize",
                    JS_NewCFunction(context, js_ui_set_size, "setSize", 3));
  JS_SetPropertyStr(context, object, "setOpacity",
                    JS_NewCFunction(context, js_ui_set_opacity,
                                    "setOpacity", 2));
  JS_SetPropertyStr(context, object, "show",
                    JS_NewCFunction(context, js_ui_show, "show", 1));
  JS_SetPropertyStr(context, object, "hide",
                    JS_NewCFunction(context, js_ui_hide, "hide", 1));
  JS_SetPropertyStr(context, object, "remove",
                    JS_NewCFunction(context, js_ui_remove, "remove", 1));
  JS_SetPropertyStr(context, object, "rect",
                    JS_NewCFunction(context, js_ui_rect, "rect", 5));
  JS_SetPropertyStr(context, object, "primary",
                    JS_NewUint32(context, g_qpk.primary_color));
  JS_SetPropertyStr(context, object, "secondary",
                    JS_NewUint32(context, g_qpk.secondary_color));
  JS_SetPropertyStr(context, object, "surface",
                    JS_NewUint32(context, g_qpk.surface_color));
  JS_SetPropertyStr(context, global, "ui", object);

  object = JS_NewObject(context);
  JS_SetPropertyStr(context, object, "showToast",
                    JS_NewCFunction(context, js_prompt_toast,
                                    "showToast", 1));
  JS_SetPropertyStr(context, object, "dialog",
                    JS_NewCFunction(context, js_prompt_dialog,
                                    "dialog", 1));
  JS_SetPropertyStr(context, object, "input",
                    JS_NewCFunction(context, js_prompt_input,
                                    "input", 2));
  JS_SetPropertyStr(context, global, "prompt", object);

  object = JS_NewObject(context);
  JS_SetPropertyStr(context, object, "get",
                    JS_NewCFunction(context, js_storage_get, "get", 1));
  JS_SetPropertyStr(context, object, "set",
                    JS_NewCFunction(context, js_storage_set, "set", 2));
  JS_SetPropertyStr(context, object, "delete",
                    JS_NewCFunction(context, js_storage_delete,
                                    "delete", 1));
  {
    JSValue system = JS_NewObject(context);
    JSValue camera = JS_NewObject(context);
    JSValue homeassistant = JS_NewObject(context);

    JS_SetPropertyStr(context, camera, "start",
                      JS_NewCFunction(context, js_camera_start,
                                      "start", 2));
    JS_SetPropertyStr(context, camera, "stop",
                      JS_NewCFunction(context, js_camera_stop,
                                      "stop", 0));
    JS_SetPropertyStr(context, camera, "blank",
                      JS_NewCFunction(context, js_camera_blank,
                                      "blank", 2));
    JS_SetPropertyStr(context, camera, "frames",
                      JS_NewCFunction(context, js_camera_frames,
                                      "frames", 0));
    JS_SetPropertyStr(context, system, "storage", object);
    JS_SetPropertyStr(context, system, "camera", camera);
    JS_SetPropertyStr(context, homeassistant, "getState",
                      JS_NewCFunction(context, js_ha_get_state,
                                      "getState", 3));
    JS_SetPropertyStr(context, homeassistant, "get",
                      JS_NewCFunction(context, js_ha_get, "get", 3));
    JS_SetPropertyStr(context, homeassistant, "callService",
                      JS_NewCFunction(context, js_ha_call_service,
                                      "callService", 5));
    JS_SetPropertyStr(context, homeassistant, "poll",
                      JS_NewCFunction(context, js_ha_poll, "poll", 0));
    JS_SetPropertyStr(context, system, "homeAssistant", homeassistant);
    qpk_pomodoro_bind(context, system);
    JS_SetPropertyStr(context, global, "system", system);
  }

  object = JS_NewObject(context);
  JS_SetPropertyStr(context, object, "getInfo",
                    JS_NewCFunction(context, js_app_get_info,
                                    "getInfo", 0));
  JS_SetPropertyStr(context, global, "app", object);

  object = JS_NewObject(context);
  JS_SetPropertyStr(context, object, "log",
                    JS_NewCFunction(context, js_console_log, "log", 1));
  JS_SetPropertyStr(context, global, "console", object);
  JS_SetPropertyStr(context, global, "setInterval",
                    JS_NewCFunction(context, js_set_interval,
                                    "setInterval", 2));
  JS_SetPropertyStr(context, global, "clearInterval",
                    JS_NewCFunction(context, js_clear_interval,
                                    "clearInterval", 1));
  JS_FreeValue(context, global);
}

int qpk_runtime_launch(lv_obj_t *root, const char *name,
                       const char *package, const char *version,
                       const char *filename, const char *source,
                       size_t source_len, qpk_font_cb_t font_cb,
                       qpk_message_cb_t toast_cb,
                       qpk_message_cb_t dialog_cb)
{
  JSValue result;
  uint32_t background;
  unsigned int brightness;

  if (root == NULL || source == NULL || source_len == 0)
    {
      return -EINVAL;
    }

  qpk_runtime_stop();
  memset(&g_qpk, 0, sizeof(g_qpk));
  g_qpk.camera.fd = -1;
  g_qpk.camera.fb_fd = -1;
  g_qpk.root = root;
  g_qpk.font_cb = font_cb;
  g_qpk.toast_cb = toast_cb;
  g_qpk.dialog_cb = dialog_cb;
  g_qpk.next_timer_id = 100;
  g_qpk.input_callback = JS_UNDEFINED;
  background = lv_color_to_u32(lv_obj_get_style_bg_color(root,
                                                          LV_PART_MAIN));
  brightness = ((background >> 16) & 0xff) +
               ((background >> 8) & 0xff) + (background & 0xff);
  if (brightness > 384)
    {
      g_qpk.primary_color = 0x172033;
      g_qpk.secondary_color = 0x65708a;
      g_qpk.surface_color = 0xe8ebf2;
    }
  else
    {
      g_qpk.primary_color = 0xffffff;
      g_qpk.secondary_color = 0x8f9bb5;
      g_qpk.surface_color = 0x30394f;
    }
  strlcpy(g_qpk.name, name ? name : "Quick App", sizeof(g_qpk.name));
  strlcpy(g_qpk.package, package ? package : "", sizeof(g_qpk.package));
  strlcpy(g_qpk.version, version ? version : "", sizeof(g_qpk.version));

  g_qpk.runtime = JS_NewRuntime();
  if (g_qpk.runtime == NULL)
    {
      return -ENOMEM;
    }

  JS_SetMemoryLimit(g_qpk.runtime,
                    package != NULL &&
                    strcmp(package, "com.openvela.homeassistant") == 0 ?
                    QPK_HA_MEMORY_LIMIT : QPK_MEMORY_LIMIT);
  JS_SetMaxStackSize(g_qpk.runtime, QPK_STACK_LIMIT);
  JS_SetInterruptHandler(g_qpk.runtime, qpk_interrupt, &g_qpk);
  g_qpk.context = JS_NewContext(g_qpk.runtime);
  if (g_qpk.context == NULL)
    {
      qpk_runtime_stop();
      return -ENOMEM;
    }

  lv_obj_remove_flag(g_qpk.root, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_qpk.root, qpk_event_swiped,
                      LV_EVENT_GESTURE, NULL);
  qpk_install_api(g_qpk.context);
  qpk_deadline_begin(QPK_EVAL_BUDGET);
  result = JS_Eval(g_qpk.context, source, source_len,
                   filename ? filename : "app.js", JS_EVAL_TYPE_GLOBAL);
  qpk_deadline_end();
  if (JS_IsException(result))
    {
      qpk_show_error("启动失败");
      JS_FreeValue(g_qpk.context, result);
      qpk_runtime_stop();
      return -ENOEXEC;
    }

  JS_FreeValue(g_qpk.context, result);
  qpk_deadline_begin(QPK_EVAL_BUDGET);
  qpk_run_jobs();
  qpk_deadline_end();
  printf("[qpk] started %s (%s %s) with QuickJS\n",
         g_qpk.name, g_qpk.package, g_qpk.version);
  return 0;
}

void qpk_runtime_stop(void)
{
  int i;

  qpk_camera_stop();
  qpk_ha_stop();

  if (g_qpk.root != NULL)
    {
      lv_obj_remove_event_cb(g_qpk.root, qpk_event_swiped);
    }

  if (g_qpk.context != NULL)
    {
      if (g_qpk.input_shade != NULL)
        {
          lv_obj_delete(g_qpk.input_shade);
          g_qpk.input_shade = NULL;
          g_qpk.input_textarea = NULL;
        }

      if (!JS_IsUndefined(g_qpk.input_callback))
        {
          JS_FreeValue(g_qpk.context, g_qpk.input_callback);
          g_qpk.input_callback = JS_UNDEFINED;
        }

      for (i = 0; i < QPK_MAX_TIMERS; i++)
        {
          if (g_qpk.timers[i].used)
            {
              lv_timer_delete(g_qpk.timers[i].timer);
              JS_FreeValue(g_qpk.context, g_qpk.timers[i].function);
            }
        }

      for (i = 0; i < QPK_MAX_EVENTS; i++)
        {
          if (g_qpk.events[i].used)
            {
              JS_FreeValue(g_qpk.context, g_qpk.events[i].function);
            }
        }

      if (g_qpk.swipe_event.used)
        {
          JS_FreeValue(g_qpk.context, g_qpk.swipe_event.function);
        }

      JS_FreeContext(g_qpk.context);
    }

  if (g_qpk.runtime != NULL)
    {
      JS_FreeRuntime(g_qpk.runtime);
    }

  memset(&g_qpk, 0, sizeof(g_qpk));
  g_qpk.camera.fd = -1;
  g_qpk.camera.fb_fd = -1;
}

bool qpk_runtime_camera_active(void)
{
  return g_qpk.camera.direct_preview;
}

int qpk_native_camera_start(void)
{
  return qpk_camera_start(0, 0);
}

int qpk_native_camera_stop(void)
{
  struct qpk_camera_s *camera = &g_qpk.camera;
  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  int ret;

  camera->stop_requested = true;
  if (camera->thread_running)
    {
      ret = pthread_join(camera->thread, NULL);
      if (ret != 0)
        {
          return -ret;
        }

      camera->thread_running = false;
    }

  /* Never return framebuffer ownership while DMA may still be running. */
  if (camera->streaming)
    {
      if (ioctl(camera->fd, VIDIOC_STREAMOFF, (uintptr_t)&type) < 0)
        {
          return -errno;
        }

      camera->streaming = false;
    }

  qpk_camera_stop();
  return OK;
}

bool qpk_runtime_running(void)
{
  return g_qpk.context != NULL;
}
