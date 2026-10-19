/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_ZH_Q8_H
#define GLASS_ZH_Q8_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct zh_q8_blob {const void *data;size_t bytes;};
struct zh_q8_stats {
  uint64_t duration_us,latent_us,decoder_us,first_pcm_us;
  uint64_t conv1d_us;unsigned conv1d_calls;
  uint64_t convtr_us;unsigned convtr_calls;
  size_t working_bytes;unsigned frames,samples;
};
typedef bool (*zh_q8_cancel_fn)(void *);
typedef int (*zh_q8_pcm_fn)(void *,const float *,size_t);
/* One bounded diagnostic chunk; source blobs must remain valid until return.
 * No static model/scratch state: serialized by the enclosing speech service. */
int glass_zh_q8_synthesize(const struct zh_q8_blob blobs[4],
  const int32_t *ids,int n,int backend,zh_q8_pcm_fn pcm,zh_q8_cancel_fn cancel,void *user,
  struct zh_q8_stats *stats);
int glass_zh_q8_probe(int mode,zh_q8_cancel_fn cancel,void *user);
/* Internal hook in the pinned decoder adapter. Returns false for scalar fallback. */
bool glass_zh_q8_conv1d_stage(int16_t *,float *,const int16_t *,const int8_t *,
  const float *,const float *,int,int,int,int,int,float,float,const int16_t *,float,int);
int32_t glass_zh_q8_dot(const int16_t *,const int16_t *,int);
bool glass_zh_q8_convtr_stage(int16_t *,const int16_t *,const int8_t *,const float *,const float *,
  int,int,int,int,int,int,int,float,float);
#endif
