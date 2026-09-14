"""Produce a reviewable patch against the untouched source archive."""
from pathlib import Path
import difflib
import tarfile

workspace = Path(__file__).resolve().parent.parent
delivery = workspace / '04-v3-20260913/black-screen-fix'
overlay = delivery / 'overlay'
targets = {p.relative_to(overlay).as_posix(): p for p in overlay.rglob('*')
           if p.is_file() and '__pycache__' not in p.parts}
patch = []
with tarfile.open(delivery.parent / 'current-build-tree-source.tar.gz') as archive:
    for member in archive:
        name = member.name.removeprefix('./')
        if name in targets:
            before = archive.extractfile(member).read().decode('utf-8').splitlines(True)
            after = targets.pop(name).read_text(encoding='utf-8').splitlines(True)
            patch.extend(difflib.unified_diff(before, after, 'a/' + name, 'b/' + name))
for name, target in sorted(targets.items()):
    patch.extend(difflib.unified_diff([], target.read_text(encoding='utf-8').splitlines(True),
                                    '/dev/null', 'b/' + name))
(delivery / 'black-screen.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
print(''.join(patch))
