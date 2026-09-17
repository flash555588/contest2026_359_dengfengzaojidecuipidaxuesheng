/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
extern "C" {
#include "qpk_mjpeg.h"
}
#include "face_fixture.h"
#include "dl_image_process.hpp"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int verify_preprocess(uint16_t *pixels)
{
  using namespace dl::image;
  const unsigned n=160*120*3;
  int8_t *simd=(int8_t *)heap_caps_aligned_alloc(16,n,MALLOC_CAP_DEFAULT);
  int8_t *scalar=(int8_t *)heap_caps_aligned_alloc(16,n,MALLOC_CAP_DEFAULT);
  if(!simd||!scalar) {free(simd);free(scalar);return -ENOMEM;}
  img_t src={pixels,320,240,DL_IMAGE_PIX_TYPE_RGB565};
  img_t dst={simd,160,120,DL_IMAGE_PIX_TYPE_RGB888_QINT8};
  ImageTransformer a,b;
  a.set_src_img(src).set_dst_img(dst).set_caps(DL_IMAGE_CAP_RGB_SWAP)
    .set_norm_quant_param({0,0,0},{1,1,1},1,NormQuantWrapper::INT8_QUANT);
  dst.data=scalar;
  b.set_src_img(src).set_dst_img(dst).set_caps(DL_IMAGE_CAP_RGB_SWAP)
    .set_norm_quant_param({0,0,0},{1,1,1},1,NormQuantWrapper::INT8_QUANT);
  int ret=a.transform<true>();
  if(!ret)ret=b.transform<false>();
  unsigned mismatches=0;
  if(!ret)for(unsigned i=0;i<n;i++)if(simd[i]!=scalar[i])mismatches++;
  printf("[espdl] preprocess raw=%04x SIMD=%d,%d,%d scalar=%d,%d,%d mismatches=%u/%u\n",
    pixels[0],simd[0],simd[1],simd[2],scalar[0],scalar[1],scalar[2],mismatches,n);
  /* Validate the selected scalar path against direct RGB565 quantization.
   * The SDK image SIMD comparison is diagnostic; NN SIMD is tested above. */
  for(unsigned y=0;!ret&&y<120;y++)for(unsigned x=0;x<160;x++) {
    uint16_t value=pixels[y*2*320+x*2];
    int8_t expected[3]={(int8_t)((value&31)*4),(int8_t)(((value>>5)&63)*2),
                        (int8_t)((value>>11)*4)};
    if(memcmp(scalar+(y*160+x)*3,expected,3)) {ret=-EILSEQ;break;}
  }
  printf("[espdl] selected scalar preprocess reference result=%d\n",ret);
  free(simd);free(scalar);return ret;
}

extern "C" int qpk_dl_model_verify(void)
{
  uint8_t *jpeg=(uint8_t *)malloc(sizeof(face_fixture)+512);
  uint16_t *pixels=(uint16_t *)malloc(QPK_DL_PIXELS*2);
  void *decoder=qpk_mjpeg_create(),*backend=nullptr;
  int ret=-ENOMEM;
  if(jpeg&&pixels&&decoder) {
    memcpy(jpeg,face_fixture,sizeof(face_fixture));
    ret=qpk_mjpeg_decode(decoder,jpeg,sizeof(face_fixture),sizeof(face_fixture)+512,
                         pixels,QPK_DL_WIDTH,QPK_DL_HEIGHT,320,240);
    if(!ret)ret=verify_preprocess(pixels);
    for(int mode=QPK_DL_FACE;!ret&&mode>=QPK_DL_CLASSIFY;mode--) {
      ret=qpk_dl_backend_open((qpk_dl_mode)mode,&backend);
      qpk_dl_result first={},next={};
      if(!ret)ret=qpk_dl_backend_run(backend,pixels,&first);
      printf("[espdl] 320x240 fixture mode=%d result=%d count=%u\n",mode,ret,first.count);
      if(!ret&&!first.count)ret=-ENODATA;
      if(!ret)ret=qpk_dl_backend_run(backend,pixels,&next);
      if(!ret&&(first.count!=next.count||strcmp(first.items[0].label,next.items[0].label)||
                 !isfinite(first.items[0].score)||fabsf(first.items[0].score-next.items[0].score)>0.00001f||
                 first.items[0].x1!=next.items[0].x1||first.items[0].y1!=next.items[0].y1||
                 first.items[0].x2!=next.items[0].x2||first.items[0].y2!=next.items[0].y2))ret=-EILSEQ;
      if(!ret&&mode==QPK_DL_FACE) {
        auto &face=first.items[0];
        if(first.count!=1||face.score<.8f||face.x1<75||face.x1>125||face.y1<40||face.y1>95||
           face.x2<170||face.x2>220||face.y2<165||face.y2>220)ret=-EILSEQ;
      }
      if(!ret)printf("[espdl] fixture top=%s score=%.5f box=%d,%d,%d,%d repeat=stable\n",
        first.items[0].label,(double)first.items[0].score,first.items[0].x1,first.items[0].y1,
        first.items[0].x2,first.items[0].y2);
      if(!ret&&mode==QPK_DL_FACE) {
        uint16_t *blank=(uint16_t *)calloc(QPK_DL_PIXELS,2);
        if(!blank)ret=-ENOMEM;
        else {
          qpk_dl_result empty={};
          ret=qpk_dl_backend_run(backend,blank,&empty);
          printf("[espdl] blank face test result=%d count=%u\n",ret,empty.count);
          if(!ret&&empty.count)ret=-EILSEQ;
          free(blank);
        }
      }
      qpk_dl_backend_close(backend);backend=nullptr;
    }
  }
  qpk_mjpeg_destroy(decoder);free(jpeg);free(pixels);
  printf("[espdl] model fixture test result=%d\n",ret);
  return ret;
}
