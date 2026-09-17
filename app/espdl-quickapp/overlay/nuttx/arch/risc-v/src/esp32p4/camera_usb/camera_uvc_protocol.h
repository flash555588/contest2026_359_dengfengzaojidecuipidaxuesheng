/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <nuttx/video/uvc_camera.h>
struct uvc_descriptor_info
{
  struct uvc_camera_caps caps;
  uint8_t control_interface, stream_interface, endpoint;
};
int camera_uvc_parse(const uint8_t *data, size_t len, uint8_t stream_interface,
                     struct uvc_descriptor_info *out);
bool camera_uvc_interval_valid(const struct uvc_camera_mode *mode, uint32_t interval);
/* Payload assembly is independent of USB transport and buffer ownership.
 * A damaged frame stays damaged until FID changes, even after EOF. */
struct uvc_payload_state
{
  size_t used;
  int fid;
  bool damaged, ended;
};
int camera_uvc_payload(struct uvc_payload_state *s, const uint8_t *packet,
                       size_t size, uint8_t *frame, size_t capacity);
