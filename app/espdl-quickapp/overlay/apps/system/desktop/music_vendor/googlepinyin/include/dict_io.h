/* Local portability adapter, Apache-2.0. Fixed-width dictionary counts. */
#pragma once
#include <stdio.h>
#include <stdint.h>
static size_t gp_read_sizes(size_t *out, size_t count, FILE *fp) {
  for(size_t i=0;i<count;i++) { unsigned char b[4];
    if(fread(b,1,4,fp)!=4) return i;
    out[i]=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
  } return count;
}
static size_t gp_write_sizes(const size_t *in, size_t count, FILE *fp) {
  for(size_t i=0;i<count;i++) { if(in[i]>UINT32_MAX) return i;
    uint32_t n=in[i]; unsigned char b[4]={(unsigned char)n,(unsigned char)(n>>8),(unsigned char)(n>>16),(unsigned char)(n>>24)};
    if(fwrite(b,1,4,fp)!=4) return i;
  } return count;
}
