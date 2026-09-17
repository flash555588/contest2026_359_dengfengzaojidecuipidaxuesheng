"""Explicitly integrate composed desktop networking into an existing build tree."""
import argparse
import json
import shutil
from pathlib import Path
from prepare_c6_desktop import prepare, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    parser.add_argument('desktop', type=Path)
    parser.add_argument('backup', type=Path)
    args = parser.parse_args()
    tree = args.workspace.resolve()
    backup = args.backup.resolve()
    if backup.exists():
        raise SystemExit('Fresh backup directory required')
    radio = tree / 'apps/system/c6probe'
    desktop = tree / 'apps/system/desktop'
    generated = prepare(radio, backup / 'composed')
    config = (radio / 'Kconfig').read_text()
    if 'config SYSTEM_C6_DESKTOP' in config:
        raise SystemExit('Already integrated; refusing repeat')
    config += '''
config SYSTEM_C6_DESKTOP
\tbool "Desktop C6 scan and connection worker"
\tdefault n
\tdepends on SYSTEM_C6PROBE=y && SYSTEM_DESKTOP=y && NET
\tdepends on !DISABLE_PTHREAD
\t---help---
\t\tEnable experimental desktop scan/association requests. No DHCP.

config SYSTEM_C6_DESKTOP_STACKSIZE
\tint "Desktop C6 worker stack size"
\tdefault 8192
\trange 8192 65536
\tdepends on SYSTEM_C6_DESKTOP
'''
    makefile = (radio / 'Makefile').read_text()
    anchor = 'include $(APPDIR)/Application.mk'
    if makefile.count(anchor) != 1:
        raise SystemExit('Unexpected radio Makefile')
    makefile = makefile.replace(anchor, '''ifeq ($(CONFIG_SYSTEM_C6_DESKTOP),y)
CSRCS += desktop_worker.c desktop_backend.c
endif

''' + anchor)
    desktop_make = (desktop / 'Makefile').read_text()
    desktop_anchor = 'include $(APPDIR)/Make.defs'
    if desktop_make.count(desktop_anchor) != 1:
        raise SystemExit('Unexpected desktop Makefile')
    desktop_make = desktop_make.replace(desktop_anchor, desktop_anchor + '''
ifeq ($(CONFIG_SYSTEM_C6_DESKTOP),y)
CFLAGS += -I$(APPDIR)/system/c6probe
endif
''')
    updates = {radio / 'Kconfig': config.encode(), radio / 'Makefile': makefile.encode(),
               desktop / 'Makefile': desktop_make.encode()}
    for name in generated['outputs']:
        updates[radio / name] = (backup / 'composed' / name).read_bytes()
    for name in ('glass_ui.inc', 'glass_files.inc', 'glass_wifi.inc'):
        updates[desktop / name] = (args.desktop / name).read_bytes()
    originals = {}
    for path in updates:
        relative = path.relative_to(tree)
        originals[str(relative)] = path.exists()
        if path.exists():
            saved = backup / 'originals' / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, saved)
    shutil.copy2(tree / 'nuttx/.config', backup / 'resolved-before.config')
    for path, data in updates.items():
        path.write_bytes(data)
    (backup / 'integration.json').write_text(json.dumps({
        'workspace': str(tree), 'originals': originals,
        'outputs': {str(p.relative_to(tree)): digest(d) for p, d in updates.items()}
    }, indent=2) + '\n')
    print('Integrated sources; configuration resolution and build still required')


if __name__ == '__main__':
    main()
