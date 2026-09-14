"""Verify the delivered overlay was compiled inside the camera-only tree."""
from pathlib import Path
import hashlib
import json
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--incremental', action='store_true')
args = parser.parse_args()

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/camera-usb-fix'
build = Path('/tmp/v3-desktop-camera-usb-20260914')
checked = 0
for source in (delivery / 'overlay').rglob('*'):
    if not source.is_file() or '__pycache__' in source.parts:
        continue
    relative = source.relative_to(delivery / 'overlay')
    target = build / relative
    assert target.resolve().is_relative_to(build), str(target)
    assert target.read_bytes() == source.read_bytes(), str(relative)
    checked += 1
for p in build.rglob('*'):
    if p.is_symlink():
        resolved = str(p.resolve())
        assert not resolved.startswith('/tmp/v3-desktop-') or p.resolve().is_relative_to(build), str(p)
assert (delivery / 'resolved.config').read_bytes() == (build / 'nuttx/.config').read_bytes()
log = (delivery / 'evidence/build.log').read_text(errors='replace')
expected = ['qpk_runtime.c'] if args.incremental else [
    'camera_usb_port.c', 'camera_uvc.c', 'camera_uvc_protocol.c',
    'qpk_runtime.c', 'qpk_mjpeg.c', 'qpk_tjpgd.c', 'qpk_camera_probe.c']
for name in expected:
    assert any('CC:' in line and name in line for line in log.splitlines()), name
for a, b in [('nuttx.bin', 'nuttx.bin'), ('nuttx', 'nuttx.elf'), ('nuttx.map', 'nuttx.map')]:
    assert (build / 'nuttx' / a).read_bytes() == (delivery / b).read_bytes(), b
report = {'status': 'passed', 'overlay_files_verified': checked,
          'clean_build_camera_objects_verified': not args.incremental,
          'required_compilations_verified': expected,
          'symlinks_to_other_desktop_builds': False,
          'firmware_sha256': hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest()}
(delivery / 'evidence/build-tree-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
