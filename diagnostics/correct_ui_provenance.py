"""Correct inherited UI evidence attribution without changing firmware or logs."""
from pathlib import Path
import hashlib
import json
import shutil

root = Path(__file__).resolve().parent.parent
delivery = root / '04-v3-20260913/ui-layout-fix'
source = root / '04-v3-20260913/app-storage-fix'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

copied = []
for name in ['running-state.txt', 'final-console.json']:
    original, inherited = source / 'evidence' / name, delivery / 'evidence' / name
    assert original.read_bytes() == inherited.read_bytes(), name
    copied.append({'file': 'evidence/' + name, 'sha256': sha(inherited),
                   'actual_source': original.relative_to(root).as_posix()})
correction = {
    'date': '2026-09-14', 'target_firmware_sha256': sha(delivery / 'nuttx.bin'),
    'actual_source_firmware_sha256': sha(source / 'nuttx.bin'),
    'usable_as_target_final_state': False, 'inherited_files': copied,
    'reason': 'These files were copied from app-storage-fix and incorrectly attributed to ui-layout-fix.',
    'action': 'Preserve historical logs; withdraw unsupported serial-resume claim. Current BLE image has separate live captures.',
    'unaffected_evidence': ['flash.log', 'boot1.log', 'regression.json', 'review-light/', 'review-dark/']}
(delivery / 'evidence/provenance-correction.json').write_text(
    json.dumps(correction, indent=2) + '\n', encoding='utf-8')
for path in [delivery / 'evidence/hardware-validation.json', delivery / 'build-metadata.json']:
    data = json.loads(path.read_text(encoding='utf-8'))
    hardware = data.get('hardware_validation', data)
    hardware['serial_resumed_after_jtag'] = None
    hardware['final_state_provenance'] = 'evidence/provenance-correction.json'
    note = 'Inherited running-state/final-console logs are not final-state evidence for this image.'
    if note not in hardware['limits']:
        hardware['limits'].append(note)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for name in ['package_ui_fix.py', 'correct_ui_provenance.py']:
    shutil.copyfile(root / 'diagnostics' / name, delivery / 'build-scripts' / name)
paths = sorted(p for p in delivery.rglob('*') if p.is_file() and
               p.name != 'SHA256SUMS' and '__pycache__' not in p.parts)
(delivery / 'SHA256SUMS').write_text(''.join(sha(p) + '  ' + p.relative_to(delivery).as_posix() + '\n'
                                           for p in paths), encoding='utf-8')
print('Corrected UI evidence attribution; firmware unchanged:', sha(delivery / 'nuttx.bin'))
