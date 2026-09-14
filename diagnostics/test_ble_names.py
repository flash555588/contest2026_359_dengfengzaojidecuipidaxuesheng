"""Compile and run the production name parser under ASan and UBSan in WSL."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
delivery = root / '04-v3-20260913/ble-device-names'
include = delivery / 'overlay/apps/wireless/bluetooth/nimble'
with tempfile.TemporaryDirectory(prefix='ble-names-test-') as tmp:
    executable = Path(tmp) / 'test'
    command = ['cc', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
               '-I', str(include), str(root / 'diagnostics/test_ble_names.c'), '-o', str(executable)]
    subprocess.run(command, check=True)
    run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
    report = {'returncode': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr,
              'sanitizers': ['address', 'undefined'],
              'source_sha256': hashlib.sha256((include / 'glass_ble_name.h').read_bytes()).hexdigest()}
    (delivery / 'evidence/name-parser-test.json').write_text(json.dumps(report, indent=2) + '\n')
    print(run.stdout, run.stderr)
    run.check_returncode()
