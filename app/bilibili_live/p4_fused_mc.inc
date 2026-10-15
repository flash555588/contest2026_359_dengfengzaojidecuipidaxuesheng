/* SPDX-License-Identifier: Apache-2.0
 * Included inside OpenH264's scalar MC namespace, after its filter helper.
 * Fuse quarter-pel prediction and averaging, preserving both rounding stages.
 */
#if defined(BILI_P4_FUSED_MC) || defined(BILI_P4_FUSED_CENTRAL)
#ifndef BILI_P4_MC_DEFAULT
#define BILI_P4_MC_DEFAULT 3
#endif
static unsigned g_bili_mc_features = BILI_P4_MC_DEFAULT;
static inline bool BiliMcEnabled(unsigned feature)
{
  return (__atomic_load_n(&g_bili_mc_features, __ATOMIC_RELAXED) & feature) != 0;
}
static inline bool BiliMcDisjoint(const uint8_t *src, int32_t ss,
                                  uint8_t *dst, int32_t ds,
                                  int32_t w, int32_t h)
{
  if (ss <= 0 || ds <= 0 || w <= 0 || h <= 0 || w > 16 || h > 16)
    return false;
  const uintptr_t s = reinterpret_cast<uintptr_t>(src);
  const uintptr_t d = reinterpret_cast<uintptr_t>(dst);
  const uintptr_t halo = 2 * static_cast<uintptr_t>(ss) + 2;
  const uintptr_t span = (h + 2) * static_cast<uintptr_t>(ss) + w + 3;
  const uintptr_t out = (h - 1) * static_cast<uintptr_t>(ds) + w;
  if (s < halo || UINTPTR_MAX - s < span || UINTPTR_MAX - d < out)
    return false;
  return d + out <= s - halo || s + span <= d;
}

/* HR < 0 selects only the vertical half-pel; VC < 0 only horizontal.
 * Otherwise average the two separately clipped half-pel predictions.
 * RR/RC select the raw pixel for single-filter quarter-pel positions.
 */
template<int HR, int VC, int RR, int RC>
static inline __attribute__((always_inline)) void
BiliMcQuarter(const uint8_t *src, int32_t ss, uint8_t *dst, int32_t ds,
              int32_t w, int32_t h)
{
  for (int32_t y = 0; y < h; ++y)
    {
      for (int32_t x = 0; x < w; ++x)
        {
          unsigned a = 0, b = 0;
          if (HR >= 0)
            a = WelsClip1((FilterInput8bitWithStride_c(src + HR * ss + x, 1) + 16) >> 5);
          if (VC >= 0)
            b = WelsClip1((FilterInput8bitWithStride_c(src + x + VC, ss) + 16) >> 5);
          if (HR < 0) a = src[RR * ss + x + RC];
          if (VC < 0) b = src[RR * ss + x + RC];
          dst[x] = static_cast<uint8_t>((a + b + 1) >> 1);
        }
      src += ss;
      dst += ds;
    }
}

/* Center (2,2) and its adjacent half-pel predictions. Vertical half-pels
 * reuse the unrounded six-tap intermediates, rather than filtering again.
 */
template<int HR, int VC>
static inline __attribute__((always_inline)) void
BiliMcCentral(const uint8_t *src, int32_t ss, uint8_t *dst, int32_t ds,
              int32_t w, int32_t h)
{
  int16_t tmp[22];
  for (int32_t y = 0; y < h; ++y)
    {
      for (int32_t x = 0; x < w + 5; ++x)
        tmp[x] = static_cast<int16_t>(FilterInput8bitWithStride_c(src - 2 + x, ss));
      for (int32_t x = 0; x < w; ++x)
        {
          const unsigned center = WelsClip1((HorFilterInput16bit_c(tmp + x) + 512) >> 10);
          unsigned half;
          if (VC >= 0) half = WelsClip1((tmp[x + 2 + VC] + 16) >> 5);
          else half = WelsClip1((FilterInput8bitWithStride_c(src + HR * ss + x, 1) + 16) >> 5);
          dst[x] = static_cast<uint8_t>((center + half + 1) >> 1);
        }
      src += ss;
      dst += ds;
    }
}
#endif
