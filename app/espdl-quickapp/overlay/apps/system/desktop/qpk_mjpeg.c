/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_mjpeg.h"
#include "qpk_tjpgd.h"
#include "qpk_mjpeg_dht.h"
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct mjpeg_decoder
{
  JDEC jpeg;
  uint8_t workspace[16384];
  const uint8_t *input;
  size_t size, position;
  uint16_t *pixels;
  unsigned stride, height, width;
};
void *qpk_mjpeg_create(void) { return calloc(1, sizeof(struct mjpeg_decoder)); }
void qpk_mjpeg_destroy(void *decoder) { free(decoder); }
size_t qpk_mjpeg_frame_size(void *decoder)
{ return decoder ? ((struct mjpeg_decoder *)decoder)->size : 0; }
static size_t jpeg_input(JDEC *jd, uint8_t *buffer, size_t count)
{
  struct mjpeg_decoder *d = jd->device;
  if (count > d->size - d->position) count = d->size - d->position;
  if (buffer) memcpy(buffer, d->input + d->position, count);
  d->position += count;
  return count;
}
static int jpeg_output(JDEC *jd, void *bitmap, JRECT *r)
{
  struct mjpeg_decoder *d = jd->device;
  if (r->right >= d->width || r->bottom >= d->height) return 0;
  const uint16_t *src = bitmap;
  for (unsigned y = r->top; y <= r->bottom; y++)
    {
      size_t n = r->right - r->left + 1;
      memcpy(d->pixels + y * d->stride + r->left, src, n * 2);
      src += n;
    }
  return 1;
}
/* UVC MJPEG permits omission of the standard JPEG Huffman tables. Insert
 * the ISO/IEC 10918-1 default tables only if the frame has no DHT segment. */
static int jpeg_tables(uint8_t *input, size_t *size, size_t capacity)
{
  if (*size < 4 || input[0] != 255 || input[1] != 216) return -EBADMSG;
  bool dht = false;
  size_t p = 2;
  while (p + 4 <= *size)
    {
      if (input[p] != 255) return -EBADMSG;
      if (input[p + 1] == 255) { p++; continue; }
      uint8_t marker = input[p + 1];
      if (marker == 0xda)
        {
          if (dht) return 0;
          if (*size > capacity || sizeof(g_mjpeg_dht) > capacity - *size) return -EOVERFLOW;
          memmove(input + p + sizeof(g_mjpeg_dht), input + p, *size - p);
          memcpy(input + p, g_mjpeg_dht, sizeof(g_mjpeg_dht));
          *size += sizeof(g_mjpeg_dht);
          return 0;
        }
      if (marker == 0xc4) dht = true;
      if (marker == 0xd9 || marker == 0xd8 || marker == 0 || (marker >= 0xd0 && marker <= 0xd7)) return -EBADMSG;
      size_t n = ((size_t)input[p + 2] << 8) | input[p + 3];
      if (n < 2 || n > *size - p - 2) return -EBADMSG;
      p += n + 2;
    }
  return -EBADMSG;
}
/* Header-only parse used to size an on-screen image before decoding it. */
int qpk_mjpeg_probe(const uint8_t *input, size_t size, unsigned *width,
                    unsigned *height)
{
  if (!input || !size || !width || !height) return -EINVAL;
  struct mjpeg_decoder *d = calloc(1, sizeof(struct mjpeg_decoder));
  if (!d) return -ENOMEM;
  d->input = input;
  d->size = size;
  d->position = 0;
  JRESULT result = qpk_jd_prepare(&d->jpeg, jpeg_input, d->workspace,
                                  sizeof(d->workspace), d);
  if (result == JDR_OK)
    {
      *width = d->jpeg.width;
      *height = d->jpeg.height;
    }
  free(d);
  return result == JDR_OK ? 0 : -EBADMSG;
}

int qpk_mjpeg_decode(void *decoder, uint8_t *input, size_t size, size_t capacity,
                    uint16_t *output, unsigned width, unsigned height,
                    unsigned expected_width, unsigned expected_height)
{
  struct mjpeg_decoder *d = decoder;
  if (!d || !input || !output || !width || !height || size > capacity) return -EINVAL;
  int ret = jpeg_tables(input, &size, capacity);
  if (ret < 0) return ret;
  d->input = input; d->size = size; d->position = 0;
  if (qpk_jd_prepare(&d->jpeg, jpeg_input, d->workspace, sizeof(d->workspace), d) != JDR_OK) return -EBADMSG;
  if (d->jpeg.width != expected_width || d->jpeg.height != expected_height ||
      !expected_width || !expected_height || expected_width > 4096 || expected_height > 4096) return -EPROTO;
  unsigned scale = 0;
  while (scale < 3 && ((expected_width >> scale) > width || (expected_height >> scale) > height)) scale++;
  d->width = expected_width >> scale; d->height = expected_height >> scale;
  if (!d->width || !d->height || d->width > width || d->height > height) return -ENOTSUP;
  size_t pixels = (size_t)d->width * d->height;
  /* The AI preview exactly matches the reduced JPEG. Decode into its worker
   * buffer and publish only on success, avoiding a temporary frame and copy. */
  bool direct = d->width == width && d->height == height;
  d->pixels = direct ? output : malloc(pixels * 2); d->stride = d->width;
  if (!d->pixels) return -ENOMEM;
  JRESULT result = qpk_jd_decomp(&d->jpeg, jpeg_output, scale);
  if (result != JDR_OK) {
    if (!direct) free(d->pixels);
    d->pixels = NULL; return -EBADMSG;
  }
  if (direct) { d->pixels = NULL; return 0; }
  unsigned dw = width, dh = (uint64_t)d->height * width / d->width;
  if (dh > height) { dh = height; dw = (uint64_t)d->width * height / d->height; }
  unsigned ox = (width - dw) / 2, oy = (height - dh) / 2;
  memset(output, 0, (size_t)width * height * 2);
  for (unsigned y = 0; y < dh; y++)
    {
      const uint16_t *src = d->pixels + (size_t)(y * d->height / dh) * d->width;
      uint16_t *dst = output + (size_t)(y + oy) * width + ox;
      for (unsigned x = 0; x < dw; x++) dst[x] = src[x * d->width / dw];
    }
  free(d->pixels); d->pixels = NULL;
  return 0;
}
