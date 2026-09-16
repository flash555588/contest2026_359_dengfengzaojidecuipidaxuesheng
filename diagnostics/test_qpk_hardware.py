"""Exercise real QuickJS and the production hardware scheduler with mocked I/O."""
from pathlib import Path
import hashlib, json, os, subprocess, tempfile
from qpk_test_build import qpk_test_inputs
ws = Path(__file__).resolve().parent.parent
overlay = ws / '04-v3-20260913/espdl-quickapp/overlay/apps/system'
build = Path('/tmp/v3-desktop-espdl-20260915')
flags, _, library = qpk_test_inputs(build, overlay)
cjson = build / 'apps/netutils/cjson/cJSON'
with tempfile.TemporaryDirectory(prefix='qpk-hardware-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread', *flags,
                    '-I'+str(overlay/'desktop'), '-I'+str(cjson),
                    str(ws/'diagnostics/qpk-tests/hardware_test.c'),
                    str(overlay/'desktop/qpk_hardware.c'), str(cjson/'cJSON.c'), str(library),
                    '-lm', '-o', str(binary)], check=True)
    result = subprocess.run([str(binary), str(overlay/'desktop/recorder/app.js')], check=True, text=True, capture_output=True,
                            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'), timeout=30)
    print(result.stdout, end='')
    report = dict(asan=True, ubsan=True, quickjs='production firmware sources', physical_io='mocked',
                  result=result.stdout.strip(), sources={name:hashlib.sha256((overlay/'desktop'/name).read_bytes()).hexdigest()
                    for name in ('qpk_hardware.h', 'qpk_hardware.c', 'qpk_hardware_js.inc', 'recorder/app.js')})
    (ws/'04-v3-20260913/espdl-quickapp/evidence/qpk-hardware-host.json').write_text(json.dumps(report, indent=2)+'\n')
