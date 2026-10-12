/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_FLV_H
#define BILI_FLV_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define BILI_TAG_LIMIT (1024 * 1024)
#define BILI_AU_LIMIT (BILI_TAG_LIMIT + 4096)
typedef int (*bili_read_fn)(void *, uint8_t *, size_t);
typedef int (*bili_video_fn)(void *, const uint8_t *, size_t, uint32_t);
struct bili_flv {
  bili_read_fn read;
  bili_video_fn video;
  bili_video_fn audio; /* Complete FLV audio payload: flags, AAC type, data. */
  void *opaque;
  unsigned nal_length;
  int configured;
  uint8_t *tag;
  uint8_t *annexb;
  bool managed_buffers;
  size_t tag_capacity,annexb_capacity;
};
int bili_flv_run(struct bili_flv *f);
int bili_avc_config(struct bili_flv *f, const uint8_t *p, size_t n);
int bili_avc_packet(struct bili_flv *f, const uint8_t *p, size_t n,
                    uint32_t timestamp);
#endif
