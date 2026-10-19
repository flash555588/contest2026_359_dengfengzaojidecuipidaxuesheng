"""Run the production network lifetime logic with virtual time and DHCP I/O."""
from pathlib import Path
import shutil
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
desktop = repo / 'app/espdl-quickapp/overlay/apps/system/desktop'
c6 = desktop.parent / 'c6probe'


def function(text, name):
    start = text.index('int ' + name + '(void)')
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


with tempfile.TemporaryDirectory(prefix='network-lifetime-') as folder:
    root = Path(folder)
    (root / 'netutils').mkdir()
    (root / 'desktop').mkdir()
    (root / 'c6probe').mkdir()
    (root / 'netutils/netlib.h').write_text('''
#include <stdint.h>
#include <netinet/in.h>
int netlib_getifstatus(const char *, uint8_t *);
int netlib_get_ipv4addr(const char *, struct in_addr *);
int netlib_get_dripv4addr(const char *, struct in_addr *);
int netlib_getmacaddr(const char *, uint8_t *);
int netlib_set_ipv4addr(const char *, const struct in_addr *);
int netlib_set_ipv4netmask(const char *, const struct in_addr *);
int netlib_set_dripv4addr(const char *, const struct in_addr *);
int netlib_clear_dnsaddr(void);
int netlib_set_ipv4dnsaddr(const struct in_addr *);
int netlib_ifup(const char *);
''')
    (root / 'netutils/dhcpc.h').write_text('''
#include <stdint.h>
#include <netinet/in.h>
struct dhcpc_state {
  struct in_addr serverid, ipaddr, netmask, dnsaddr, default_router;
  uint32_t lease_time;
};
void *dhcpc_open(const char *, const void *, int);
int dhcpc_request(void *, struct dhcpc_state *);
void dhcpc_close(void *);
''')
    (root / 'c6net.h').write_text('''
#include <stdbool.h>
#define C6NET_IFNAME "wlan0"
struct c6_link_snapshot;
int c6net_get_link_snapshot(struct c6_link_snapshot *);
int c6net_prepare(void);
int c6net_reconnect(void);
bool c6net_is_initialized(void);
''')
    shutil.copy2(repo / 'tools/c6/link_state.h', root)
    shutil.copy2(desktop / 'glass_netboot.inc', root / 'desktop')
    shutil.copy2(c6 / 'status_rssi.h', root / 'c6probe')
    shutil.copy2(desktop / 'glass_status.h', root)
    source = (c6 / 'c6net.c').read_text()
    (root / 'reconnect.inc').write_text(
        function(source, 'c6net_disconnect') + '\n' +
        function(source, 'c6net_reconnect'))
    binary = root / 'test'
    subprocess.run(['cc', '-std=gnu11', '-D_GNU_SOURCE', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-variable', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-pthread', '-I' + str(root),
                    '-I' + str(root / 'desktop'),
                    str(repo / 'app/espdl-quickapp/tests/network_lifetime_host.c'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
