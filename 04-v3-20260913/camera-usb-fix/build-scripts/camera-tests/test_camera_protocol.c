/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera_uvc_protocol.h"
#include "qpk_mjpeg.h"

static const unsigned char descriptors[] = {
  9,2,107,0,2,1,0,0x80,100,
  9,4,0,0,0,14,1,0,0,
  13,0x24,1,0x10,1,13,0,0,0,0,0,1,3,
  9,4,3,0,0,14,2,0,0,
  13,0x24,1,1,0,0,0x81,0,0,0,0,1,0,
  11,0x24,6,2,1,0,1,0,0,0,0,
  34,0x24,7,1,0,0x80,2,0xe0,1,0,0,0,0,0,0,0,0,0,0,0x10,0,
    0x15,0x16,5,0,2,0x15,0x16,5,0,0x2a,0x2c,0x0a,0,
  9,4,3,1,1,14,2,0,0
};
static void parse_tests(void)
{
  struct uvc_descriptor_info out;
  unsigned char raw[sizeof(descriptors)];
  memcpy(raw, descriptors, sizeof(raw));
  raw[2] = sizeof(raw); raw[3] = sizeof(raw) >> 8;
  assert(camera_uvc_parse(raw, sizeof(raw), 3, &out) == 0);
  assert(out.control_interface == 0 && out.stream_interface == 3 && out.endpoint == 0x81);
  assert(out.caps.count == 1 && out.caps.uvc_version == 0x110);
  assert(out.caps.modes[0].width == 640 && out.caps.modes[0].height == 480);
  assert(out.caps.modes[0].format_index == 2 && out.caps.modes[0].frame_index == 1);
  assert(camera_uvc_interval_valid(&out.caps.modes[0], 333333));
  assert(camera_uvc_interval_valid(&out.caps.modes[0], 666666));
  assert(!camera_uvc_interval_valid(&out.caps.modes[0], 1));
  for (size_t n = 0; n < sizeof(raw); n++) assert(camera_uvc_parse(raw, n, 3, &out) < 0);
  raw[9] = 0; assert(camera_uvc_parse(raw, sizeof(raw), 3, &out) < 0);
  for (unsigned seed = 1; seed < 30000; seed++)
    {
      memcpy(raw, descriptors, sizeof(raw)); raw[2] = sizeof(raw); raw[3] = sizeof(raw) >> 8;
      unsigned x = seed * 1664525u + 1013904223u;
      for (int j = 0; j < 4; j++) { x = x * 1664525u + 1013904223u; raw[x % sizeof(raw)] = x >> 24; }
      camera_uvc_parse(raw, sizeof(raw), 3, &out);
    }
}
static void payload_tests(void)
{
  struct uvc_payload_state s = { .fid = -1 };
  unsigned char frame[32], a[] = {2,0,255,216,1}, b[] = {2,2,2,255,217};
  assert(camera_uvc_payload(&s, a, sizeof(a), frame, sizeof(frame)) == 0);
  assert(camera_uvc_payload(&s, b, sizeof(b), frame, sizeof(frame)) == 6);
  assert(frame[0] == 255 && frame[5] == 217);
  assert(camera_uvc_payload(&s, b, sizeof(b), frame, sizeof(frame)) == 0);
  a[1] = 1; b[1] = 3;
  assert(camera_uvc_payload(&s, a, sizeof(a), frame, 3) == 0);
  assert(camera_uvc_payload(&s, b, sizeof(b), frame, 3) < 0);
  a[1] = 0x40;
  assert(camera_uvc_payload(&s, a, sizeof(a), frame, sizeof(frame)) == 0);
  b[1] = 2; assert(camera_uvc_payload(&s, b, sizeof(b), frame, sizeof(frame)) == 0);
  unsigned char malformed[] = {12, 4};
  assert(camera_uvc_payload(&s, malformed, sizeof(malformed), frame, sizeof(frame)) < 0);
  a[1] = 1; b[1] = 3;
  assert(camera_uvc_payload(&s, a, sizeof(a), frame, sizeof(frame)) == 0);
  assert(camera_uvc_payload(&s, b, sizeof(b), frame, sizeof(frame)) == 6);
}
static void jpeg_tests(const char *path)
{
  FILE *f = fopen(path, "rb"); assert(f);
  fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
  unsigned char *input = malloc(size + 512), *copy = malloc(size + 512);
  assert(fread(input, 1, size, f) == (size_t)size); fclose(f);
  memcpy(copy, input, size);
  uint16_t *pixels = malloc(1024 * 600 * 2);
  void *decoder = qpk_mjpeg_create(); assert(decoder);
  assert(qpk_mjpeg_decode(decoder, input, size, size + 512, pixels, 1024, 600, 640, 480) == 0);
  size_t saved = qpk_mjpeg_frame_size(decoder);
  assert(saved >= (size_t)size && saved <= (size_t)size + 512);
  assert(input[saved - 2] == 0xff && input[saved - 1] == 0xd9);
  /* Saved JPEG is independently decodable, including a camera frame that
   * originally omitted DHT. It must not grow another table on a second pass. */
  void *reader = qpk_mjpeg_create(); assert(reader);
  assert(qpk_mjpeg_decode(reader, input, saved, size + 512, pixels, 1024, 600, 640, 480) == 0);
  assert(qpk_mjpeg_frame_size(reader) == saved);
  qpk_mjpeg_destroy(reader);
  assert(pixels[300 * 1024 + 512] != 0);
  /* 4:3 source remains 4:3: black side margins on a 1024x600 screen. */
  assert(pixels[300 * 1024] == 0 && pixels[300 * 1024 + 1023] == 0);
  /* Camera preview reserves rows 500..599 for controls. Neither clearing
   * nor scaling a new image may overwrite the existing toolbar. */
  for (unsigned i = 500 * 1024; i < 600 * 1024; i++) pixels[i] = 0x1082;
  assert(qpk_mjpeg_decode(decoder, input, saved, size + 512, pixels, 1024, 500, 640, 480) == 0);
  for (unsigned i = 500 * 1024; i < 600 * 1024; i++) assert(pixels[i] == 0x1082);
  memcpy(input, copy, size);
  assert(qpk_mjpeg_decode(decoder, input, size, size + 512, pixels, 1024, 600, 1280, 720) < 0);
  for (long n = 0; n < size; n += 31)
    { memcpy(input, copy, size); assert(qpk_mjpeg_decode(decoder, input, n, size + 512, pixels, 1024, 600, 640, 480) < 0); }
  qpk_mjpeg_destroy(decoder); free(input); free(copy); free(pixels);
}
int main(int argc, char **argv)
{
  assert(argc == 3); parse_tests(); payload_tests(); jpeg_tests(argv[1]); jpeg_tests(argv[2]);
  puts("PASS: UVC descriptors, 29999 malformed descriptors, MJPEG payloads, JPEG/DHT, aspect ratio, truncated input");
}
