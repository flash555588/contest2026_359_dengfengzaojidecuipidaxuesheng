/* SPDX-License-Identifier: Apache-2.0 */
/* Optional external probe. No cookie, signed URL or response body is logged. */
#include "bili_http.h"
#include "bili_api.h"
#include "live_platform.h"
#include "bili_flv.h"
#include "bili_decoder.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
struct probe { struct bili_http *http; void *decoder; unsigned frames; };
static int read_stream(void *ctx, uint8_t *p, size_t n)
{
  return bili_http_read(((struct probe *)ctx)->http,p,n);
}
static int frame(void *ctx,const uint8_t *const p[3],const int s[2],int w,int h)
{
  struct probe *probe=ctx;
  (void)p; (void)s;
  if (++probe->frames == 1) printf("Decoded live frame: %dx%d\n",w,h);
  return probe->frames >= 3 ? -ECANCELED : 0;
}
static int video(void *ctx,const uint8_t *p,size_t n,uint32_t ts)
{
  (void)ts; return bili_decoder_feed(((struct probe *)ctx)->decoder,p,n);
}
int main(int argc,char **argv)
{
  atomic_bool cancel=false;
  int platform=argc==3?atoi(argv[2]):LIVE_BILI;
  int r;
  struct bili_streams *streams=calloc(1,sizeof(*streams));
  struct probe probe={0};
  struct bili_flv f={read_stream,video,&probe,0,0,NULL,NULL};
  if ((argc != 2 && argc != 3) || !bili_valid_room(argv[1]) || !streams) return 2;
  r=live_resolve(platform,argv[1],"",&cancel,streams);
  if(r){printf("Resolve platform=%d failed: %d\n",platform,r);free(streams);return 1;}
  printf("Platform=%d room=%s requested=%d lines=%d\n",platform,argv[1],streams->qn,streams->count);
  f.tag=malloc(BILI_TAG_LIMIT); f.annexb=malloc(BILI_AU_LIMIT);
  for (int line=0;line<streams->count && line<3;line++) {
    probe.decoder=bili_decoder_create(frame,&probe);
    probe.http=bili_http_open_site(streams->urls[line],"",streams->referer,&cancel,&r);
    f.configured=0;
    if (probe.http && probe.decoder && f.tag && f.annexb) r=bili_flv_run(&f);
    bili_http_close(probe.http); bili_decoder_destroy(probe.decoder);
    printf("CDN line %d result %d\n",line+1,r);
    if (probe.frames >= 3) break;
  }
  free(f.tag); free(f.annexb); free(streams);
  printf("Probe result %d; decoded %u live frames (host only)\n",r,probe.frames);
  return probe.frames >= 3 ? 0 : 1;
}
