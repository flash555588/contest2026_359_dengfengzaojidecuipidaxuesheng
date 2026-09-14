from pathlib import Path
root = Path('/tmp/v3-desktop-voice-20260915')
for rel in ['apps/system/espclaw/Makefile', 'apps/system/desktop/Makefile',
            'apps/system/espclaw/Make.dep', 'apps/system/desktop/Make.dep', 'apps/Makefile',
            'apps/Make.defs', 'apps/.config', 'nuttx/Make.defs', 'apps/Application.mk']:
    p = root / rel
    print('\nFILE', rel, 'resolved', p.resolve())
    if p.is_file():
        for line in p.read_text(errors='replace').splitlines():
            if any(x in line for x in ['voice', '/tmp/', 'CSRCS', 'include', 'BUILDIR', '.built', '.depend']): print(line[:250])
for d in ['apps/system/espclaw', 'apps/system/desktop']:
    print('FILES', d)
    for p in (root / d).iterdir():
        if p.name.startswith('.') or p.suffix == '.o': print(p.name, p.stat().st_mtime)
for p in root.rglob('*'):
    if p.is_file() and not p.is_symlink() and (p.name in ['.depend', '.built', 'Make.defs', 'Make.dep', 'Makefile', '.context', '.dirlinks'] or p.suffix == '.bdat'):
        s = p.read_text(errors='replace')
        if '/tmp/v3-desktop-camera-usb-20260914' in s:
            print('STALE',p.relative_to(root))
