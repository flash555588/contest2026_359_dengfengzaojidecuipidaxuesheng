/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include "dl_module_gemm.hpp"
#include <algorithm>
#include <cmath>
#include <errno.h>
#include <memory>

extern "C" int qpk_dl_digit_run(const void *model_data, size_t size,
                                 const uint8_t *image, float scores[10])
{
  if (!model_data || !image || !scores || size != 102248) return -EINVAL;
  const uint8_t *blob=static_cast<const uint8_t *>(model_data);
  if (memcmp(blob,"QDM1",4)) return -EBADMSG;
  uint32_t dims[3]; int32_t exp[5];
  memcpy(dims,blob+4,sizeof(dims));memcpy(exp,blob+16,sizeof(exp));
  if (dims[0]!=784 || dims[1]!=128 || dims[2]!=10) return -EBADMSG;
  for (int e:exp) if (e < -24 || e > 16) return -EBADMSG;
  using namespace dl;
  TensorBase input({1,784},nullptr,exp[0],DATA_TYPE_INT8);
  TensorBase hidden({1,128},nullptr,exp[2],DATA_TYPE_INT8);
  TensorBase output({1,10},nullptr,exp[4],DATA_TYPE_INT8);
  TensorBase w1({1,1,784,128},blob+64,exp[1],DATA_TYPE_INT8,false);
  TensorBase b1({128},blob+64+784*128,exp[0]+exp[1],DATA_TYPE_INT32,false);
  TensorBase w2({1,1,128,10},blob+64+784*128+128*4,exp[3],DATA_TYPE_INT8,false);
  TensorBase b2({10},blob+64+784*128+128*4+128*10,exp[2]+exp[3],DATA_TYPE_INT32,false);
  if (!input.data || !hidden.data || !output.data) return -ENOMEM;
  for (int i=0;i<784;i++)
    static_cast<int8_t *>(input.data)[i]=quantize<int8_t>(image[i]/255.f,ldexpf(1.f,-exp[0]));
  module::Gemm layer1(ReLU,nullptr,QUANT_TYPE_SYMM_8BIT);
  module::Gemm layer2(Linear,nullptr,QUANT_TYPE_SYMM_8BIT);
  if (qpk_dl_should_cancel()) return -ECANCELED;
  layer1.run({&input,&w1,&b1},{&hidden},RUNTIME_MODE_SINGLE_CORE);
  if (qpk_dl_should_cancel()) return -ECANCELED;
  layer2.run({&hidden,&w2,&b2},{&output},RUNTIME_MODE_SINGLE_CORE);
  float max_score=-INFINITY, total=0;
  for (int i=0;i<10;i++) {
    scores[i]=static_cast<int8_t *>(output.data)[i]*ldexpf(1.f,exp[4]);
    max_score=std::max(max_score,scores[i]);
  }
  for (int i=0;i<10;i++) { scores[i]=expf(scores[i]-max_score);total+=scores[i]; }
  for (int i=0;i<10;i++) scores[i]/=total;
  return qpk_dl_should_cancel()?-ECANCELED:0;
}
