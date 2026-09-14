"""Validate storage image boundaries, prior fixes, config and linked code."""
from pathlib import Path
import difflib
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
import tarfile
sys.dont_write_bytecode = True

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/ui-layout-fix'
base = workspace / '04-v3-20260913/app-storage-fix'
evidence = delivery / 'evidence'
assert (delivery / 'resolved.config').read_bytes() == (base / 'resolved.config').read_bytes()
for path in (base / 'overlay').rglob('*'):
    if path.is_file() and '__pycache__' not in path.parts and path.name not in ['desktop_main.c', 'qpk_runtime.c']:
        assert path.read_bytes() == (delivery / 'overlay' / path.relative_to(base / 'overlay')).read_bytes(), path
# UI edits must preserve the previously verified storage JS bindings.
import re
old_runtime = (base / 'overlay/apps/system/desktop/qpk_runtime.c').read_text(encoding='utf-8')
new_runtime = (delivery / 'overlay/apps/system/desktop/qpk_runtime.c').read_text(encoding='utf-8')
for function in ['qpk_storage_key', 'js_storage_get', 'js_storage_set', 'js_storage_delete']:
    pattern = r'(?ms)^static [^\n]*\b' + function + r'\(.*?(?=^static |\Z)'
    before = re.search(pattern, old_runtime)
    after = re.search(pattern, new_runtime)
    assert before and after and before.group() == after.group(), function
spec = importlib.util.spec_from_file_location('digest', delivery / 'overlay/nuttx/tools/espressif/simple_boot_digest.py')
digest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(digest)
image = (delivery / 'nuttx.bin').read_bytes()
assert image == digest.add_digest(image)
assert image[0] == 0xe9 and image[23] == 1
assert struct.unpack_from('<H', image, 12)[0] == 18
offset = 24
for _ in range(image[1]):
    _, length = struct.unpack_from('<II', image, offset)
    offset += 8 + length
offset = (offset + 16) & ~15
assert image[offset:offset+32] == hashlib.sha256(image[:offset]).digest()
offset += 32
segments = []
while offset < len(image):
    address, length = struct.unpack_from('<II', image, offset)
    assert offset + 8 + length <= len(image)
    if address:
        segments.append({'offset': hex(offset), 'address': hex(address), 'bytes': length})
    offset += 8 + length
assert offset == len(image) and len(segments) >= 2
erase_end = 0x2000 + ((len(image) + 4095) & ~4095)
assert erase_end <= 0x400000
nm = Path('D:/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin/riscv32-esp-elf-nm.exe')
result = subprocess.run([str(nm), '--defined-only', str(delivery / 'nuttx.elf')],
                        capture_output=True, text=True, check=True)
symbols = {line.split()[-1] for line in result.stdout.splitlines() if line.split()}
needed = ['g_ui_probe_frame', 'g_ui_probe_frame_bytes', 'qpk_storage_read', 'qpk_storage_write', 'qpk_storage_remove', 'qpk_storage_selftest',
          'qpk_runtime_launch', 'c6net_initialize', 'dhcpc_request', 'heap_caps_free', 'esp_timer_impl_early_init']
assert all(name in symbols for name in needed)
assert not any('emac' in name.lower() for name in symbols)
(evidence / 'storage-symbols.txt').write_text('\n'.join(line for line in result.stdout.splitlines()
    if any(key in line for key in ['qpk_storage', 'js_storage', 'c6net_initialize', 'dhcpc_request'])) + '\n')
result = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32p4', 'image-info',
                         str(delivery / 'nuttx.bin')], capture_output=True, check=True)
(evidence / 'image-info.txt').write_bytes(result.stdout)
report = {'status': 'static-checks-passed', 'firmware_sha256': hashlib.sha256(image).hexdigest(),
          'bytes': len(image), 'flash_offset': '0x2000', 'flash_end_exclusive': hex(0x2000 + len(image)),
          'erase_end_exclusive': hex(erase_end), 'data_offset': '0x400000',
          'simpleboot_digest_valid': True, 'mapped_segments': segments,
          'config_identical_to_storage_fix': True, 'prior_functional_fixes_preserved': True,
          'required_symbols_present': needed}
(evidence / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')

targets = {p.relative_to(delivery / 'overlay').as_posix(): p for p in (delivery / 'overlay').rglob('*')
           if p.is_file() and '__pycache__' not in p.parts}
patch = []
with tarfile.open(delivery.parent / 'current-build-tree-source.tar.gz') as archive:
    for member in archive:
        name = member.name.removeprefix('./')
        if name in targets:
            before = archive.extractfile(member).read().decode('utf-8').splitlines(True)
            after = targets.pop(name).read_text(encoding='utf-8').splitlines(True)
            patch.extend(difflib.unified_diff(before, after, 'a/' + name, 'b/' + name))
for name, path in sorted(targets.items()):
    patch.extend(difflib.unified_diff([], path.read_text(encoding='utf-8').splitlines(True), '/dev/null', 'b/' + name))
(delivery / 'full-fix.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
patch = []
for path in sorted((delivery / 'overlay/apps/system/desktop').iterdir()):
    name = 'apps/system/desktop/' + path.name
    previous = workspace / 'diagnostics/ui-baseline' / path.name
    before = previous.read_text(encoding='utf-8').splitlines(True) if previous.exists() else []
    patch.extend(difflib.unified_diff(before, path.read_text(encoding='utf-8').splitlines(True),
                                    'a/' + name if previous.exists() else '/dev/null', 'b/' + name))
(delivery / 'ui-layout.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
print(json.dumps(report, indent=2))
