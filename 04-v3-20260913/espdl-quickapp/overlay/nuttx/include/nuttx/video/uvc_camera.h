/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __INCLUDE_NUTTX_VIDEO_UVC_CAMERA_H
#define __INCLUDE_NUTTX_VIDEO_UVC_CAMERA_H
#include <stdint.h>
#define UVC_CAMERA_MAX_DEVICES 4
#define UVC_CAMERA_MAX_MODES 32
#define UVC_CAMERA_MAX_INTERVALS 16
#define UVC_CAMERA_MAX_FRAME (4 * 1024 * 1024)
/* Private ioctl range, restricted to /dev/uvcctl and /dev/uvcN. */
#define UVCIOC_LIST  0x7501
#define UVCIOC_CAPS  0x7502
#define UVCIOC_START 0x7503
#define UVCIOC_STOP  0x7504
#define UVCIOC_STATS 0x7505
struct uvc_camera_device
{
  uint32_t generation;
  uint16_t vid, pid;
  uint8_t id, connected, supported, reserved;
  char name[64];
};
struct uvc_camera_list
{
  uint32_t count;
  struct uvc_camera_device devices[UVC_CAMERA_MAX_DEVICES];
};
struct uvc_camera_mode
{
  uint16_t width, height;
  uint8_t format_index, frame_index, interval_count, continuous;
  uint32_t max_frame_size, default_interval;
  /* Units are 100 ns. Continuous modes store min, max, step. */
  uint32_t intervals[UVC_CAMERA_MAX_INTERVALS];
};
struct uvc_camera_caps
{
  uint32_t generation;
  uint16_t uvc_version;
  uint8_t count, truncated;
  struct uvc_camera_mode modes[UVC_CAMERA_MAX_MODES];
};
struct uvc_camera_start
{
  uint32_t generation, interval;
  uint8_t mode;
  uint8_t reserved[3];
  /* Filled with the successfully negotiated mode and payload limits. */
  uint16_t width, height;
  uint32_t max_frame_size, max_payload_size;
};
struct uvc_camera_stats
{
  uint32_t frames, dropped, packets, errors;
  int32_t error;
};
#endif
