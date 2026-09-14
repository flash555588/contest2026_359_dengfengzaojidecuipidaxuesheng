"""Package camera development sources without converting failed tests to passes."""
from pathlib import Path
import difflib
import hashlib
import json
import shutil
import tarfile

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/camera-usb-fix'
evidence = delivery / 'evidence'
base = ws / '04-v3-20260913/ble-device-names'
release_status = 'development; USB preview/photo/reopen verified; real P4 cache writeback fixed; physical video flicker confirmation pending'

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def write(p, value):
    p.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

validation = json.loads((evidence / 'validation.json').read_text())
assert validation['firmware_sha256'] == sha(delivery / 'nuttx.bin')
assert validation['status'] == 'static-checks-passed'
sources = {p.relative_to(delivery / 'overlay').as_posix(): p
           for p in (delivery / 'overlay').rglob('*')
           if p.is_file() and '__pycache__' not in p.parts}
remaining = sources.copy()
patch = []
archive_path = delivery.parent / 'current-build-tree-source.tar.gz'
with tarfile.open(archive_path, 'r|gz') as archive:
    for member in archive:
        name = member.name.removeprefix('./')
        if name in remaining and member.isfile():
            before = archive.extractfile(member).read().decode('utf-8').splitlines(True)
            after = remaining.pop(name).read_text(encoding='utf-8').splitlines(True)
            patch.extend(difflib.unified_diff(before, after, 'a/' + name, 'b/' + name))
for name, p in sorted(remaining.items()):
    patch.extend(difflib.unified_diff([], p.read_text(encoding='utf-8').splitlines(True), '/dev/null', 'b/' + name))
(delivery / 'full-fix.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
script_dir = delivery / 'build-scripts'
script_dir.mkdir(exist_ok=True)
for pattern in ['*camera*.py', 'camera-*-suite.json', 'build_linux.py', 'prepare_tls.py',
                'software_probe.py', 'capture_serial.py', 'capture_ui.py', 'flash_ble_fix.py']:
    for p in (ws / 'diagnostics').glob(pattern):
        shutil.copyfile(p, script_dir / p.name)
shutil.copytree(ws / 'diagnostics/camera-tests', script_dir / 'camera-tests', dirs_exist_ok=True)

runs = {}
for p in evidence.glob('*.json'):
    try:
        cases = json.loads(p.read_text(encoding='utf-8'))
        if not isinstance(cases, list) or not cases or not all(isinstance(c, dict) and 'completed' in c for c in cases):
            continue
        runs[p.name] = {'sha256': sha(p), 'case_count': len(cases),
                        'all_checks_passed': all(c['completed'] and all(c.get('checks', {}).values()) for c in cases),
                        'failed_cases': [c['name'] for c in cases if not c['completed'] or not all(c.get('checks', {}).values())]}
    except (ValueError, KeyError):
        continue
write(evidence / 'evidence-index.json', {
    'current_firmware_sha256': validation['firmware_sha256'],
    'release_status': release_status,
    'serial_runs': runs,
    'revision_hashes': {
        '03': 'afacb2aa7e66743c85f9955e9713bc8c95affcdf5f61d41635b18f1ca30e2b58',
        '04': '8d3d307258be73a7b39289783d03b03547ebf426a8fa907ec24ebdf6bc1408b1',
        '05': '5af2522f927eaa70c2e10f649ddaa2f15c10c9ded5853fa71649856e7c0a4fac',
        '06': '8539f2a5d03d720e2e43159cbc35b0c1188fece241e079494febd7e25ab711c4',
        '07': 'fbae159f56fffe95b9e6f925f6dafc8c22495361a5a52f18deed6a6218f5882a',
        '08': '91e6895b34592009c894d2b2abc9f6a9ca68b923cd29ad280e1922304ef89bc2',
        '09': 'af8ae979fd833406317ea5690b3dbd64874d14a8f8ad61f7ea98fe6d88669de7',
        '10': '61846e688ad5af0a961aed38d0682c1792d6b2f0dbd03d2b38a286b7fb7948f4',
        '11': 'ee8465189b45167e567c9cd606106d9673e3cc9fe75c25226d444e1729e1f2d0'},
    'notes': 'Revision 03 streamed but crashed on stop; 04-08 failed startup. Revision 09 initially timed out, then generation changed to 2 and three full start/stop cycles passed; the reconnection trigger was not recorded. Revision 10 improved controls, but user reported flickering lines in the bottom fifth of the video. Its cached CPU screenshot cannot prove DMA memory coherence. Revision 11 fixes the confirmed no-op up_clean_dcache by calling real P4 C2M writeback; disassembly and preview/photo/reopen tests passed. Physical video flicker confirmation is pending. Hotplug/multiple cameras/CSI remain unverified; physical unplug is not a normal-use prerequisite.'})
write(delivery / 'build-metadata.json', {
    'release_status': release_status,
    'firmware': validation,
    'base_firmware_sha256': sha(base / 'nuttx.bin'),
    'source_archive': '../current-build-tree-source.tar.gz',
    'source_archive_sha256': sha(archive_path),
    'build_root': '/tmp/v3-desktop-camera-usb-20260914',
    'compiler': 'Espressif GCC 14.2 + xPack RV32IMAC/ILP32 soft-float libgcc',
    'user_original_nuttxspace_modified': False,
    'overlay_sha256': {name: sha(p) for name, p in sorted(sources.items())}})
files = sorted(p for p in delivery.rglob('*') if p.is_file() and p.name != 'SHA256SUMS' and '__pycache__' not in p.parts)
(delivery / 'SHA256SUMS').write_text(''.join(sha(p) + '  ' + p.relative_to(delivery).as_posix() + '\n' for p in files), encoding='utf-8')
print('Packaged development firmware', validation['firmware_sha256'], len(sources), 'overlay files;', release_status)
