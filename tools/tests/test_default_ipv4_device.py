"""Compile the actual netdev_default overlay and exercise dual-interface picks."""
from pathlib import Path
import subprocess
import tempfile

repo=Path(__file__).resolve().parents[2]
source=repo/'app/espdl-quickapp/overlay/nuttx/net/netdev/netdev_default.c'
with tempfile.TemporaryDirectory() as temp:
    root=Path(temp)
    files={
        'nuttx/config.h':'#define CONFIG_NET_IPv4 1\n',
        'utils/utils.h':'',
        'netdev/netdev.h':'',
        'nuttx/net/netdev.h':'''#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#define FAR
#define IFF_IS_RUNNING(f) ((f)&1)
#define NET_LL_LOOPBACK 2
#define NET_LL_CAN 3
struct net_driver_s {struct net_driver_s *flink; unsigned d_flags,d_lltype; in_addr_t d_ipaddr,d_draddr;};
extern struct net_driver_s *g_netdevices;
void netdev_list_lock(void);
void netdev_list_unlock(void);
''',
        'test.c':'''#include <assert.h>
#include <nuttx/net/netdev.h>
struct net_driver_s *g_netdevices;
void netdev_list_lock(void) {}
void netdev_list_unlock(void) {}
struct net_driver_s *netdev_default(void);
int main(void) {
  struct net_driver_s wifi={.d_flags=1,.d_ipaddr=2,.d_draddr=3};
  struct net_driver_s wired={.flink=&wifi,.d_flags=1,.d_draddr=99};
  g_netdevices=&wired;
  /* Inserted cable, DHCP pending, bogus preset gateway: use Wi-Fi. */
  assert(netdev_default()==&wifi);
  wired.d_ipaddr=4;wired.d_draddr=5;
  assert(netdev_default()==&wired); /* Wired lease acquired: wired first. */
  wired.d_flags=0;
  assert(netdev_default()==&wifi); /* Cable/carrier removed. */
  wired.d_flags=1;wired.d_draddr=0;
  assert(netdev_default()==&wifi); /* Wired LAN has no default gateway. */
  wifi.d_draddr=0;
  assert(netdev_default()==&wired); /* No gateways: local-address fallback. */
  wired.d_ipaddr=wifi.d_ipaddr=0;
  assert(netdev_default()==&wired); /* Initial DHCP broadcast still possible. */
  wired.d_lltype=NET_LL_LOOPBACK;
  assert(netdev_default()==&wifi);
  wifi.d_flags=0;
  assert(netdev_default()==NULL);
  return 0;
}
'''
    }
    for name,text in files.items():
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-I'+str(root),str(source),str(root/'test.c'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('PASS actual IPv4 default-device overlay: pending DHCP, wired preference, carrier loss, missing gateway, bootstrap fallback')
