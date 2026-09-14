"""Check patch applicability, preserved original files, and GDMA relocations."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tarfile
import tempfile

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/wifi-dhcp-fix'
overlay = delivery / 'overlay'
targets = {p.relative_to(overlay).as_posix(): p for p in overlay.rglob('*')
           if p.is_file() and '__pycache__' not in p.parts}
with tempfile.TemporaryDirectory(prefix='v3-wifi-patch-check-') as tmp:
    root = Path(tmp)
    with tarfile.open(workspace / '04-v3-20260913/current-build-tree-source.tar.gz') as archive:
        for member in archive:
            name = member.name.removeprefix('./')
            if name in targets:
                dest = root / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(archive.extractfile(member).read())
    result = subprocess.run(['patch', '--batch', '-p1', '-i', str(delivery / 'full-fix.patch')],
                            cwd=root, capture_output=True, text=True)
    (delivery / 'evidence/patch-apply.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
    for name, expected in targets.items():
        assert (root / name).read_bytes() == expected.read_bytes(), name

original = json.loads((workspace / 'diagnostics/network-comparison/comparison.json').read_text())['original']
for name, digest in original['files'].items():
    assert hashlib.sha256((Path(original['root']) / name).read_bytes()).hexdigest() == digest, name

build = Path('/tmp/v3-desktop-wifi-dhcp-20260914/nuttx')
gdma = list((build / 'arch/risc-v').rglob('gdma.o'))
assert len(gdma) == 1, gdma
objdump = '/home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin/riscv32-esp-elf-objdump'
result = subprocess.run([objdump, '-dr', str(gdma[0])], capture_output=True, text=True, check=True)
lines = [line for line in result.stdout.splitlines()
         if 'R_RISCV_CALL' in line and re.search(r'\b(?:heap_caps_free|free)\b', line)]
assert len(lines) == 8 and all(re.search(r'\bheap_caps_free\b', line) for line in lines), lines
(delivery / 'evidence/gdma-relocations.txt').write_text('\n'.join(lines)+'\n')

report = {'full_patch_matches_overlay': True, 'verified_overlay_files': len(targets),
          'original_compared_files_unchanged': len(original['files']),
          'gdma_heap_caps_free_calls': len(lines), 'gdma_plain_free_calls': 0}
(delivery / 'evidence/source-validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
