"""Keep every build-local symlink inside the isolated camera tree."""
from pathlib import Path
import os
import re
root = Path('/tmp/v3-desktop-camera-usb-20260914')
for p in root.rglob('*'):
    if not p.is_symlink(): continue
    target = str(p.readlink())
    match = re.match(r'/tmp/v3-desktop-[^/]+/(.*)', target)
    if not match: continue
    destination = root / match.group(1)
    if not destination.resolve().is_relative_to(root):
        # Nested stale links are rewritten in this same traversal.
        destination = Path(os.path.normpath(destination))
    assert Path(os.path.abspath(destination)).is_relative_to(root)
    p.unlink()
    p.symlink_to(os.path.relpath(destination, p.parent))
    print(p.relative_to(root), '->', p.readlink())
