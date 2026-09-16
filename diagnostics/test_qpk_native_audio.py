"""Fault-inject selected production HAL/I2S functions on the host.

These checks cover software ownership and critical-section contracts. They do
not emulate DMA, cache coherency, the codec, or physical audio signals.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import tempfile

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
overlay = delivery / 'overlay/nuttx'
sources = {
    'os': overlay / 'arch/risc-v/src/esp32p4/esp-hal-3rdparty/nuttx/src/platform/os.c',
    'i2s': overlay / 'arch/risc-v/src/common/espressif/esp_i2s.c',
}


def function(source, name):
    # Select a definition rather than the forward declaration.
    match = re.search(r'\b(?:static\s+)?(?:struct\s+\w+\s*\*\s*|(?:int|void)\s+)'
                      + name + r'\([^;{]*\)\s*\{', source)
    if not match:
        raise ValueError(name)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


with tempfile.TemporaryDirectory(prefix='qpk-native-audio-') as directory:
    work = Path(directory)
    port = sources['os'].read_text()
    # The two signatures are selected by the firmware's OS_SPINLOCK setting.
    port = re.sub(r'#if OS_SPINLOCK == 1\s*'
                  r'(void nuttx_(?:enter|exit)_critical\(FAR rspinlock_t \*lock\))'
                  r'\s*#else\s*void nuttx_(?:enter|exit)_critical\(void\)\s*#endif',
                  r'\1', port)
    i2s = sources['i2s'].read_text()
    names = ('i2s_buf_free', 'i2s_buf_allocate', 'i2s_buf_initialize',
             'i2s_send', 'i2s_receive')
    extracted = '\n\n'.join(function(port, n) for n in
                            ('nuttx_enter_critical', 'nuttx_exit_critical'))
    extracted += '\n\n' + '\n\n'.join(function(i2s, n) for n in names)
    (work / 'native_audio.inc').write_text(extracted)
    binary = work / 'test'
    subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                    '-Wno-unused-parameter', '-Wno-unused-variable',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(work),
                    str(ws / 'diagnostics/qpk-tests/native_audio_test.c'),
                    '-o', str(binary)], check=True)
    run = subprocess.run([str(binary)], check=True, capture_output=True, text=True,
                         env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                                  UBSAN_OPTIONS='halt_on_error=1'), timeout=20)
    print(run.stdout, end='')
    report = {'asan': True, 'ubsan': True, 'physical_io': 'not exercised',
              'result': run.stdout.strip(),
              'sources': {name: hashlib.sha256(path.read_bytes()).hexdigest()
                          for name, path in sources.items()}}
    (delivery / 'evidence/qpk-native-audio-host.json').write_text(
        json.dumps(report, indent=2) + '\n')
