"""Create a voice-input branch from the verified camera revision 11."""
from pathlib import Path
import os
import re
import shutil
import subprocess

ws = Path(__file__).resolve().parent.parent
old = Path('/tmp/v3-desktop-camera-usb-20260914')
root = Path('/tmp/v3-desktop-voice-20260915')
delivery = ws / '04-v3-20260913/espclaw-voice'
if not root.exists():
    subprocess.run(['cp', '-a', str(old), str(root)], check=True)
    for p in root.rglob('*'):
        if p.is_symlink():
            target = str(p.readlink())
            match = re.match(r'/tmp/v3-desktop-[^/]+/(.*)', target)
            if match:
                destination = root / match.group(1)
                assert Path(os.path.abspath(destination)).is_relative_to(root)
                p.unlink()
                p.symlink_to(os.path.relpath(destination, p.parent))
    for p in root.rglob('Kconfig'):
        if p.is_file() and p.resolve().is_relative_to(root):
            p.write_text(p.read_text().replace(str(old) + '/', str(root) + '/'))
(delivery / 'evidence').mkdir(parents=True, exist_ok=True)
if not (delivery / 'overlay').exists():
    shutil.copytree(ws / '04-v3-20260913/camera-usb-fix/overlay', delivery / 'overlay')
    for name in ['resolved.config']:
        shutil.copyfile(ws / '04-v3-20260913/camera-usb-fix' / name, delivery / name)
    for name in ['apps/system/espclaw/port/espclaw_main.c',
                 'apps/system/espclaw/port/http_webclient.c',
                 'apps/system/espclaw/include/claw_webclient.h',
                 'apps/system/espclaw/Makefile',
                 'nuttx/drivers/audio/es8311.c']:
        target = delivery / 'overlay' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
print('Voice branch ready:', root)
