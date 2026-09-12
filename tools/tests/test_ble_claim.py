"""Exercise the generated HCI claim/release implementation without radio IO."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_v1_ble_claim import adapt


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    args = p.parse_args()
    generated = adapt(args.source.read_text())
    begin = generated.index('static pthread_mutex_t g_rx_dispatch_lock')
    end = generated.index('/****************************************************************************',
                          generated.index('int esp_hosted_register('))
    code = '''
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#define FAR
#define OK 0
#define ESP_HOSTED_IF_HCI 4
#define ESP_HOSTED_IF_MAX 8
typedef void (*esp_hosted_rx_cb_t)(void);
static struct {esp_hosted_rx_cb_t rx_cb[8]; void *rx_arg[8];} g_hosted;
''' + generated[begin:end] + '''
static void callback(void) {}
int main(void) {
 int owner, other;
#ifdef CONFIG_ESP32P4_SELECTS_REV_LESS_V3
 assert(esp_hosted_hci_claim(NULL, &owner)==-EINVAL);
 assert(esp_hosted_register(4,callback,&other)==0);
 assert(esp_hosted_hci_claim(callback,&owner)==-EBUSY);
 assert(esp_hosted_register(4,NULL,NULL)==0);
 assert(esp_hosted_hci_claim(callback,&owner)==0);
 assert(esp_hosted_hci_claim(callback,&owner)==-EBUSY);
 assert(esp_hosted_register(4,NULL,NULL)==-EBUSY);
 assert(esp_hosted_register(3,callback,&other)==0);
 assert(esp_hosted_hci_release(callback,&other)==-EPERM);
 assert(g_hosted.rx_arg[4]==&owner);
 assert(esp_hosted_hci_release(callback,&owner)==0);
 assert(g_hosted.rx_cb[4]==NULL && g_hosted.rx_arg[4]==NULL);
 assert(esp_hosted_hci_release(callback,&owner)==-EPERM);
 assert(esp_hosted_hci_claim(callback,&other)==0);
 assert(esp_hosted_hci_release(callback,&other)==0);
#else
 assert(esp_hosted_register(4,callback,&owner)==0);
 assert(esp_hosted_register(4,callback,&other)==0);
 assert(g_hosted.rx_arg[4]==&other);
#endif
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / 'test.c').write_text(code)
        for flags in ([], ['-DCONFIG_ESP32P4_SELECTS_REV_LESS_V3=1']):
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread', *flags,
                            str(root / 'test.c'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True, timeout=10)
    print('PASS: v1 HCI ownership and unchanged non-v1 registration behavior')


if __name__ == '__main__':
    main()
