/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __INCLUDE_NUTTX_VIDEO_P4_PPA_H
#define __INCLUDE_NUTTX_VIDEO_P4_PPA_H
#include <nuttx/compiler.h>
#include <nuttx/fs/ioctl.h>
#include <stdint.h>
#include <stddef.h>

/* Board-private synchronous PPA SRM ioctl. Buffers must be 128-byte aligned
 * RAM, with sizes rounded up to 128 bytes. Input is packed UYVY, not I420.
 * The driver owns DMA2D exclusively; do not enable a second DMA2D driver.
 */

#define P4_PPA_SRM _IOC(0x7f00, 1)
struct p4_ppa_job_s
{
  FAR const void *input;
  FAR void *output;
  size_t input_size;
  size_t output_size;
  uint16_t width;
  uint16_t height;
  uint16_t out_width;
  uint16_t out_height;
  uint16_t out_x;
  uint16_t out_y;
  uint16_t scale256; /* 16..256, multiple of 16 (hardware steps of 1/16). */
};

int esp32p4_ppa_register(FAR const char *path);
#endif
