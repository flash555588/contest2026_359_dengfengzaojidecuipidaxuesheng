"""Compare the actual MJPEG decoder's fixture pixels with Pillow/libjpeg."""
from pathlib import Path
import ctypes as C
import io
import json
import subprocess
import numpy as np
from PIL import Image

ws=Path(__file__).resolve().parent.parent
app=ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
fixture=ws/'diagnostics/espdl-reference/esp-dl/examples/human_face_detect/main/human_face.jpg'
so=Path('/tmp/espdl-mjpeg-pixels.so')
subprocess.run(['gcc','-O2','-shared','-fPIC','-I'+str(app),str(app/'qpk_mjpeg.c'),
                str(app/'qpk_tjpgd.c'),'-o',str(so)],check=True)
lib=C.CDLL(str(so))
lib.qpk_mjpeg_create.restype=C.c_void_p
lib.qpk_mjpeg_decode.argtypes=[C.c_void_p,C.c_void_p,C.c_size_t,C.c_size_t,C.c_void_p,
                             C.c_uint,C.c_uint,C.c_uint,C.c_uint]
lib.qpk_mjpeg_destroy.argtypes=[C.c_void_p]
evidence=ws/'04-v3-20260913/espdl-quickapp/evidence'
report=[]
for width,height in [(320,240),(640,480)]:
    if width==320:
        data=fixture.read_bytes()
    else:
        stream=io.BytesIO()
        Image.open(fixture).resize((width,height)).save(stream,format='JPEG',quality=95)
        data=stream.getvalue()
    original=Image.open(io.BytesIO(data)).convert('RGB')
    for scale in range(4):
        dw,dh=width>>scale,height>>scale
        jpeg=C.create_string_buffer(data,len(data)+512)
        pixels=np.empty((dh,dw),dtype=np.uint16)
        decoder=lib.qpk_mjpeg_create()
        try:
            ret=lib.qpk_mjpeg_decode(decoder,jpeg,len(data),len(data)+512,pixels.ctypes.data,dw,dh,width,height)
            assert ret==0,ret
        finally:
            lib.qpk_mjpeg_destroy(decoder)
        rgb=np.stack(((pixels>>11)<<3,((pixels>>5)&63)<<2,(pixels&31)<<3),axis=-1).astype(np.uint8)
        reference=np.array(original.reduce(1<<scale))
        error=np.abs(rgb.astype(np.int16)-reference)
        result={'source':[width,height],'output':[dw,dh],'mean_absolute_error':float(error.mean()),
                'maximum_error':int(error.max())}
        report.append(result)
        if width==320 and scale==0:
            Image.fromarray(rgb).save(evidence/'face-fixture-decoded.png')
        assert error.mean()<6 and error.max()<40,result
(evidence/'face-decoder-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
