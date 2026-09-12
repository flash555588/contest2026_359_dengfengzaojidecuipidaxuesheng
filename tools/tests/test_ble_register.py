"""Test registration ownership and allocation failure cleanup without hardware."""
from pathlib import Path
import subprocess
import tempfile

TOOLS = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for name, text in {
            'nuttx/config.h': '#define CONFIG_ESP32P4_SELECTS_REV_LESS_V3 1\n#define CONFIG_UART_BTH4 1\n',
            'nuttx/wireless/bluetooth/bt_driver.h': '#include <stddef.h>\n#include <stdint.h>\nstruct bt_driver_s;\n',
            'nuttx/serial/uart_bth4.h': 'struct bt_driver_s;\nint uart_bth4_register(const char *, struct bt_driver_s *);\n',
        }.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        (root / 'test.c').write_text('''
#include "ble_hosted.h"
#include "ble_register.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
struct bt_driver_s {int dummy;};
static struct bt_driver_s driver;
static struct c6_ble_transport transport;
static int fail, freed_driver, freed_transport, registered;
struct c6_ble_transport *c6_ble_hosted_create(void) {
 if(fail==1){errno=ENOMEM;return NULL;} return &transport;
}
void c6_ble_hosted_destroy(struct c6_ble_transport *p) {assert(p==&transport);freed_transport++;}
struct bt_driver_s *c6_ble_driver_create(const struct c6_ble_transport *p) {
 assert(p==&transport);if(fail==2){errno=ENOMEM;return NULL;}return &driver;
}
void c6_ble_driver_destroy(struct bt_driver_s *p) {assert(p==&driver);freed_driver++;}
int uart_bth4_register(const char *path,struct bt_driver_s *p) {
 assert(!strcmp(path,"/dev/ttyHCI0") && p==&driver);registered++;
 return fail==3 ? -EEXIST : 0;
}
int main(void) {
 assert(c6_ble_register(NULL)==-EINVAL);
 assert(c6_ble_register("/dev/ttyHCI")==-EINVAL);
 assert(c6_ble_register("/dev/ttyHCI0/other")==-EINVAL);
 fail=1;assert(c6_ble_register("/dev/ttyHCI0")==-ENOMEM);
 assert(!freed_driver && !freed_transport);
 fail=2;assert(c6_ble_register("/dev/ttyHCI0")==-ENOMEM);
 assert(!freed_driver && freed_transport==1);
 fail=3;assert(c6_ble_register("/dev/ttyHCI0")==-EEXIST);
 assert(freed_driver==1 && freed_transport==2);
 fail=0;assert(!c6_ble_register("/dev/ttyHCI0"));
 assert(registered==2 && freed_driver==1 && freed_transport==2);
 return 0;
}
''')
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(root), '-I', str(TOOLS / 'c6'),
                        str(TOOLS / 'c6/ble_register.c'), str(root / 'test.c'),
                        '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=10)
    print('PASS: registration validation, allocation failures and ownership transfer')


if __name__ == '__main__':
    main()
