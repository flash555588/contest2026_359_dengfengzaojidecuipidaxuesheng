"""Run the firmware SSE decoder under sanitizers."""
from pathlib import Path
import subprocess, tempfile, os
ws = Path(__file__).resolve().parent.parent
src = ws / '04-v3-20260913/espdl-quickapp/overlay/apps/system/espclaw'
cjson = Path('/tmp/v3-desktop-espdl-20260915/apps/netutils/cjson/cJSON')
with tempfile.TemporaryDirectory(prefix='claw-stream-') as folder:
    binary = str(Path(folder) / 'test')
    subprocess.run(['cc', '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread', '-I'+str(src/'include'),
        '-I'+str(cjson), str(ws/'diagnostics/chat-tests/stream_test.c'), str(src/'port/claw_stream.c'),
        str(cjson/'cJSON.c'), '-lm', '-o', binary], check=True)
    subprocess.run([binary], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1'))
