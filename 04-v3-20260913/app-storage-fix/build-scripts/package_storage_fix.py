"""Record actual hardware results and package the reviewed storage fix."""
from pathlib import Path
import hashlib
import json
import re
import shutil

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/app-storage-fix'
evidence = delivery / 'evidence'
validation = json.loads((evidence / 'validation.json').read_text())
digest = hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest()
assert digest == validation['firmware_sha256'] == '2159a0adca9a7e76680233064d907aa3676b6d10da12b1e35dae3901f7b65932'
post = {c['name']: c for c in json.loads((evidence / 'postflash.json').read_text())}
final = {c['name']: c for c in json.loads((evidence / 'final-console.json').read_text())}
assert all(c['completed'] and all(c['checks'].values()) for c in [*post.values(), *final.values()])
flash = (evidence / 'flash.log').read_text(encoding='utf-8', errors='replace')
assert 'Hash of data verified.' in flash and f'Wrote {validation["bytes"]} bytes' in flash
boot = (evidence / 'boot1.log').read_text(encoding='utf-8', errors='replace')
assert 'NuttShell (NSH)' in boot and 'SHA-256 comparison failed' not in boot
assert 'storage selftest PASS (ret=0' in post['storage_selftest']['output']
assert '.storage-check-' not in post['test_directory_cleanup']['output']
assert '.storage-check-' not in final['storage_after_jtag']['output']
assert post['legacy_before']['output'] == post['legacy_after']['output']
assert '26K      4070K' in post['space_before']['output']
assert '26K      4070K' in post['space_after']['output']
assert 'EVENT StaConnected' in post['association_observation']['output']
assert 'DHCP ret=0' in post['dhcp']['output']
address = final['final_address']['output']
assert address.count('Link encap:') == 1 and '10:bd:a3:89:58:74' in address
ipv4, gateway, mask = re.search(r'inet addr:(\S+) DRaddr:(\S+) Mask:(\S+)', address).groups()
running = (evidence / 'running-state.txt').read_text(encoding='utf-8', errors='replace')
assert 'video_running = true' in running and 'OSINIT_IDLELOOP' in running
assert 'dpi_h_res = 1024, dpi_v_res = 600' in running
assert 'PASS:' in (evidence / 'host-tests.log').read_text()
hardware = {
    'date': '2026-09-14', 'status': 'flashed-storage-selftest-and-dhcp-passed',
    'firmware_sha256': digest, 'flashed': True, 'flash_content_verified': True,
    'port': 'COM23', 'device_serial': 'E8:F6:0A:E3:A9:5F', 'chip_revision': '3.2',
    'boot_nsh': True, 'shared_storage_selftest': 'PASS',
    'storage_cases_on_target': ['com.example.hello/launches', 'com.example.game2048/best',
                                '47-byte package / 64-byte key'],
    'storage_operations_on_target': ['create', 'overwrite', 'read and compare', 'delete', 'read missing'],
    'test_directory_removed': True, 'legacy_directory_listing_unchanged': True,
    'data_partition_erased': False, 'data_used_kib_before_and_after': 26,
    'data_available_kib_before_and_after': 4070,
    'legacy_read_compatibility': 'host regression test passed; target existing files preserved',
    'desktop_task_alive': True, 'display_video_running': True,
    'display_geometry': '1024x600 RGB565', 'physical_screen_visual_confirmation': 'not-observed',
    'individual_app_ui_tested': False, 'power_loss_recovery_tested': False,
    'wifi_associated': True, 'dhcp_result': 0, 'ipv4': ipv4, 'gateway': gateway, 'netmask': mask,
    'interface': 'eth0', 'interface_count': 1, 'serial_port_released': True,
    'console_resumed_after_jtag': True
}
(evidence / 'hardware-validation.json').write_text(json.dumps(hardware, indent=2) + '\n')
for name in ['storage-state.log', 'storage-state.json']:
    shutil.copyfile(workspace / 'diagnostics' / name, evidence / ('pre-fix-' + name))
scripts = delivery / 'build-scripts'
scripts.mkdir(exist_ok=True)
for name in ['build_storage_fix.py', 'build_linux.py', 'prepare_tls.py', 'integrate_storage_fix.py',
             'validate_storage_fix.py', 'flash_storage_fix.py', 'package_storage_fix.py',
             'test_qpk_storage.c', 'software_probe.py', 'capture_serial.py', 'inspect_running.py']:
    shutil.copyfile(workspace / 'diagnostics' / name, scripts / name)
for name in ['storage-postflash-suite.json', 'storage-final-suite.json']:
    shutil.copyfile(workspace / 'diagnostics' / name, evidence / name)
metadata = {
    'date': '2026-09-14', 'status': hardware['status'], 'firmware': validation,
    'config_sha256': hashlib.sha256((delivery / 'resolved.config').read_bytes()).hexdigest(),
    'base_firmware_sha256': '559e8f13dc4f1627048c6a5250383762d96262f3b892018382a1a68c60bca953',
    'build_tree': '/tmp/v3-desktop-app-storage-20260914',
    'original_tree_preserved': '/home/streetartist/nuttxspace',
    'compiler': 'Espressif GCC esp-14.2.0_20251107',
    'compiler_runtime': 'xPack GCC 14.2 RV32IMAC/ILP32 libgcc',
    'config_changes_from_wifi_fix': [], 'hardware_validation': hardware,
    'host_validation': 'GCC -Wall -Wextra -Werror, AddressSanitizer and UndefinedBehaviorSanitizer',
    'storage_layout': '.data/@2p<up-to-28-package-chars>/p<remaining-package>/k<30-key-chars>/.../value',
    'storage_name_characters_max': 31, 'storage_parent_directories_max': 7
}
(delivery / 'build-metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
paths = sorted(p for p in delivery.rglob('*') if p.is_file() and p.name != 'SHA256SUMS'
               and '__pycache__' not in p.parts)
(delivery / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() +
    '  ' + p.relative_to(delivery).as_posix() + '\n' for p in paths), encoding='utf-8', newline='\n')
print('Packaged', len(paths), 'files;', hardware['status'], digest)
