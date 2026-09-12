"""Test generated c6net carrier integration with a disconnect during publish."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from adapt_c6_daemon_retry import adapt as retry
from adapt_c6_link_state import adapt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    text = adapt(retry(args.source.read_text()))
    start = text.index('int c6net_get_link_snapshot(')
    end = text.index('static pthread_mutex_t g_c6net_connect_lock', start)
    code = '''
#include "link_state.h"
#include <assert.h>
struct net_driver_s { int unused; };
static struct c6_link_state g_c6link = C6_LINK_STATE_INITIALIZER;
static bool carrier, inject;
static int locked;
static void netdev_lock(struct net_driver_s *dev) {
 (void)dev; assert(!locked); locked=1;
}
static void netdev_unlock(struct net_driver_s *dev) {
 (void)dev; assert(locked); locked=0;
}
static void netdev_carrier_on(struct net_driver_s *dev) {
 (void)dev; assert(locked); carrier=true;
 if(inject) { inject=false; assert(!c6_link_update(&g_c6link,C6_LINK_DISCONNECTED)); }
}
static void netdev_carrier_off(struct net_driver_s *dev) {
 (void)dev; assert(locked); carrier=false;
}
'''
    code += text[start:end]
    code += '''
int main(void) {
 struct net_driver_s dev;
 struct c6_link_snapshot value;
 assert(!c6_link_update(&g_c6link,C6_LINK_STARTED));
 assert(!c6_link_update(&g_c6link,C6_LINK_IFUP));
 assert(!c6_link_update(&g_c6link,C6_LINK_ASSOCIATED));
 inject=true;
 c6net_sync_carrier(&dev);
 assert(!carrier && !locked);
 assert(!c6net_get_link_snapshot(&value));
 assert(!value.associated && !value.carrier_ready);
 c6net_sync_carrier(&dev);
 assert(!carrier);
 assert(!c6_link_update(&g_c6link,C6_LINK_ASSOCIATED));
 c6net_sync_carrier(&dev);
 assert(carrier);
 assert(!c6net_get_link_snapshot(&value) && value.carrier_ready);
 assert(!c6_link_update(&g_c6link,C6_LINK_IFDOWN));
 c6net_sync_carrier(&dev);
 assert(!carrier);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / 'test.c').write_text(code)
        headers = Path(__file__).resolve().parents[1] / 'c6'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-I', str(headers), str(root / 'test.c'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=10)
    print('PASS: generated carrier integration rejects stale publication and recovers')


if __name__ == '__main__':
    main()
