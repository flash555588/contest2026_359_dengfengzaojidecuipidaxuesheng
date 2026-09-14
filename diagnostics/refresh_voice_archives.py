"""Drop copied build outputs whose archive member names embed the old path."""
from pathlib import Path
root = Path('/tmp/v3-desktop-voice-20260915').resolve()
assert root.name == 'v3-desktop-voice-20260915'
count = 0
for p in root.rglob('*'):
    if p.is_symlink() or not p.is_file(): continue
    if p.name in ['Make.dep', '.depend', '.built'] or p == root / 'apps/libapps.a':
        assert p.resolve().is_relative_to(root)
        p.unlink()
        count += 1
print('Removed', count, 'generated dependency/archive files in the voice branch')
