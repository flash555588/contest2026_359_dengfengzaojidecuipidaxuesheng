"""Integrate the production UI bridge with the real ESPClaw core and backends."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
from prepare_qpk_guide import prepare_qpk_guide
from qpk_test_build import qpk_test_inputs

ws = Path(__file__).resolve().parent.parent
build = Path('/tmp/v3-desktop-espdl-20260915')
core = build / 'apps/system/espclaw'
overlay = ws / '04-v3-20260913/espdl-quickapp/overlay/apps/system'
cjson = build / 'apps/netutils/cjson/cJSON'
tls = build / 'standalone-tls'
hal = build / 'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_common/include'
sources = list((core / 'core').glob('*.c'))
sources = [f for f in sources if f.name not in ['claw_task.c', 'claw_paths.c']]
sources += list((core / 'core/llm/backends').glob('*.c'))
sources += [core / 'core/llm/claw_llm_runtime.c', core / 'core/llm/media/claw_media_pipeline.c']
sources += [core / ('port/' + f) for f in ['queue.c', 'mutex.c', 'task.c', 'esp_err_to_name.c']]
sources += list((core / 'utils').glob('*.c'))
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
prepare_qpk_guide(overlay)
qpk_flags, qpk_sources, qpk_library = qpk_test_inputs(build, overlay)
with tempfile.TemporaryDirectory(prefix='espclaw-core-') as directory:
    binary = Path(directory) / 'test-core'
    command = ['cc', '-D_GNU_SOURCE', '-std=gnu11', '-g', '-O1', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-pthread', '-include', 'stddef.h', *qpk_flags,
               f'-DQPK_BUILDER_ROOT="{directory}/qpk"']
    for include in [overlay / 'espclaw/include', core / 'include', core / 'core',
                    overlay / 'desktop', cjson, hal, tls / 'include', tls / 'tf-psa-crypto/include',
                    tls / 'tf-psa-crypto/drivers/builtin/include', tls / 'tf-psa-crypto/core']:
        command += ['-I' + str(include)]
    command += [str(ws / 'diagnostics/chat-tests/core_test.c'), str(overlay / 'espclaw/port/espclaw_chat.c'),
                str(overlay / 'desktop/glass_chat_store.c'), *map(str, sources),
                *map(str, qpk_sources), str(qpk_library),
                str(tls / 'tf-psa-crypto/drivers/builtin/src/base64.c'), str(cjson / 'cJSON.c'), '-lm', '-o', str(binary)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], env=env, text=True, capture_output=True, timeout=30)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr)
    result.check_returncode()
report = {'asan': True, 'ubsan': True, 'core': 'production ESPClaw',
          'http_peer': 'deterministic local fixture; no external model request', 'output': result.stdout.strip()}
(ws / '04-v3-20260913/espdl-quickapp/evidence/chat-core-validation.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
