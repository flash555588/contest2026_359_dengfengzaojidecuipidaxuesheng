"""Run host sanitizers on device-provided descriptors, payloads and JPEGs."""
from pathlib import Path
import subprocess
ws = Path(__file__).resolve().parent.parent
o = ws / '04-v3-20260913/camera-usb-fix/overlay'
chip = o / 'nuttx/arch/risc-v/src/esp32p4/camera_usb'
app = o / 'apps/system/desktop'
tests = ws / 'diagnostics/camera-tests'
exe = Path('/tmp/v3-camera-protocol-test')
cmd = ['gcc', '-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
       '-I' + str(o / 'nuttx/include'), '-I' + str(chip), '-I' + str(app),
       str(tests / 'test_camera_protocol.c'), str(chip / 'camera_uvc_protocol.c'),
       str(app / 'qpk_mjpeg.c'), str(app / 'qpk_tjpgd.c'), '-o', str(exe)]
subprocess.run(cmd, check=True)
result = subprocess.run([str(exe), str(tests / 'sample.jpg'), str(tests / 'sample-no-dht.jpg')], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(ws / '04-v3-20260913/camera-usb-fix/evidence/protocol-test.log').write_text(result.stdout)
print(result.stdout)
raise SystemExit(result.returncode)
