"""Generate an isolated BLE app and transport patch; never alter source trees."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
from prepare_v1_ble_claim import adapt


def prepare(tree, output):
    tree, output = tree.resolve(), output.resolve()
    if output.exists() or tree == output or tree in output.parents:
        raise ValueError('Fresh independent output required')
    if 'CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y' not in (tree / 'nuttx/.config').read_text().splitlines():
        raise ValueError('v1 only')
    source = tree / 'apps/system/c6probe/esp_hosted.c'
    before = source.read_text()
    after = adapt(before)
    files = {}
    for name in ('ble_main.c', 'ble_driver.c', 'ble_driver.h', 'ble_hosted.c',
                 'ble_hosted.h', 'ble_register.c', 'ble_register.h', 'ble_h4.h'):
        files[name] = (Path(__file__).parent / 'c6' / name).read_bytes()
    files['Kconfig'] = b'''config SYSTEM_C6BLE
\tbool "Experimental v1 C6 BLE HCI transport"
\tdefault n
\tdepends on ESP32P4_SELECTS_REV_LESS_V3 && SYSTEM_C6PROBE=y
\tdepends on UART_BTH4 && !DISABLE_PTHREAD
\t---help---
\t\tRegister HCI using c6ble register. Requires the HCI ownership patch.
\t\tHosted must already be prepared before opening the device.
'''
    files['Make.defs'] = b'''ifeq ($(CONFIG_SYSTEM_C6BLE),y)
CONFIGURED_APPS += $(APPDIR)/system/c6ble
endif
'''
    files['Makefile'] = b'''include $(APPDIR)/Make.defs
PROGNAME = c6ble
PRIORITY = 100
STACKSIZE = 8192
MODULE = $(CONFIG_SYSTEM_C6BLE)
CFLAGS += -I$(APPDIR)/system/c6probe
MAINSRC = ble_main.c
CSRCS = ble_driver.c ble_hosted.c ble_register.c
include $(APPDIR)/Application.mk
'''
    output.mkdir(parents=True)
    app = output / 'c6ble'
    app.mkdir()
    for name, data in files.items():
        (app / name).write_bytes(data)
    patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        fromfile='a/system/c6probe/esp_hosted.c', tofile='b/system/c6probe/esp_hosted.c'))
    (output / 'hci-claim.patch').write_text(patch)
    (output / 'manifest.json').write_text(json.dumps({
        'status': 'uninstalled-unlinked-v1-candidate',
        'transport_input': hashlib.sha256(source.read_bytes()).hexdigest(),
        'files': {n: hashlib.sha256(d).hexdigest() for n, d in files.items()},
        'patch_sha256': hashlib.sha256(patch.encode()).hexdigest(),
    }, indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('tree', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    prepare(a.tree, a.output)
