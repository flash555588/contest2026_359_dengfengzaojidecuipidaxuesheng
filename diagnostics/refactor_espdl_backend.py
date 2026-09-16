"""Apply the two-mode, persistent-session backend to the initial port."""
from pathlib import Path
p=Path(__file__).resolve().parent.parent/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop/espdl_port/inference_backend.cpp'
s=p.read_text()
s=s.replace('#include "dl_detect_pico_postprocessor.hpp"\n','')
s=s.replace('extern "C" int qpk_dl_digit_run(const void *, size_t, const uint8_t *, float[10]);\n','')
a=s.index('    const char *allowed[]=')
b=s.index('    for (const auto &node:',a)
s=s[:a]+'''    const char *allowed[]={"Conv","PRelu","Concat","Add","RequantizeLinear",
                           "GlobalAveragePool","Transpose","Flatten","Gemm"};
'''+s[b:]
a=s.index('extern "C" int qpk_dl_backend(')
b=s.index('  if (mode==QPK_DL_CLASSIFY) {',s.index('  Model first;',a))
s=s[:a]+'''struct Session {
  qpk_dl_mode mode;
  Model first,second;
};
extern "C" int qpk_dl_backend_open(enum qpk_dl_mode mode,void **context)
{
  if (!context || mode<QPK_DL_CLASSIFY || mode>QPK_DL_FACE) return -EINVAL;
  *context=nullptr;
  std::unique_ptr<Session> session(new Session());
  session->mode=mode;
  int ret=session->first.load(mode==QPK_DL_CLASSIFY?0:1);
  if (!ret && mode==QPK_DL_FACE) ret=session->second.load(2);
  if (ret) return ret;
  *context=session.release();return 0;
}
extern "C" void qpk_dl_backend_close(void *context) {delete static_cast<Session *>(context);}
extern "C" int qpk_dl_backend_run(void *context,const uint16_t *pixels,struct qpk_dl_result *out)
{
  if (!context || !pixels || !out) return -EINVAL;
  auto &session=*static_cast<Session *>(context);
  auto mode=session.mode;
  auto &first=session.first;
  out->count=0;
  dl::image::img_t image={const_cast<uint16_t *>(pixels),QPK_DL_WIDTH,QPK_DL_HEIGHT,
                          dl::image::DL_IMAGE_PIX_TYPE_RGB565};
'''+s[b:]
a=s.index('  if (mode==QPK_DL_PEDESTRIAN)')
b=s.index('  dl::detect::MSRPostprocessor',a)
s=s[:a]+s[b:]
s=s.replace('  Model second;\n  ret=second.load(2);\n  if (ret) return ret;', '  auto &second=session.second;')
p.write_text(s)
