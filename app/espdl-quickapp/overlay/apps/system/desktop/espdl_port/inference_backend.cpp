/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_detect_msr_postprocessor.hpp"
#include "dl_detect_mnp_postprocessor.hpp"
#include "imagenet_cls_postprocessor.hpp"
#include <algorithm>
#include <errno.h>
#include <memory>
#include <stdio.h>


namespace {
bool verifying=false;
void trace_tensor(const char *name,dl::TensorBase *tensor)
{
  if (!tensor || tensor->dtype!=dl::DATA_TYPE_INT8) return;
  const int8_t *p=static_cast<const int8_t *>(tensor->data);
  int low=127,high=-128;uint32_t hash=2166136261u;
  for(int i=0;i<tensor->get_size();i++) {
    low=std::min(low,(int)p[i]);high=std::max(high,(int)p[i]);
    hash=(hash^(uint8_t)p[i])*16777619u;
  }
  printf("[espdl] %s exp=%d n=%d range=%d..%d hash=%08lx shape=",name,
    tensor->exponent,tensor->get_size(),low,high,(unsigned long)hash);
  for(int size:tensor->shape)printf("%d,",size);
  printf(" first=");
  for(int i=0;i<std::min(3,tensor->get_size());i++)printf("%d,",p[i]);
  printf("\n");
}
class Asset {
public:
  void *data=nullptr;
  size_t bytes=0;
  ~Asset() { qpk_dl_model_free(data); }
  int load(unsigned index) { return qpk_dl_model_load(index,&data,&bytes); }
};
class Model {
  Asset asset;
  std::unique_ptr<fbs::FbsModel> flatbuffer;
public:
  std::unique_ptr<dl::Model> model;
  int load(unsigned index)
  {
    qpk_dl_set_stage(QPK_DL_LOAD);
    int ret=asset.load(index);
    if (ret) return ret;
    const uint8_t *data=static_cast<const uint8_t *>(asset.data);
    uint32_t header[4];memcpy(header,data,16);
    if (memcmp(data,"EDL2",4) || header[1] || header[3] ||
        header[2]>asset.bytes-16 || header[2]<16) return -EBADMSG;
    // Only exact SHA256-pinned model files reach the vendor FlatBuffer parser.
    flatbuffer.reset(new fbs::FbsModel(data+16,header[2],fbs::MODEL_LOCATION_IN_SDCARD,
                                      false,false,false,false));
    flatbuffer->load_map();
    const char *allowed[]={"Conv","PRelu","Concat","Add","RequantizeLinear",
                           "GlobalAveragePool","Transpose","Flatten","Gemm"};
    for (const auto &node:flatbuffer->topological_sort()) {
      auto operation=flatbuffer->get_operation_type(node);
      bool found=false;
      for (const char *name:allowed) if (operation==name) { found=true;break; }
      if (!found) { printf("[espdl] Unsupported model operator: %s\n",operation.c_str());return -ENOTSUP; }
    }
    if (qpk_dl_should_cancel()) return -ECANCELED;
    model.reset(new dl::Model(flatbuffer.get(),0));
    if (model->get_inputs().size()!=1 || model->get_outputs().empty()) return -EBADMSG;
    dl::TensorBase *input=model->get_input();
    if (!input || !input->data || input->shape.size()!=4 || input->shape[3]!=3) return -EBADMSG;
    return 0;
  }
  bool run()
  {
    qpk_dl_set_stage(QPK_DL_INFER);
    return model->run_cancellable(qpk_dl_should_cancel);
  }
};
void boxes(std::list<dl::detect::result_t> &items,const char *label,struct qpk_dl_result *out)
{
  for (auto &item:items) {
    if (out->count==QPK_DL_MAX_RESULTS) break;
    if (item.box.size()!=4 || item.box[2]<=item.box[0] || item.box[3]<=item.box[1]) continue;
    auto &dst=out->items[out->count++];
    snprintf(dst.label,sizeof(dst.label),"%s",label);
    dst.score=item.score;
    dst.x1=item.box[0];dst.y1=item.box[1];dst.x2=item.box[2];dst.y2=item.box[3];
  }
}
}

struct Session {
  qpk_dl_mode mode;
  Model first,second;
  std::unique_ptr<dl::image::ImagePreprocessor> pre,refine;
  std::unique_ptr<dl::detect::MSRPostprocessor> msr;
  std::unique_ptr<dl::detect::MNPPostprocessor> mnp;
  unsigned runs=0;
  void prepare()
  {
    if(mode==QPK_DL_FACE) {
      unsigned caps=dl::image::DL_IMAGE_CAP_RGB_SWAP;
      pre.reset(new dl::image::ImagePreprocessor(first.model.get(),{0,0,0},{1,1,1},caps));
      refine.reset(new dl::image::ImagePreprocessor(second.model.get(),{0,0,0},{1,1,1},caps));
      msr.reset(new dl::detect::MSRPostprocessor(first.model.get(),pre.get(),.3f,.5f,10,
        {{8,8,9,9,{{16,16},{32,32}}},{16,16,9,9,{{64,64},{128,128}}}}));
      mnp.reset(new dl::detect::MNPPostprocessor(second.model.get(),refine.get(),.4f,.3f,10,
        {{1,1,0,0,{{48,48}}}}));
    } else {
      pre.reset(new dl::image::ImagePreprocessor(first.model.get(),
        {123.675,116.28,103.53},{58.395,57.12,57.375}));
    }
  }
};
extern "C" int qpk_dl_backend_open(enum qpk_dl_mode mode,void **context)
{
  if (!context || mode<QPK_DL_CLASSIFY || mode>QPK_DL_FACE) return -EINVAL;
  *context=nullptr;
  /* This state follows the worker across preemption and CPU migration. */
  __asm__ volatile("csrwi 0x7f2, 1\ncsrwi 0x7f1, 1" ::: "memory");
  dl_esp32p4_cfg_round(ROUND_MODE_HALF_EVEN);
  std::unique_ptr<Session> session(new Session());
  session->mode=mode;
  int ret=session->first.load(mode==QPK_DL_CLASSIFY?0:1);
  if (!ret && mode==QPK_DL_FACE) ret=session->second.load(2);
  if (ret) return ret;
  session->prepare();
  *context=session.release();return 0;
}
extern "C" void qpk_dl_backend_close(void *context) {delete static_cast<Session *>(context);}
extern "C" int qpk_dl_backend_verify(void *context)
{
  /* The verifier also preprocesses images before opening a model session. */
  __asm__ volatile("csrwi 0x7f2, 1\ncsrwi 0x7f1, 1" ::: "memory");
  dl_esp32p4_cfg_round(ROUND_MODE_HALF_EVEN);
  extern int qpk_dl_kernel_verify(void);
  extern int qpk_dl_model_verify(void);
  int ret=qpk_dl_kernel_verify();
  verifying=true;
  if(!ret)ret=qpk_dl_model_verify();
  verifying=false;
  return ret;
}
extern "C" int qpk_dl_backend_run(void *context,const uint16_t *pixels,struct qpk_dl_result *out)
{
  if (!context || !pixels || !out) return -EINVAL;
  auto &session=*static_cast<Session *>(context);
  auto mode=session.mode;
  auto &first=session.first;
  auto &pre=*session.pre;
  bool trace=verifying && session.runs++<2;
  out->count=0;
  dl::image::img_t image={const_cast<uint16_t *>(pixels),QPK_DL_WIDTH,QPK_DL_HEIGHT,
                          dl::image::DL_IMAGE_PIX_TYPE_RGB565};
  if (mode==QPK_DL_CLASSIFY) {
    pre.preprocess(image);
    if (!first.run()) return -ECANCELED;
    dl::cls::ImageNetClsPostprocessor post(first.model.get(),3,0,true);
    for (auto &item:post.postprocess()) {
      if (out->count==3) break;
      auto &dst=out->items[out->count++];
      snprintf(dst.label,sizeof(dst.label),"%s",item.cat_name);dst.score=item.score;
    }
    return 0;
  }
  pre.preprocess(image);
  if(trace)trace_tensor("msr input",first.model->get_input());
  if (!first.run()) return -ECANCELED;
  if(trace)for(auto &output:first.model->get_outputs())trace_tensor(output.first.c_str(),output.second);
  auto &msr=*session.msr;
  msr.clear_result();msr.postprocess();
  auto candidates=msr.get_result(image.width,image.height);
  if(trace)printf("[espdl] msr candidates=%u scale=%f,%f\n",(unsigned)candidates.size(),
    (double)pre.get_resize_scale_x(true),(double)pre.get_resize_scale_y(true));
  if (candidates.empty()) return 0;
  auto &second=session.second;
  auto &refine=*session.refine;
  auto &mnp=*session.mnp;
  mnp.clear_result();
  for (auto &candidate:candidates) {
    if (qpk_dl_should_cancel()) return -ECANCELED;
    int cx=(candidate.box[0]+candidate.box[2])/2,cy=(candidate.box[1]+candidate.box[3])/2;
    int side=std::max(candidate.box[2]-candidate.box[0],candidate.box[3]-candidate.box[1]);
    candidate.box={cx-side/2,cy-side/2,cx-side/2+side,cy-side/2+side};
    candidate.limit_box(image.width,image.height);
    if (candidate.box[2]<=candidate.box[0] || candidate.box[3]<=candidate.box[1]) continue;
    refine.preprocess(image,candidate.box);
    if (!second.run()) return -ECANCELED;
    if(trace)for(auto &output:second.model->get_outputs())trace_tensor(output.first.c_str(),output.second);
    mnp.postprocess();
  }
  mnp.nms();boxes(mnp.get_result(image.width,image.height),"人脸",out);
  return 0;
}
