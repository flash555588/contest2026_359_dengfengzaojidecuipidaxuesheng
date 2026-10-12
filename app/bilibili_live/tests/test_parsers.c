/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_api.h"
#include "bili_flv.h"
#include "bili_sps.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct fixture { uint8_t data[512]; size_t n, pos; int frames; uint32_t expected[4]; };
static int read_fragment(void *ctx, uint8_t *p, size_t n)
{
  struct fixture *f = ctx;
  if (n > 3) n = 3;
  if (n > f->n - f->pos) n = f->n - f->pos;
  memcpy(p, f->data + f->pos, n); f->pos += n; return n;
}
static int video(void *ctx, const uint8_t *p, size_t n, uint32_t ts)
{
  struct fixture *f = ctx;
  assert(n >= 5 && !memcmp(p, "\0\0\0\1", 4));
  if (f->frames == 1) assert(ts == (f->expected[1] ? f->expected[1] : 0x01000028));
  if (f->frames == 2 && f->expected[2]) assert(ts == f->expected[2]);
  f->frames++; return 0;
}
static void tag(struct fixture *f, const uint8_t *p, size_t n, uint32_t ts)
{
  uint8_t h[11] = {9, 0, (uint8_t)(n >> 8), (uint8_t)n,
    (uint8_t)(ts >> 16), (uint8_t)(ts >> 8), (uint8_t)ts, (uint8_t)(ts >> 24), 0, 0, 0};
  memcpy(f->data + f->n, h, 11); f->n += 11;
  memcpy(f->data + f->n, p, n); f->n += n;
  memset(f->data + f->n, 0, 4);
  f->data[f->n + 3] = 11 + n; f->n += 4;
}
static void flv_tests(void)
{
  struct fixture fixture = {{'F','L','V',1,1,0,0,0,9,0,0,0,0},13,0,0};
  uint8_t config[] = {0x17,0,0,0,0,1,66,0,30,0xff,0xe1,0,4,0x67,66,0,30,1,0,2,0x68,0};
  uint8_t packet[] = {0x27,1,0,0,0,0,0,0,2,0x41,0x80};
  uint8_t end[] = {0x17,2,0,0,0};
  struct bili_flv f = {read_fragment,video,&fixture,0,0,NULL,NULL};
  size_t i, total;
  f.tag = malloc(BILI_TAG_LIMIT); f.annexb = malloc(BILI_AU_LIMIT);
  assert(f.tag && f.annexb);
  tag(&fixture, config, sizeof(config), 0);
  tag(&fixture, packet, sizeof(packet), 0x01000028);
  tag(&fixture, end, sizeof(end), 0);
  total = fixture.n;
  assert(bili_flv_run(&f) == 0 && fixture.frames == 2);
  for (i = 0; i < total; i++) {
    fixture.n = i; fixture.pos = 0; fixture.frames = 0; f.configured = 0;
    assert(bili_flv_run(&f) < 0);
  }
  fixture.n = total; fixture.pos = 0; fixture.frames = 0;
  fixture.data[13 + 11 + sizeof(config) + 3] ^= 1;
  assert(bili_flv_run(&f) == -EPROTO);
  f.configured = 0;
  assert(bili_avc_packet(&f, packet + 5, sizeof(packet) - 5, 0) == -EPROTO);
  assert(bili_avc_config(&f, config + 5, 2) == -EPROTO);
  config[9] = 0xfe; /* Reserved lengthSizeMinusOne = 2. */
  assert(bili_avc_config(&f, config + 5, sizeof(config) - 5) == -EPROTO);
  /* B-frame presentation offsets must be signed and wrap at 32 bits. */
  struct fixture cts = {{'F','L','V',1,1,0,0,0,9,0,0,0,0},13,0,0,{0,116,0xfffffff0u}};
  config[9] = 0xff;
  tag(&cts, config, sizeof(config), 0);
  packet[4] = 16; tag(&cts, packet, sizeof(packet), 100);
  packet[2] = packet[3] = 0xff; packet[4] = 0xe0;
  tag(&cts, packet, sizeof(packet), 16);
  tag(&cts, end, sizeof(end), 0);
  f.opaque = &cts; f.configured = 0;
  assert(bili_flv_run(&f) == 0 && cts.frames == 3);
  free(f.tag); free(f.annexb); f.tag=NULL;f.annexb=NULL;
  f.managed_buffers=true;f.configured=0;cts.pos=0;cts.frames=0;
  assert(bili_flv_run(&f)==0&&cts.frames==3);
  assert(f.tag_capacity==16384&&f.annexb_capacity==16384);
  free(f.tag);free(f.annexb);
}
static void api_tests(void)
{
  struct bili_streams *s = malloc(sizeof(*s));
  unsigned long id;
  const char *json = "{\"code\":0,\"data\":{\"playurl_info\":{\"playurl\":{\"stream\":["
    "{\"protocol_name\":\"http_stream\",\"format\":[{\"format_name\":\"flv\",\"codec\":["
    "{\"codec_name\":\"avc\",\"current_qn\":250,\"base_url\":\"/live.flv\",\"url_info\":["
    "{\"host\":\"https://cdn.example\",\"extra\":\"?token=a%2Fb\"},"
    "{\"host\":\"https://cdn.example\",\"extra\":\"?token=a%2Fb\"}]}]}]}]}}}}";
  assert(bili_play_info(json,s) == 0 && s->count == 1 && s->qn == 250);
  assert(!strcmp(s->urls[0],"https://cdn.example/live.flv?token=a%2Fb"));
  assert(bili_play_info("{\"code\":-403}",s) < 0);
  assert(bili_play_info("{\"code\":0,\"data\":{}}",s) < 0);
  assert(bili_room_id("{\"code\":0,\"data\":{\"room_id\":12345,\"live_status\":1}}",&id) == 0 && id == 12345);
  assert(bili_room_id("{\"code\":0,\"data\":{\"room_id\":12345,\"live_status\":2}}",&id) == -ENODATA);
  assert(bili_room_id("{\"code\":0,\"data\":{\"room_id\":1e50,\"live_status\":1}}",&id) < 0);
  assert(bili_valid_room("12345") && !bili_valid_room("0") && !bili_valid_room("1&qn=10000"));
  free(s);
}
int main(void)
{
  uint8_t junk[512]; unsigned x = 1; int w,h; size_t i,j;
  flv_tests(); api_tests();
  for (i = 0; i < 20000; i++) {
    for (j = 0; j < sizeof(junk); j++) { x = x * 1664525u + 1013904223u; junk[j] = x >> 24; }
    junk[0] = 0x67;
    (void)bili_sps_dimensions(junk, (i % 512) + 1, &w, &h);
  }
  puts("PASS: fragmented FLV, every truncation, AVC lengths, API/quality, room IDs, 20000 SPS fuzz cases");
  return 0;
}
