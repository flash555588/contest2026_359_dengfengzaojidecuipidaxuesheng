/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_decoder.h"
#include "bili_sps.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned frames;
static int on_frame(void *, const uint8_t *const p[3], const int s[2], int w,int h)
{
  assert(p[0] && p[1] && p[2] && s[0] >= w && s[1] >= w/2);
  assert(w == 176 && h == 144); frames++; return 0;
}
int main(int argc, char **argv)
{
  assert(argc == 2);
  FILE *f = fopen(argv[1], "rb"); assert(f);
  fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
  uint8_t *p=static_cast<uint8_t *>(malloc(size)); assert(p);
  assert(fread(p,1,size,f)==static_cast<size_t>(size)); fclose(f);
  void *d=bili_decoder_create(on_frame,NULL); assert(d);
  /* Convert each 3/4-byte Annex-B start code to our FLV converter's 4-byte form. */
  size_t pos=0;
  while (pos < static_cast<size_t>(size)) {
    size_t prefix = 0;
    if (pos+4 <= static_cast<size_t>(size) && !memcmp(p+pos,"\0\0\0\1",4)) prefix=4;
    else if (pos+3 <= static_cast<size_t>(size) && !memcmp(p+pos,"\0\0\1",3)) prefix=3;
    if (!prefix) { pos++; continue; }
    size_t end=pos+prefix;
    while (end+3 <= static_cast<size_t>(size) && memcmp(p+end,"\0\0\1",3) &&
           !(end+4 <= static_cast<size_t>(size) && !memcmp(p+end,"\0\0\0\1",4))) end++;
    if (end+3 > static_cast<size_t>(size)) end=size;
    size_t n=end-pos-prefix;
    uint8_t *nal=static_cast<uint8_t *>(malloc(n+4)); assert(nal);
    memcpy(nal,"\0\0\0\1",4); memcpy(nal+4,p+pos+prefix,n);
    int result=bili_decoder_feed(d,nal,n+4);
    if (result) fprintf(stderr,"NAL type=%u size=%zu decode=%d\n",nal[4]&31,n,result);
    assert(result==0); free(nal);
    pos=end;
  }
  assert(frames > 0);
  bili_decoder_destroy(d); free(p);
  puts("PASS: real H.264 QCIF -> OpenH264 -> I420, 176x144");
  return 0;
}
