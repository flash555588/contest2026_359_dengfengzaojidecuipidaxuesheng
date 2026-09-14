"""Sample live display counters and GPIO outputs without changing firmware."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import time

root = Path(__file__).resolve().parent
firmware = root.parent / '04-v3-20260913/black-screen-fix/nuttx.bin'
assert hashlib.sha256(firmware.read_bytes()).hexdigest() == '61d7d1c8f03a1c01d50b1dfe8c9df9415933a01f7a8e6c752587d76b0659a6fe'
ocd = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
# Addresses verified against the matching ELF and hw_ver3 GPIO register header.
addresses = {'ticks': 0x4ff4f99c, 'frames': 0x4ff4f9d0,
             'underruns': 0x4ff4f9d4, 'gpio_out': 0x500e0004,
             'gpio_enable': 0x500e0020}
samples = []
for index in range(2):
    reads = '; '.join(f'echo "SAMPLE {name} [read_memory 0x{address:x} 32 1]"'
                      for name, address in addresses.items())
    command = ('adapter speed 6000; gdb port disabled; tcl port disabled; telnet port disabled; '
               'init; halt; ' + reads + '; resume; shutdown')
    result = subprocess.run([str(ocd / 'bin/openocd.exe'), '-s',
        str(ocd / 'share/openocd/scripts'), '-f', 'board/esp32p4-builtin.cfg',
        '-c', command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
    (root / f'display-health-{index}.log').write_bytes(result.stdout)
    result.check_returncode()
    text = result.stdout.decode('utf-8', errors='replace')
    memory = {name: int(value, 0) for name, value in
              re.findall(r'SAMPLE (\w+) (0x[0-9a-fA-F]+|\d+)', text)}
    sample = {name: memory[name] for name in addresses}
    sample['monotonic_seconds'] = time.monotonic()
    samples.append(sample)
    print(f'Sample {index}: {sample}', flush=True)
    if index == 0:
        time.sleep(10)

elapsed = samples[1]['monotonic_seconds'] - samples[0]['monotonic_seconds']
frames = (samples[1]['frames'] - samples[0]['frames']) & 0xffffffff
underruns = (samples[1]['underruns'] - samples[0]['underruns']) & 0xffffffff
report = {'samples': samples, 'interval_seconds': elapsed,
          'frames_delta': frames, 'approximate_fps': frames / elapsed,
          'underruns_delta': underruns,
          'backlight_gpio26_output_high': all(bool(s['gpio_out'] & (1 << 26)) for s in samples),
          'backlight_gpio26_output_enabled': all(bool(s['gpio_enable'] & (1 << 26)) for s in samples),
          'reset_gpio27_output_high': all(bool(s['gpio_out'] & (1 << 27)) for s in samples),
          'reset_gpio27_output_enabled': all(bool(s['gpio_enable'] & (1 << 27)) for s in samples)}
(root / 'display-health.json').write_text(json.dumps(report, indent=2) + '\n')
assert frames > 0, 'Display DMA frames have stopped'
assert underruns == 0, 'New display underruns detected'
assert all(report[name] for name in report if name.endswith(('_high', '_enabled')))
print(json.dumps(report, indent=2), flush=True)
