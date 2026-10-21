/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/video/uvc_camera.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include "qpk_mjpeg.h"
#ifdef CONFIG_ESPRESSIF_MIPI_DSI
#include <nuttx/video/camera_timing.h>
#endif

void qpk_camera_timing_log(void)
{
#ifdef CONFIG_ESPRESSIF_MIPI_DSI
  struct esp_camera_timing stats;esp_camera_timing_read(&stats);
#ifdef CONFIG_ESP32P4_FLASH_AUTO_SUSPEND
  extern void esp32p4_flash_auto_diagnostics(void);
  esp32p4_flash_auto_diagnostics();
#endif
  printf("[display-time] frames=%lu irq_gap_max_us=%lu pans=%lu pan_max_us=%lu pan_errors=%lu flash_guard_frames=%lu\n",
    (unsigned long)stats.display_frames,(unsigned long)stats.display_gap_max_us,
    (unsigned long)stats.pan_count,(unsigned long)stats.pan_max_us,(unsigned long)stats.pan_errors,
    (unsigned long)stats.display_guard_frames);
  printf("[flash-time] active=%lu ops(r/w/e)=%lu/%lu/%lu max_us=%lu/%lu/%lu total_us=%llu/%llu/%llu\n",
    (unsigned long)stats.flash_active,(unsigned long)stats.flash_ops[0],(unsigned long)stats.flash_ops[1],(unsigned long)stats.flash_ops[2],
    (unsigned long)stats.flash_max_us[0],(unsigned long)stats.flash_max_us[1],(unsigned long)stats.flash_max_us[2],
    (unsigned long long)stats.flash_total_us[0],(unsigned long long)stats.flash_total_us[1],(unsigned long long)stats.flash_total_us[2]);
#endif
}

int qpk_camera_probe(int argc, char **argv)
{
  extern void qpk_camera_diagnostics(void);
  qpk_camera_diagnostics();
  qpk_camera_timing_log();
  struct uvc_camera_list list;
  int fd = open("/dev/uvcctl", O_RDONLY);
  if (fd < 0) { printf("camera: USB host unavailable errno=%d\n", errno); return 1; }
  int ret = ioctl(fd, UVCIOC_LIST, (unsigned long)&list);
  close(fd);
  if (ret < 0) return 1;
  struct stat st;
  bool csi = stat("/dev/video0", &st) == 0 && S_ISCHR(st.st_mode);
  printf("camera: connected=%lu CSI=%d USB=%lu\n", (unsigned long)list.count + csi, csi, (unsigned long)list.count);
  if (csi) printf("camera: csi0 SC2336 1024x600 RGB565 30 fps\n");
  struct uvc_camera_caps *caps = malloc(sizeof(*caps));
  if (!caps) return 1;
  for (unsigned i = 0; i < list.count; i++)
    {
      struct uvc_camera_device *d = &list.devices[i];
      printf("camera: usb%u generation=%lu %s [%04x:%04x]\n", d->id, (unsigned long)d->generation, d->name, d->vid, d->pid);
      char path[24]; snprintf(path, sizeof(path), "/dev/uvc%u", d->id);
      fd = open(path, O_RDWR | O_NONBLOCK);
      if (fd < 0) continue;
      if (ioctl(fd, UVCIOC_CAPS, (unsigned long)caps) < 0) { close(fd); continue; }
      struct uvc_camera_stats stats;
      if (ioctl(fd, UVCIOC_STATS, (unsigned long)&stats) == 0)
        printf("  stream: frames=%lu dropped=%lu packets=%lu errors=%lu error=%ld\n",
          (unsigned long)stats.frames, (unsigned long)stats.dropped,
          (unsigned long)stats.packets, (unsigned long)stats.errors, (long)stats.error);
      for (unsigned m = 0; m < caps->count; m++)
        {
          struct uvc_camera_mode *mode = &caps->modes[m];
          printf("  mode=%u MJPEG %ux%u default_interval=%lu", m, mode->width, mode->height, (unsigned long)mode->default_interval);
          printf(mode->continuous ? " range(min,max,step)=" : " intervals=");
          for (unsigned j = 0; j < mode->interval_count; j++) printf("%s%lu", j ? "," : "", (unsigned long)mode->intervals[j]);
          printf("\n");
        }
      close(fd);
    }
  free(caps);
  return 0;
}
