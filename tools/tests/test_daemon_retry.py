"""Run actual adapted initialization against task-create failure injection."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from adapt_c6_daemon_retry import adapt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    text = adapt(args.source.read_text())
    start = text.index('int c6net_initialize(')
    end = text.index('bool c6net_is_initialized(', start)
    code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#define FAR
#define CONFIG_NET_ETH_PKTSIZE 1500
#define NET_LL_ETHERNET 1
#define C6NET_TASK_PRIORITY 100
#define C6NET_TASK_STACK 4096
struct net_driver_s {
 struct {struct {uint8_t ether_addr_octet[6];} ether;} d_mac;
 char d_ifname[16];
 int d_llhdrlen, d_pktsize;
 void *d_buf, *d_private;
 int (*d_ifup)(struct net_driver_s *), (*d_ifdown)(struct net_driver_s *);
 int (*d_txavail)(struct net_driver_s *);
};
struct c6net_state_s {
 struct net_driver_s dev;
 bool registered, initialized, ifup, associated, carrier_ready, event_pending;
 int daemon_pid;
 uint8_t buf[64];
};
static struct c6net_state_s g_c6net;
static pthread_mutex_t g_c6net_connect_lock=PTHREAD_MUTEX_INITIALIZER;
static int registrations, tasks, connects, starts;
static int c6net_ifup(struct net_driver_s *d) {(void)d;return 0;}
static int c6net_ifdown(struct net_driver_s *d) {(void)d;return 0;}
static int c6net_txavail(struct net_driver_s *d) {(void)d;return 0;}
#define c6net_wifi_event 0
#define c6net_rx 0
#define c6net_daemon 0
#define ESP_HOSTED_IF_STA 1
#define esp_hosted_initialize(v) (starts++,0)
#define esp_hosted_rpc_wifi_init() 0
#define esp_hosted_rpc_wifi_set_mode(m) 0
#define esp_hosted_rpc_wifi_start() 0
#define esp_hosted_rpc_get_mac(i,m) (memset(m,1,6),0)
#define esp_hosted_rpc_set_wifi_event_cb(c,a) 0
#define esp_hosted_register(i,c,a) 0
#define netdev_register(d,t) (registrations++,0)
static int task_create(const char *n,int p,int s,int f,void *a) {
 (void)n;(void)p;(void)s;(void)f;(void)a;
 tasks++; if(tasks==1){errno=ENOMEM;return -1;}return 42;
}
static int esp_hosted_rpc_wifi_connect(const char *s,const char *p) {
 (void)s;(void)p;connects++;return 0;
}
'''
    code += text[start:end]
    code += r'''
int main(void) {
 assert(c6net_connect("test","")==-ENOMEM);
 assert(registrations==1 && starts==1 && tasks==1 && connects==0);
 assert(g_c6net.registered && !g_c6net.initialized && !g_c6net.ifup);
 assert(c6net_connect("test","")==0);
 assert(registrations==1 && starts==1 && tasks==2 && connects==1);
 assert(g_c6net.initialized && g_c6net.ifup);
 assert(c6net_connect("test","")==0);
 assert(registrations==1 && tasks==2 && connects==2);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root / 'test.c').write_text(code)
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread',
                        str(root / 'test.c'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=10)
    print('PASS: task-create failure, retry without duplicate registration, reconnect')


if __name__ == '__main__':
    main()
