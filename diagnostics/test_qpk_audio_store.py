"""Verify real PCM WAV metadata, package isolation and durable listing."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
desktop = delivery / 'overlay/apps/system/desktop'
cjson = Path('/tmp/v3-desktop-espdl-20260915/apps/netutils/cjson/cJSON')
with tempfile.TemporaryDirectory(prefix='qpk-audio-store-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(desktop), '-I' + str(cjson),
                    str(ws / 'diagnostics/qpk-tests/audio_store_test.c'),
                    str(desktop / 'qpk_audio_store.c'), str(desktop / 'qpk_storage.c'),
                    str(cjson / 'cJSON.c'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary), directory + '/media'], check=True,
                            capture_output=True, text=True, timeout=20,
                            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1'))
    print(result.stdout, end='')
    (delivery / 'evidence/qpk-audio-store-host.json').write_text(json.dumps({
        'asan': True, 'ubsan': True, 'result': result.stdout.strip(),
        'sources': {name: hashlib.sha256((desktop / name).read_bytes()).hexdigest()
                    for name in ('qpk_audio_store.c', 'qpk_audio_store.h', 'qpk_storage.c')}
    }, indent=2) + '\n')
