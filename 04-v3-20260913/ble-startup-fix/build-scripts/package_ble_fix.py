"""Package the BLE release with explicit provenance for successful and failed runs."""
from pathlib import Path
from datetime import datetime, timezone
import difflib
import hashlib
import json
import re
import shutil
import sys
import tarfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
root = Path(__file__).resolve().parent.parent
delivery = root / '04-v3-20260913/ble-startup-fix'
evidence = delivery / 'evidence'
base = root / '04-v3-20260913/ui-layout-fix'
release_sha = '314fc2168eb0873082018edc08a6e1f3fee5d58f18662f4458642273cccd14ee'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))

def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

assert sha(delivery / 'nuttx.bin') == release_sha
validation = read_json(evidence / 'validation.json')
assert validation['firmware_sha256'] == release_sha
assert (delivery / 'resolved.config').read_bytes() == (base / 'resolved.config').read_bytes()
assert 'Hash of data verified.' in (evidence / 'flash4.log').read_text(encoding='utf-8')
assert 'NuttShell (NSH)' in (evidence / 'boot4.log').read_text(encoding='utf-8')

suite_files = {
    'storage4.json': 'ble-storage-suite.json',
    'ble-final.json': 'ble-postflash-suite.json',
    'coexist-final.json': 'ble-coexist-suite.json',
    'repeat-final.json': 'ble-repeat-suite.json',
    'wifi-first-final.json': 'ble-wifi-first-suite.json',
    'recovery-final.json': 'ble-wifi-first-suite.json',
    'wifi-retry-final.json': 'ble-coexist-suite.json',
    'network-observation-final.json': 'ble-network-observation-suite.json',
    'final-console.json': 'ble-final-suite.json',
    'final-console-clean.json': 'ble-final-suite.json',
    'cleanup-final.json': 'ble-cleanup-suite.json',
}
required_pass = {'storage4.json', 'ble-final.json', 'coexist-final.json',
                 'repeat-final.json', 'wifi-first-final.json'}
runs = {}
results = {}
for name, suite in suite_files.items():
    cases = read_json(evidence / name)
    expected = read_json(root / 'diagnostics' / suite)
    assert [c['name'] for c in cases] == ['console'] + [c['name'] for c in expected], name
    assert all(c['completed'] for c in cases), name
    failures = [{'case': c['name'], 'pattern': p} for c in cases
                for p, passed in c['checks'].items() if not passed]
    if name in required_pass:
        assert not failures, (name, failures)
    environment_changed = name in {'recovery-final.json', 'wifi-retry-final.json'}
    runs[name] = {'firmware_sha256': release_sha, 'all_checks_passed': not failures,
                  'classification': 'hotspot unavailable after user relocation' if environment_changed else 'current release observation',
                  'failed_checks': failures, 'suite': 'build-scripts/' + suite,
                  'case_count': len(cases), 'sha256': sha(evidence / name)}
    results[name] = {c['name']: c for c in cases}

final = results['final-console-clean.json']
assert all('nsh:' not in case['output'] for case in results['cleanup-final.json'].values())
assert all(final[n]['completed'] and all(final[n]['checks'].values()) for n in
           ['restore_light', 'show_bluetooth', 'ready_state', 'storage_mounted', 'tasks', 'uptime'])
assert '.storage-check-' not in final['storage_cleanup']['output']
pages = []
for directory in ['review-light', 'review-dark-final']:
    folder = evidence / directory
    metadata = read_json(folder / 'capture-metadata.json')
    assert metadata['firmware_sha256'] == release_sha
    reports = read_json(folder / 'layout-audit.json')
    assert len(reports) == 1
    for report in reports:
        assert not report['overlaps'] and not report['overflow'], report
        image = folder / (report['page'] + '.png')
        assert image.exists()
        log = (folder / (report['page'] + '.gdb.log')).read_text(encoding='utf-8')
        assert 'dumped ' in log and 'detached' in log
        assert 'LIBUSB_ERROR' not in log and 'Error in sourced command file' not in log
        pages.append({'page': report['page'], 'objects': report['objects'],
                      'screenshot': image.relative_to(delivery).as_posix(),
                      'sha256': sha(image), 'overlaps': 0, 'overflow': 0})
assert (evidence / 'final-console-clean.json').stat().st_mtime > max(
    (delivery / page['screenshot']).stat().st_mtime for page in pages)
callout = read_json(evidence / 'callout-test.json')
assert callout['baseline']['returncode'] != 0 and callout['fixed']['returncode'] == 0

address = re.search(r'inet addr:(\S+) DRaddr:(\S+) Mask:(\S+)',
                    results['coexist-final.json']['address']['output'])
assert address and address.group(1) != '0.0.0.0'
last_address = re.search(r'inet addr:(\S+) DRaddr:(\S+) Mask:(\S+)', final['network']['output'])
scan_counts = {name: int(re.search(r'就绪 · (\d+) 个设备', results[name][case]['output']).group(1))
               for name, case in [('ble-final.json', 'scan_layout'),
                                  ('repeat-final.json', 'repeat_scan_finished'),
                                  ('wifi-first-final.json', 'scan_finished')]}
hardware = {
    'firmware_sha256': release_sha, 'flashed_and_verified': True,
    'port': 'COM23', 'device_serial': 'E8:F6:0A:E3:A9:5F', 'chip_revision': '3.2',
    'ble_host_ready': True, 'scan_counts': scan_counts,
    'scan_deadline_ms': 10000, 'deadline_observed_after_wait_seconds': 11,
    'repeat_scan_and_page_close_cancel_passed': True,
    'wifi_first_and_ble_first_passed': True, 'dhcp_success_observed': True,
    'verified_ipv4': address.group(1), 'gateway': address.group(2), 'netmask': address.group(3),
    'last_ipv4': last_address.group(1) if last_address else None,
    'last_interfaces_checks_passed': all(final['network']['checks'].values()),
    'last_wifi_associated': 'eth0' in final['network']['output'] and bool(re.search(r'eth0.*RUNNING', final['network']['output'])),
    'network_environment': 'User confirmed moving to a different workplace; the previously tested hotspot is no longer available. No further connection attempts requested.',
    'storage_selftest': 'PASS: 3 namespaces; overwrite/read/delete',
    'storage_during_scan': 'PASS', 'data_partition_erased': False,
    'framebuffer_visual_review': pages, 'serial_resumed_after_final_jtag': True,
    'final_state_source': 'evidence/final-console-clean.json',
    'observed_issues': [
        'First dark JTAG capture lost USB communication and reported a reset; it is excluded from visual validation. Retry at 2000 kHz succeeded.',
        'Later recovery-final.json and wifi-retry-final.json record STA disassociation and DHCP failure. User confirmed hotspot loss after relocation; these are environment-dependent observations, not firmware regression evidence. Prior coexistence runs passed.'
    ],
    'limits': [
        'ESP32-C6 is BLE-only; no BR/EDR or classic Bluetooth audio.',
        'No designated peripheral: real connection, pairing, GATT and application profiles were not tested.',
        'Scan count depends on nearby advertisements; device list capacity is 12.',
        'Framebuffer review does not measure physical touch accuracy or panel color.',
        'No camera modification or camera hardware validation in this release.'
    ]}
write_json(evidence / 'hardware-validation.json', hardware)

index = {
    'firmware_sha256': release_sha,
    'association_basis': 'These runs followed flash4.log with this image; no later flash or rebuild occurred during capture.',
    'positive_release_evidence': ['flash4.log', 'boot4.log', 'boot-wifi-first.log', 'build.log',
                                  'validation.json', 'image-info.txt', 'callout-test.json'],
    'serial_runs': runs, 'visual_runs': pages,
    'failed_capture': {'directory': 'review-dark', 'firmware_sha256': release_sha,
                       'reason': 'USB/JTAG errors and reset; no valid PNG; not used as passing evidence.'},
    'other_files': 'Other evidence files are earlier experiments or unclassified diagnostics; do not treat them as final-image passing evidence.',
    'ui_historical_correction': '../ui-layout-fix/evidence/provenance-correction.json'}
write_json(evidence / 'evidence-index.json', index)

# Complete cumulative diff relative to the supplied source archive.
targets = {p.relative_to(delivery / 'overlay').as_posix(): p for p in (delivery / 'overlay').rglob('*')
           if p.is_file() and '__pycache__' not in p.parts}
patch = []
archive_path = delivery.parent / 'current-build-tree-source.tar.gz'
with tarfile.open(archive_path) as archive:
    for member in archive:
        name = member.name.removeprefix('./')
        if name in targets:
            before = archive.extractfile(member).read().decode('utf-8').splitlines(True)
            after = targets.pop(name).read_text(encoding='utf-8').splitlines(True)
            patch.extend(difflib.unified_diff(before, after, 'a/' + name, 'b/' + name))
for name, path in sorted(targets.items()):
    patch.extend(difflib.unified_diff([], path.read_text(encoding='utf-8').splitlines(True), '/dev/null', 'b/' + name))
(delivery / 'full-fix.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
write_json(delivery / 'build-metadata.json', {
    'packaged_at': datetime.now(timezone.utc).isoformat(), 'status': 'built-flashed-ble-scanning-verified',
    'firmware': validation, 'config_sha256': sha(delivery / 'resolved.config'),
    'base_firmware_sha256': sha(base / 'nuttx.bin'), 'config_changes': [],
    'source_archive_sha256': sha(archive_path), 'build_tree': '/tmp/v3-desktop-ble-startup-20260914',
    'original_tree_preserved': '/home/streetartist/nuttxspace',
    'compiler': 'Espressif GCC 14.2; xPack RV32IMAC/ILP32 soft-float libgcc',
    'hardware_validation': hardware})
scripts = delivery / 'build-scripts'
scripts.mkdir(exist_ok=True)
for name in ['build_ble_fix.py', 'prepare_ble_fix.py', 'build_linux.py', 'prepare_tls.py',
             'validate_ble_fix.py', 'flash_ble_fix.py', 'package_ble_fix.py', 'test_ble_callout.py',
             'capture_ui.py', 'audit_ui_layout.py', 'capture_serial.py', 'software_probe.py',
             *sorted(set(suite_files.values()))]:
    shutil.copyfile(root / 'diagnostics' / name, scripts / name)
assert (delivery / 'README.md').exists()
paths = sorted(p for p in delivery.rglob('*') if p.is_file() and
               p.name != 'SHA256SUMS' and '__pycache__' not in p.parts)
(delivery / 'SHA256SUMS').write_text(''.join(sha(p) + '  ' + p.relative_to(delivery).as_posix() + '\n'
                                           for p in paths), encoding='utf-8')
print(json.dumps({'files': len(paths), 'firmware_sha256': release_sha,
                  'scan_counts': scan_counts, 'last_ipv4': hardware['last_ipv4'],
                  'last_interfaces_checks_passed': hardware['last_interfaces_checks_passed']}, indent=2))
