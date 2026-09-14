"""Integrate the camera inventory and background MJPEG preview in desktop."""
from pathlib import Path
import re
import shutil
ws = Path(__file__).resolve().parent.parent
overlay = ws / '04-v3-20260913/camera-usb-fix/overlay'
base = ws / 'diagnostics/camera-baseline'
app = overlay / 'apps/system/desktop'
app.mkdir(parents=True, exist_ok=True)
# Use a private TJpgDec instance: no dependency on LVGL's allocator or threads.
decoder = base / 'apps/graphics/lvgl/lvgl/src/libs/tjpgd'
for name in ['tjpgd.c', 'tjpgd.h', 'tjpgdcnf.h']:
    s = (decoder / name).read_text()
    s = s.replace('#include "../../lv_conf_internal.h"', '#define LV_USE_TJPGD 1')
    s = s.replace('#define JD_FORMAT       0', '#define JD_FORMAT       1')
    s = s.replace('#define JD_USE_SCALE    0', '#define JD_USE_SCALE    1')
    s = s.replace('tjpgd.h', 'qpk_tjpgd.h').replace('tjpgdcnf.h', 'qpk_tjpgdcnf.h')
    s = re.sub(r'\bjd_(\w+)\b', r'qpk_jd_\1', s)
    (app / ('qpk_' + name)).write_text(s)
p = app / 'Makefile'
s = (base / 'apps/system/desktop/Makefile').read_text()
s = s.replace('include $(APPDIR)/Application.mk', 'CSRCS += qpk_tjpgd.c qpk_mjpeg.c qpk_camera_probe.c\n\ninclude $(APPDIR)/Application.mk')
p.write_text(s)
p = app / 'qpk_runtime.c'
s = (base / 'apps/system/desktop/qpk_runtime.c').read_text()
s = s.replace('#include <nuttx/video/video.h>', '#include <nuttx/video/video.h>\n#include <nuttx/video/uvc_camera.h>\n#include "qpk_mjpeg.h"')
s = s.replace('  int fb_fd;\n', '  int fb_fd;\n  bool usb;\n  struct uvc_camera_start usb_mode;\n', 1)
s = s.replace('static bool g_camera_redraw;', 'static bool g_camera_redraw;\nstatic int g_camera_last_error;')
s = s.replace('#define QPK_CAMERA_RAW_HEIGHT 600', '#define QPK_CAMERA_RAW_HEIGHT 600\n#define QPK_CAMERA_BAR_TOP 500')
s = s.replace('static void qpk_camera_bar_blit(uint16_t *pixels)', 'static int qpk_camera_bar_blit(uint16_t *pixels)')
s = s.replace('  for (int y = 500; y < 600; y++)', '  for (int y = QPK_CAMERA_BAR_TOP; y < QPK_CAMERA_RAW_HEIGHT; y++)')
s = s.replace('        pixels[y * 1024 + x] = color;\n      }\n}', '        pixels[y * 1024 + x] = color;\n      }\n  return status;\n}', 1)
s = s.replace('printf("[qpk] camera USERPTR stream started, framebuffer zero-copy\\n");',
              'printf("[qpk] camera %s worker starting\\n", camera->usb ? "USB MJPEG" : "CSI USERPTR");')
s = s.replace('  if (camera->streaming)\n', '  if (camera->streaming && !camera->usb)\n', 1)
s = s.replace('static int qpk_camera_start(int x, int y)', 'static int qpk_camera_start_source(int x, int y, int usb_id, uint32_t generation,\n                                    unsigned mode, uint32_t interval)')
s = s.replace('  camera->fd = open(QPK_CAMERA_DEVICE, O_RDWR | O_NONBLOCK);', '''  camera->usb = usb_id >= 0;
  camera->usb_mode.generation = generation;
  camera->usb_mode.mode = mode;
  camera->usb_mode.interval = interval;
  g_camera_last_error = 0;
  char device[24];
  if (camera->usb) snprintf(device, sizeof(device), "/dev/uvc%d", usb_id);
  else snprintf(device, sizeof(device), "%s", QPK_CAMERA_DEVICE);
  camera->fd = open(device, O_RDWR | O_NONBLOCK);''')
s = s.replace('  memset(&format, 0, sizeof(format));\n  format.type = type;', '  if (camera->usb) goto framebuffer;\n  memset(&format, 0, sizeof(format));\n  format.type = type;', 1)
s = s.replace('  camera->fb_fd = open("/dev/fb0", O_RDWR);', 'framebuffer:\n  camera->fb_fd = open("/dev/fb0", O_RDWR);', 1)
s = s.replace('  memset(&request, 0, sizeof(request));\n  request.type = type;', '''  camera->fb_page = camera->fb_plane.yoffset / QPK_CAMERA_RAW_HEIGHT;
  if (camera->usb)
    {
      pthread_mutex_lock(&g_camera_lock);
      camera->direct_preview = true;
      pthread_mutex_unlock(&g_camera_lock);
      goto launch_worker;
    }
  memset(&request, 0, sizeof(request));
  request.type = type;''', 1)
s = s.replace('  pthread_mutex_lock(&g_camera_lock);\n  camera->thread_alive = true;', 'launch_worker:\n  pthread_mutex_lock(&g_camera_lock);\n  camera->thread_alive = true;', 1)
s = s.replace('                             qpk_camera_thread, camera);', '                             camera->usb ? qpk_camera_usb_thread : qpk_camera_thread, camera);', 1)
start = s.index('static int qpk_camera_start_source')
end = s.index('static JSValue js_camera_capture', start)
section = s[start:end].replace('error:\n  qpk_camera_stop();', 'error:\n  g_camera_last_error = ret;\n  qpk_camera_stop();')
s = s[:start] + section + '''static int qpk_camera_start(int x, int y)
{ return qpk_camera_start_source(x, y, -1, 1, 0, 0); }

''' + s[end:]
s = s.replace('static void *qpk_camera_thread(pthread_addr_t arg)', '#include "qpk_camera_usb.inc"\n\nstatic void *qpk_camera_thread(pthread_addr_t arg)', 1)
s = s.replace('static JSValue js_camera_capture', '#include "qpk_camera_devices.inc"\n\nstatic JSValue js_camera_capture', 1)
s = s.replace('    JS_SetPropertyStr(context, camera, "start",', '''    JS_SetPropertyStr(context, camera, "devices", JS_NewCFunction(context, js_camera_devices, "devices", 0));
    JS_SetPropertyStr(context, camera, "modes", JS_NewCFunction(context, js_camera_modes, "modes", 2));
    JS_SetPropertyStr(context, camera, "startDevice", JS_NewCFunction(context, js_camera_start_device, "startDevice", 4));
    JS_SetPropertyStr(context, camera, "status", JS_NewCFunction(context, js_camera_status, "status", 0));
    JS_SetPropertyStr(context, camera, "start",''', 1)
s = s.replace('      printf("[qpk] camera failure awaiting cleanup: %d\\n", error);', '      g_camera_last_error = error;\n      printf("[qpk] camera failure awaiting cleanup: %d\\n", error);')
p.write_text(s)
source = (app / 'camera/app.js').read_bytes()
resource = '/* Generated from camera/app.js. */\n#include <stddef.h>\nstatic const unsigned char g_camera_app_js[] = {\n'
for i in range(0, len(source), 16):
    resource += '  ' + ', '.join(f'0x{b:02x}' for b in source[i:i+16]) + ',\n'
resource += '  0\n};\n'
original = (base / 'apps/system/desktop/camera_resource.c').read_text()
resource += original[original.index('const char *camera_get_app_js'):].replace('sizeof(g_camera_app_js);', 'sizeof(g_camera_app_js) - 1;')
(app / 'camera_resource.c').write_text(resource)
print('Camera app sources integrated')
