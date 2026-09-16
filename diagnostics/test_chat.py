"""Run the production ESPClaw conversation worker/store under ASan and UBSan."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ws = Path(__file__).resolve().parent.parent
src = ws / '04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
cjson = Path('/tmp/v3-desktop-espdl-20260915/apps/netutils/cjson/cJSON')
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
results = []
with tempfile.TemporaryDirectory(prefix='espclaw-chat-') as directory:
    root = Path(directory)
    binary = root / 'test-chat'
    command = ['cc', '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-pthread',
               '-I' + str(src), '-I' + str(src.parent / 'espclaw/include'), '-I' + str(cjson), f'-DCHAT_DATA_ROOT="{root}/config"',
               str(ws / 'diagnostics/chat-tests/service_test.c'), str(src / 'glass_chat_service.c'),
               str(src / 'glass_chat_store.c'), str(cjson / 'cJSON.c'), '-lm', '-o', str(binary)]
    subprocess.run(command, check=True)
    for mode in ['run', 'reload', 'interrupt', 'recover']:
        result = subprocess.run([str(binary), mode], env=env, text=True, capture_output=True, timeout=30)
        print(result.stdout, end='')
        if result.stderr:
            print(result.stderr)
        result.check_returncode()
        results.append({'case': mode, 'passed': True, 'output': result.stdout.strip()})
    recovery = root / 'test-recovery'
    subprocess.run(['cc', '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-I' + str(src), '-I' + str(src.parent / 'espclaw/include'), '-I' + str(cjson), f'-DCHAT_DATA_ROOT="{root}/recovery"',
        str(ws / 'diagnostics/chat-tests/store_recovery_test.c'), str(src / 'glass_chat_store.c'),
        str(cjson / 'cJSON.c'), '-lm', '-o', str(recovery)], check=True)
    result = subprocess.run([str(recovery)], env=env, text=True, capture_output=True, timeout=10)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr)
    result.check_returncode()
    results.append({'case': 'torn_save_recovery', 'passed': True, 'output': result.stdout.strip()})
report = {'asan': True, 'ubsan': True, 'backend': 'controllable host fixture; no external AI requests', 'tests': results}
(ws / '04-v3-20260913/espdl-quickapp/evidence/chat-host-validation.json').write_text(
    json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
