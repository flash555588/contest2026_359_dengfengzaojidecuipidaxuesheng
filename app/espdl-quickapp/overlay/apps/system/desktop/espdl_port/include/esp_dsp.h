/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
static inline int dsps_dotprod_f32(const float *a, const float *b, float *out, int n)
{
  float sum = 0;
  for (int i = 0; i < n; ++i) sum += a[i] * b[i];
  *out = sum;
  return 0;
}
