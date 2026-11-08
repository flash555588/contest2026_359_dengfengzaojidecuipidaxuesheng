"""Exercise actual STA/AP carrier reconciliation during transport failure."""
from pathlib import Path
import re
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
radio = repo / 'app/espdl-quickapp/overlay/apps/system/c6probe'


def function(path, name):
    text = path.read_text()
    start = re.search(r'^(?:static )?(?:void|bool) ' + name + r'\(', text, re.M).start()
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


code = r'''
#include <assert.h>
#include "link_state.h"
#define FAR
#define IFF_UP 1
#define IFF_RUNNING 2
struct net_driver_s { unsigned d_flags; };
struct c6_softap_private_s {
  struct { bool registered; struct net_driver_s dev; } net;
};
static struct c6_link_state g_c6link = C6_LINK_STATE_INITIALIZER;
static struct c6_softap_private_s ap;
static struct c6_softap_private_s *g_c6ap = &ap;
static bool g_c6ap_requested = true, inject_disconnect;
static int link_error, locked;
static int esp_hosted_link_error(void) { return link_error; }
static void netdev_lock(struct net_driver_s *dev) {
  (void)dev; assert(!locked); locked=1;
}
static void netdev_unlock(struct net_driver_s *dev) {
  (void)dev; assert(locked); locked=0;
}
static void netdev_carrier_on(struct net_driver_s *dev) {
  assert(locked); dev->d_flags |= IFF_RUNNING;
  if(inject_disconnect) {
    inject_disconnect=false;
    assert(!c6_link_update(&g_c6link,C6_LINK_DISCONNECTED));
  }
}
static void netdev_carrier_off(struct net_driver_s *dev) {
  assert(locked); dev->d_flags &= ~IFF_RUNNING;
}
'''
code += function(radio / 'c6net.c', 'c6net_sync_carrier')
code += function(radio / 'c6net.c', 'c6net_is_associated')
code += function(radio / 'c6_softap_net.inc', 'c6_softap_sync_carrier')
code += r'''
int main(void) {
  struct net_driver_s sta={.d_flags=IFF_UP};
  ap.net.registered=true; ap.net.dev.d_flags=IFF_UP;
  assert(!c6_link_update(&g_c6link,C6_LINK_STARTED));
  assert(!c6_link_update(&g_c6link,C6_LINK_IFUP));
  assert(!c6_link_update(&g_c6link,C6_LINK_ASSOCIATED));
  const int errors[]={0,-ETIMEDOUT,-EIO,0,-ENETRESET};
  for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
    link_error=errors[i];
    c6net_sync_carrier(&sta); c6_softap_sync_carrier();
    assert(!locked);
    assert(!!(sta.d_flags&IFF_RUNNING)==!link_error);
    assert(!!(ap.net.dev.d_flags&IFF_RUNNING)==!link_error);
    assert(c6net_is_associated()==!link_error);
  }
  link_error=0; inject_disconnect=true;
  c6net_sync_carrier(&sta);
  assert(!(sta.d_flags&IFF_RUNNING) && !c6net_is_associated());
  g_c6ap_requested=false; c6_softap_sync_carrier();
  assert(!(ap.net.dev.d_flags&IFF_RUNNING));
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='c6-carrier-health-') as temp:
    output = Path(temp)
    (output / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-pthread',
                    '-I' + str(repo / 'tools/c6'), str(output / 'test.c'),
                    '-o', str(output / 'test')], check=True)
    subprocess.run([str(output / 'test')], check=True, timeout=10)
print('PASS: real STA/AP carrier follows health, transient recovery and stale association rejection')
