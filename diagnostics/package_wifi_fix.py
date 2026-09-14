"""Collect comparison evidence and checksum the separately built Wi-Fi fix."""
from pathlib import Path
import hashlib
import json
import shutil

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/wifi-dhcp-fix'
evidence = delivery / 'evidence'
diagnostics = workspace / 'diagnostics'
for name in ['comparison.json', 'original-to-current.diff']:
    shutil.copyfile(diagnostics / 'network-comparison' / name, evidence / name)
for name in ['software-inventory.log', 'software-c6.log', 'software-wireless.log']:
    shutil.copyfile(diagnostics / name, evidence / ('pre-fix-' + name))
for name in ['build_linux.py', 'build_wifi_fix.py', 'validate_wifi_fix.py', 'verify_wifi_sources.py',
             'compare_network.py', 'flash_wifi_fix.py', 'record_wifi_hardware.py']:
    target = delivery / 'build-scripts' / name
    target.parent.mkdir(exist_ok=True)
    shutil.copyfile(diagnostics / name, target)
for name in ['wifi-postflash-suite.json', 'wifi-final-suite.json']:
    if (diagnostics / name).exists():
        shutil.copyfile(diagnostics / name, evidence / name)
validation = json.loads((evidence / 'validation.json').read_text())
assert validation['firmware_sha256'] == hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest()
assert validation['status'] == 'build-and-static-checks-passed-hardware-pending'
metadata = {'date': '2026-09-14', 'status': validation['status'],
            'firmware': {key: validation[key] for key in ['firmware_sha256', 'bytes', 'flash_offset',
                         'flash_end_exclusive', 'erase_end_exclusive', 'data_offset']},
            'config_sha256': hashlib.sha256((delivery / 'resolved.config').read_bytes()).hexdigest(),
            'base_firmware_sha256': '61d7d1c8f03a1c01d50b1dfe8c9df9415933a01f7a8e6c752587d76b0659a6fe',
            'original_tree': '/home/streetartist/nuttxspace',
            'build_tree': '/tmp/v3-desktop-wifi-dhcp-20260914',
            'flashed': False, 'hardware_validation': 'pending',
            'compiler': 'Espressif GCC esp-14.2.0_20251107',
            'compiler_runtime': 'xPack GCC 14.2 RV32IMAC/ILP32 libgcc',
            'changed_config_symbols': list(validation['config_changes'])}
hardware_path = evidence / 'hardware-validation.json'
if hardware_path.exists():
    hardware = json.loads(hardware_path.read_text())
    assert hardware['firmware_sha256'] == validation['firmware_sha256']
    metadata['status'] = hardware['status']
    metadata['flashed'] = hardware['flashed']
    metadata['hardware_validation'] = hardware
(delivery / 'build-metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')
files = sorted(p for p in delivery.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
(delivery / 'SHA256SUMS').write_text(''.join(
    hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(delivery).as_posix()+'\n'
    for p in files))
print('Packaged', len(files), 'files;', validation['firmware_sha256'])
