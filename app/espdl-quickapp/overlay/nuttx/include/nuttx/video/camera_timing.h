/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
enum esp_camera_flash_op { CAMERA_FLASH_READ, CAMERA_FLASH_WRITE, CAMERA_FLASH_ERASE };
struct esp_camera_timing {
  uint32_t display_frames, display_gap_max_us, display_guard_frames;
  uint32_t pan_count, pan_errors, pan_max_us;
  uint32_t flash_active, flash_ops[3], flash_max_us[3];
  uint64_t flash_total_us[3];
};
uint64_t esp_camera_timing_now(void);
void esp_camera_display_frame(void);
void esp_camera_display_guard_frame(void);
void esp_camera_timing_pan(uint64_t started,int error);
uint64_t esp_camera_flash_begin(unsigned op);
void esp_camera_flash_end(unsigned op,uint32_t bytes,uint64_t started,int result);
void esp_camera_timing_read(struct esp_camera_timing *out);
