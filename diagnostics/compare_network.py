"""Read original WSL trees and save network comparison without changing them."""
from pathlib import Path
import difflib
import hashlib
import json
import re
import subprocess

workspace = Path(__file__).resolve().parent.parent
out = workspace / 'diagnostics/network-comparison'
out.mkdir(exist_ok=True)
roots = {'original': Path('/home/streetartist/nuttxspace'),
         'current': Path('/tmp/v3-desktop-black-screen-20260914'),
         'repro_fixed': Path('/home/streetartist/nuttxspace/repro_fixed')}
targets = ['nuttx/.config', 'apps/system/c6probe/c6net.c',
           'apps/system/c6probe/c6net.h', 'apps/system/c6probe/c6probe_main.c',
           'apps/system/c6probe/desktop_backend.c',
           'apps/system/c6probe/esp_hosted.c',
           'apps/system/c6probe/esp_hosted_rpc.c',
           'apps/system/c6probe/Makefile', 'apps/system/c6probe/Kconfig',
           'apps/system/c6probe/link_state.h', 'apps/system/c6probe/rpc_mailbox.h',
           'nuttx/net/netdev/netdev_findbyname.c',
           'nuttx/net/netdev/netdev_register.c',
           'apps/netutils/netlib/netlib_obtainipv4addr.c',
           'apps/netutils/dhcpc/dhcpc.c',
           'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c']
report = {}
for label, root in roots.items():
    report[label] = {'root': str(root), 'files': {}}
    print(label, root, 'exists', root.exists())
    for relative in targets:
        path = root / relative
        if path.is_file():
            data = path.read_bytes()
            dest = out / label / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            report[label]['files'][relative] = hashlib.sha256(data).hexdigest()
    for repo in ['nuttx', 'apps']:
        path = root / repo
        if (path / '.git').exists():
            for args in [('rev-parse', 'HEAD'), ('status', '--short')]:
                p = subprocess.run(['git', '-c', 'core.quotepath=false', *args], cwd=path,
                                   capture_output=True, text=True)
                report[label][repo + ' ' + ' '.join(args)] = p.stdout.strip()
    config = root / 'nuttx/.config'
    if config.exists():
        lines = [s for s in config.read_text().splitlines() if re.search(
            'EMAC|C6PROBE|C6BLE|NETDEV|DHCPC|NSH_NETINIT|NSH_IPADDR|NSH_DRIPADDR|NSH_NETMASK', s)]
        print('\n'.join(lines))
for label in ['original', 'repro_fixed']:
    chunks = []
    for relative in targets:
        before = out / label / relative
        after = out / 'current' / relative
        if before.exists() and after.exists():
            a = before.read_text().splitlines(True)
            b = after.read_text().splitlines(True)
            if a != b:
                print('DIFFERENT', label, relative)
                chunks.extend(difflib.unified_diff(a, b, label+'/'+relative, 'current/'+relative))
            else:
                print('SAME', label, relative)
    (out / (label + '-to-current.diff')).write_text(''.join(chunks))
(out / 'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
print('Saved', out)
