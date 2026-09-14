"""Collect the verified firmware's evidence and compute delivery checksums."""
from pathlib import Path
import hashlib
import json
import shutil

workspace = Path(__file__).resolve().parent.parent
diagnostics = workspace / 'diagnostics'
delivery = workspace / '04-v3-20260913/black-screen-fix'
evidence = delivery / 'evidence'
evidence.mkdir(exist_ok=True)
for name in ['serial-before-flash.log', 'serial-fixed-v2-boot1.log',
             'serial-fixed-v2-boot2.log', 'console-status.log',
             'running-state.txt', 'desktop-framebuffer.png',
             'gdma-calls.txt', 'flash-fixed-v2.log', 'build-latest.log']:
    shutil.copyfile(diagnostics / name, evidence / name)
followup = ['console-soak-start.log', 'console-soak-end.log',
            'display-health-0.log', 'display-health-1.log', 'display-health.json',
            'runtime-monitor.log', 'runtime-monitor.json', 'console-reset-reason.log',
            'no-console-startup.txt', 'no-console-startup.openocd.log']
for name in followup:
    if (diagnostics / name).exists():
        shutil.copyfile(diagnostics / name, evidence / name)
for path in diagnostics.glob('software-*'):
    if path.is_file() and path.suffix in {'.log', '.json', '.txt'}:
        shutil.copyfile(path, evidence / path.name)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

assert sha(delivery / 'nuttx.bin') == '61d7d1c8f03a1c01d50b1dfe8c9df9415933a01f7a8e6c752587d76b0659a6fe'
assert sha(delivery / 'resolved.config') == 'fff61b0a78e055078c44d9fb61714bfb0eaead2e1d9d5ddc6dc56e3ff9d59b42'
for log in ['serial-fixed-v2-boot1.log', 'serial-fixed-v2-boot2.log']:
    text = (diagnostics / log).read_text(encoding='utf-8', errors='replace')
    assert 'NuttShell (NSH)' in text
    assert 'SHA-256 comparison failed' not in text
    assert 'no mem for pair' not in text and 'Not enough memory' not in text
metadata = {
    'date': '2026-09-14',
    'status': 'flashed-verified-desktop-framebuffer-confirmed',
    'physical_screen_visual_confirmation': 'pending',
    'software_validation_report': 'software-test-report.md',
    'software_validation_status': 'partial-with-open-issues',
    'board': {'chip': 'ESP32-P4', 'revision': '3.2', 'rom': 'esp32p4-eco7-20260109',
              'port': 'COM23', 'usb_serial': 'E8:F6:0A:E3:A9:5F',
              'flash_mb': 16, 'psram_mb': 32, 'psram_mhz': 200},
    'source': '04-v3-20260913/current-build-tree-source.tar.gz',
    'compiler': 'Espressif GCC esp-14.2.0_20251107',
    'compiler_runtime': 'xPack GNU RISC-V Embedded GCC 14.2.0 libgcc.a, RV32IMAC/ILP32',
    'esptool_version': '5.3.1',
    'tls_inputs': json.loads((diagnostics / 'downloads/tls-manifest.json').read_text()),
    'flash_offset': '0x2000',
    'bytes': (delivery / 'nuttx.bin').stat().st_size,
    'flash_end_exclusive': '0x3c9070',
    'erase_end_exclusive': '0x3ca000',
    'data_offset': '0x400000',
    'data_margin_bytes': 0x400000 - 0x3c9070,
    'original_backup': '../../diagnostics/original-nuttx.bin',
    'original_sha256': sha(diagnostics / 'original-nuttx.bin'),
    'sha256': {name: sha(delivery / name) for name in
               ['nuttx.bin', 'nuttx.elf', 'nuttx.map', 'resolved.config', 'black-screen.patch']},
    'validation': {'successful_usb_resets': 2, 'mirror_framebuffer': '1024x600 RGB565',
                   'kernel_free_bytes': 317496, 'user_free_bytes': 29760512,
                   'image_regression_tests': 4},
}
if (diagnostics / 'display-health.json').exists():
    health = json.loads((diagnostics / 'display-health.json').read_text())
    metadata['validation']['display_output_fps'] = health['approximate_fps']
    metadata['validation']['display_underruns_delta'] = health['underruns_delta']
    metadata['validation']['backlight_gpio26_output_high'] = health['backlight_gpio26_output_high']
if (diagnostics / 'runtime-monitor.json').exists():
    runtime = json.loads((diagnostics / 'runtime-monitor.json').read_text())
    metadata['validation']['continuous_serial_observation_seconds'] = runtime['samples'][-1]['elapsed_seconds']
    metadata['validation']['serial_observation_reset_banner'] = runtime['reset_banner_observed']
    metadata['validation']['serial_observation_kernel_used_delta'] = runtime['kernel_used_delta']
if (diagnostics / 'no-console-startup.txt').exists():
    standalone = (diagnostics / 'no-console-startup.txt').read_text(encoding='utf-8', errors='replace')
    metadata['validation']['startup_without_console_activation'] = all(
        value in standalone for value in ['host_active = false', 'video_running = true',
                                         'OSINIT_IDLELOOP', '"desktop"'])
(delivery / 'build-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
files = sorted(p for p in delivery.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
(delivery / 'SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.relative_to(delivery).as_posix()}\n' for p in files), encoding='utf-8')
print(f'Packaged {len(files)} files; SHA256 {metadata["sha256"]["nuttx.bin"]}')
