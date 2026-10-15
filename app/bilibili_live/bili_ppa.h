/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_PPA_H
#define BILI_PPA_H
#include <stddef.h>
#include <stdint.h>
struct bili_ppa {
  int fd_plus_one;
  int failed;
  uint8_t *packed;
  size_t capacity;
  unsigned frames;
  uint64_t pack_us,dma_us;
};
uint16_t *bili_ppa_pixels(size_t pixels);
int bili_ppa_frame(struct bili_ppa *ctx,uint16_t *output,
                   const uint8_t *const planes[3],const int strides[2],
                   int width,int height,unsigned out_width,unsigned out_height);
void bili_ppa_close(struct bili_ppa *ctx);
void bili_ppa_enable(unsigned enabled);
int bili_ppa_test(void);
#endif
