"""Host behavior tests of the actual C worker/tracker/SHA and quick-app JS."""
from pathlib import Path
import subprocess
import json
import ctypes
import hashlib
ws=Path(__file__).resolve().parent.parent
d=ws/'04-v3-20260913/espdl-quickapp'
p=d/'overlay/apps/system/desktop'
b=Path('/tmp/espdl-tests');b.mkdir(exist_ok=True)
(b/'nuttx/video').mkdir(parents=True,exist_ok=True)
(b/'nuttx/config.h').write_text('')
(b/'nuttx/video/uvc_camera.h').write_text('#define UVC_CAMERA_MAX_DEVICES 2\n')
cmd=['gcc','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
     '-I'+str(p),'-I'+str(b),str(d/'tests/test_service.c'),str(p/'qpk_espdl_service.c'),
     str(p/'qpk_espdl_track.c'),'-o',str(b/'test-service')]
subprocess.run(cmd,check=True)
subprocess.run([str(b/'test-service')],check=True)
subprocess.run(['gcc','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                '-I'+str(p),str(d/'tests/test_mjpeg.c'),str(p/'qpk_mjpeg.c'),
                str(p/'qpk_tjpgd.c'),'-o',str(b/'test-mjpeg')],check=True)
subprocess.run([str(b/'test-mjpeg'),str(ws/'diagnostics/espdl-reference/esp-dl/examples/human_face_detect/main/human_face.jpg')],check=True)
subprocess.run(['gcc','-shared','-fPIC','-I'+str(p),str(p/'qpk_espdl_sha256.c'),'-o',str(b/'sha.so')],check=True)
sha=ctypes.CDLL(str(b/'sha.so')).qpk_dl_sha256
sha.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p]
blobs=[b'',b'abc',b'a'*1000000]+[bytes(range(256))[:n] for n in [1,55,56,63,64,65,127,128,129,255]]
manifest=json.loads((d/'models/manifest.json').read_text())
blobs += [(d/'models'/a['file']).read_bytes() for a in manifest['records']]
for data in blobs:
    out=ctypes.create_string_buffer(32);sha(data,len(data),out)
    assert out.raw==hashlib.sha256(data).digest()
print('PASS: SHA256 standard vectors, padding boundaries and all packaged models')
(d/'evidence/host-validation.json').write_text(json.dumps({'service_tracker_sanitizers':True,
 'mjpeg_four_scales_sanitizers':True,
 'sha256_vectors_and_models':len(blobs),'hardware_tested':False},indent=2)+'\n')
