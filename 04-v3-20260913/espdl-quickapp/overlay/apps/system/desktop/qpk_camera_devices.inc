/* SPDX-License-Identifier: Apache-2.0 */
void qpk_camera_diagnostics(void)
{
  pthread_mutex_lock(&g_camera_lock);
  printf("camera app: busy=%d alive=%d frames=%lu error=%d photo=%d path=%s\n",
    g_camera_state != QPK_CAMERA_IDLE, g_camera.thread_alive,
    (unsigned long)g_camera.frames,
    g_camera.thread_error ? g_camera.thread_error : g_camera_last_error,
    g_photo_status, g_photo_path);
  extern volatile uint32_t g_esp_mipi_dsi_frames;
  extern volatile uint32_t g_esp_mipi_dsi_underruns;
  printf("camera display: frames=%lu underruns=%lu pan_errors=%lu mode=%ux%u\n",
    (unsigned long)g_esp_mipi_dsi_frames,
    (unsigned long)g_esp_mipi_dsi_underruns,
    (unsigned long)g_camera.pan_errors,
    g_camera.usb_mode.width, g_camera.usb_mode.height);
  pthread_mutex_unlock(&g_camera_lock);
}

static void qpk_js_int(JSContext *c, JSValue o, const char *key, int value)
{ JS_SetPropertyStr(c, o, key, JS_NewInt32(c, value)); }
static void qpk_js_string(JSContext *c, JSValue o, const char *key, const char *s)
{ JS_SetPropertyStr(c, o, key, JS_NewString(c, s)); }
static JSValue js_camera_devices(JSContext *c, JSValueConst self, int argc, JSValueConst *argv)
{
  JSValue result = JS_NewArray(c);
  unsigned count = 0;
  struct stat st;
  if (stat(QPK_CAMERA_DEVICE, &st) == 0 && S_ISCHR(st.st_mode))
    {
      JSValue o = JS_NewObject(c);
      qpk_js_string(c, o, "id", "csi0"); qpk_js_string(c, o, "name", "SC2336 · CSI");
      qpk_js_string(c, o, "transport", "csi"); qpk_js_int(c, o, "generation", 1);
      JS_SetPropertyStr(c, o, "previewSupported", JS_TRUE);
      JS_SetPropertyUint32(c, result, count++, o);
    }
  struct uvc_camera_list list;
  int fd = open("/dev/uvcctl", O_RDONLY);
  if (fd < 0) return result;
  int ret = ioctl(fd, UVCIOC_LIST, (unsigned long)&list);
  close(fd);
  if (ret < 0) { JS_FreeValue(c, result); return JS_ThrowInternalError(c, "USB camera inventory unavailable"); }
  for (unsigned i = 0; i < list.count && i < UVC_CAMERA_MAX_DEVICES; i++)
    {
      struct uvc_camera_device *d = &list.devices[i];
      JSValue o = JS_NewObject(c); char id[16]; snprintf(id, sizeof(id), "usb%u", d->id);
      qpk_js_string(c, o, "id", id); qpk_js_string(c, o, "name", d->name);
      qpk_js_string(c, o, "transport", "usb");
      JS_SetPropertyStr(c, o, "generation", JS_NewUint32(c, d->generation));
      JS_SetPropertyStr(c, o, "previewSupported", JS_NewBool(c, d->supported));
      JS_SetPropertyUint32(c, result, count++, o);
    }
  return result;
}
static int qpk_usb_index(const char *id)
{ return id && strlen(id) == 4 && !memcmp(id, "usb", 3) && id[3] >= '0' && id[3] < '0' + UVC_CAMERA_MAX_DEVICES ? id[3] - '0' : -1; }
static JSValue js_camera_modes(JSContext *c, JSValueConst self, int argc, JSValueConst *argv)
{
  const char *id = qpk_arg_string(c, argc, argv, 0);
  int index = qpk_usb_index(id);
  JS_FreeCString(c, id);
  if (index < 0) return JS_NewArray(c);
  uint32_t generation = 0;
  if (argc < 2 || JS_ToUint32(c, &generation, argv[1]) < 0) return JS_EXCEPTION;
  struct uvc_camera_caps *caps = malloc(sizeof(*caps));
  if (!caps) return JS_ThrowOutOfMemory(c);
  char path[24]; snprintf(path, sizeof(path), "/dev/uvc%d", index);
  int fd = open(path, O_RDONLY);
  int ret = fd < 0 ? -1 : ioctl(fd, UVCIOC_CAPS, (unsigned long)caps);
  if (fd >= 0) close(fd);
  if (ret < 0 || caps->generation != generation)
    { free(caps); return JS_ThrowInternalError(c, "Camera was disconnected"); }
  JSValue result = JS_NewArray(c);
  for (unsigned i = 0; i < caps->count; i++)
    {
      struct uvc_camera_mode *m = &caps->modes[i];
      JSValue o = JS_NewObject(c), intervals = JS_NewArray(c);
      qpk_js_int(c, o, "index", i); qpk_js_int(c, o, "width", m->width); qpk_js_int(c, o, "height", m->height);
      qpk_js_int(c, o, "defaultInterval", m->default_interval);
      JS_SetPropertyStr(c, o, "continuous", JS_NewBool(c, m->continuous));
      for (unsigned n = 0; n < m->interval_count; n++) JS_SetPropertyUint32(c, intervals, n, JS_NewUint32(c, m->intervals[n]));
      JS_SetPropertyStr(c, o, "intervals", intervals);
      JS_SetPropertyUint32(c, result, i, o);
    }
  free(caps);
  return result;
}
static JSValue js_camera_start_device(JSContext *c, JSValueConst self, int argc, JSValueConst *argv)
{
  const char *id = qpk_arg_string(c, argc, argv, 0);
  if (!id) return JS_EXCEPTION;
  bool csi = strcmp(id, "csi0") == 0;
  int usb = qpk_usb_index(id);
  JS_FreeCString(c, id);
  if (!csi && usb < 0) return JS_FALSE;
  uint32_t generation = 0, interval = 0;
  int mode = qpk_arg_int(c, argc, argv, 2, 0);
  if (argc < 2 || JS_ToUint32(c, &generation, argv[1]) < 0) return JS_EXCEPTION;
  if (argc >= 4 && JS_ToUint32(c, &interval, argv[3]) < 0) return JS_EXCEPTION;
  if (mode < 0 || mode >= UVC_CAMERA_MAX_MODES) return JS_FALSE;
  return JS_NewBool(c, qpk_camera_start_source(0, 0, usb, generation, mode, interval) >= 0);
}
static JSValue js_camera_status(JSContext *c, JSValueConst self, int argc, JSValueConst *argv)
{
  JSValue o = JS_NewObject(c);
  pthread_mutex_lock(&g_camera_lock);
  bool busy = g_camera_state != QPK_CAMERA_IDLE;
  int error = g_camera.thread_error ? g_camera.thread_error : g_camera_last_error;
  uint32_t frames = g_camera.frames;
  struct uvc_camera_start mode = g_camera.usb_mode;
  pthread_mutex_unlock(&g_camera_lock);
  JS_SetPropertyStr(c, o, "busy", JS_NewBool(c, busy));
  qpk_js_int(c, o, "error", error); qpk_js_int(c, o, "frames", frames);
  qpk_js_int(c, o, "width", mode.width); qpk_js_int(c, o, "height", mode.height);
  qpk_js_int(c, o, "interval", mode.interval);
  return o;
}
