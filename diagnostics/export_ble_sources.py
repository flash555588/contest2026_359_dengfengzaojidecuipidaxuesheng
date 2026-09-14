"""Read BLE host/transport sources from the current-build source archive."""
from pathlib import Path
import tarfile
root = Path(__file__).resolve().parent.parent
out = root / 'diagnostics/ble-baseline'
out.mkdir(exist_ok=True)
with tarfile.open(root / '04-v3-20260913/current-build-tree-source.tar.gz') as archive:
    for member in archive:
        name = member.name.removeprefix('./')
        if not member.isfile():
            continue
        take = (name.startswith('apps/system/c6ble/') or
                name in ['apps/system/c6probe/c6net.c', 'apps/system/c6probe/c6net.h',
                         'apps/wireless/bluetooth/nimble/glass_ble.c',
                         'apps/wireless/bluetooth/nimble/Makefile',
                         'apps/wireless/bluetooth/nimble/Kconfig'] or
                'nimble' in name and any(piece in name for piece in
                    ['hci_socket', '/nuttx/', 'nimble_port.c', 'ble_hs_startup.c', 'ble_hs_hci.c']) or
                name.startswith('nuttx/wireless/bluetooth/') or
                name.startswith('nuttx/net/bluetooth/'))
        if take and member.size < 400000 and Path(name).suffix in ['.c','.h','']:
            dest = out / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(archive.extractfile(member).read())
            print(name)
