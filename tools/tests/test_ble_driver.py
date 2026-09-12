"""Run the real bt_driver adapter against a transport double, without radio IO."""
from pathlib import Path
import subprocess
import tempfile

TOOLS = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        header = root / 'nuttx/wireless/bluetooth/bt_driver.h'
        header.parent.mkdir(parents=True)
        header.write_text('''
#ifndef TEST_BT_DRIVER_H
#define TEST_BT_DRIVER_H
#include <stddef.h>
#include <stdint.h>
enum bt_buf_type_e {BT_CMD, BT_EVT, BT_ACL_OUT, BT_ACL_IN, BT_ISO_OUT, BT_ISO_IN};
struct bt_driver_s {
 size_t head_reserve;
 int (*open)(struct bt_driver_s *);
 int (*send)(struct bt_driver_s *, enum bt_buf_type_e, void *, size_t);
 void (*close)(struct bt_driver_s *);
 int (*receive)(struct bt_driver_s *, enum bt_buf_type_e, void *, size_t);
 int (*ioctl)(struct bt_driver_s *, int, unsigned long);
 void *priv;
 void *bt_net;
};
#define bt_netdev_receive(dev,type,data,len) (dev)->receive(dev,type,data,len)
#endif
''')
        test = root / 'test.c'
        test.write_text('''
#include "ble_driver.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
static struct {
 bool up;
 int starts, stops, sends, receives, start_error, send_error;
 c6_ble_rx_t rx;
 void *arg;
} transport;
static int start(void *context, c6_ble_rx_t rx, void *arg) {
 assert(context == &transport); transport.starts++;
 if (transport.start_error) return transport.start_error;
 transport.rx=rx; transport.arg=arg; transport.up=true; return 0;
}
static void stop(void *context) {
 assert(context == &transport); transport.stops++;
 transport.up=false; transport.rx=NULL; transport.arg=NULL;
}
static int send_h4(void *context, const uint8_t *data, size_t length) {
 const uint8_t reset[] = {1, 3, 12, 0};
 assert(context == &transport);
 if (!transport.up) return -ENOTCONN;
 assert(length == sizeof(reset) && !memcmp(data, reset, length));
 transport.sends++; return transport.send_error;
}
static int receive_hci(struct bt_driver_s *dev, enum bt_buf_type_e type,
                       void *data, size_t length) {
 const uint8_t complete[] = {14, 4, 1, 3, 12, 0};
 assert(dev && type == BT_EVT && length == sizeof(complete));
 assert(!memcmp(data, complete, length)); transport.receives++; return 0;
}
int main(void) {
 struct c6_ble_transport ops={&transport,start,stop,send_h4};
 uint8_t reset[]={3,12,0}, complete[]={14,4,1,3,12,0};
 assert(c6_ble_driver_create(NULL)==NULL);
 struct bt_driver_s *dev=c6_ble_driver_create(&ops);
  assert(dev && dev->head_reserve==0);
  /* Actual uart_bth4_register overwrites this field. */
  dev->priv=&transport;
 dev->receive=receive_hci;
 assert(dev->send(dev,BT_CMD,reset,sizeof(reset))==-ENOTCONN);
 transport.start_error=-EIO;
 assert(dev->open(dev)==-EIO);
 dev->close(dev); assert(transport.stops==0);
 transport.start_error=0;
 assert(dev->open(dev)==0 && dev->open(dev)==0 && transport.starts==2);
 assert(dev->send(dev,BT_CMD,reset,sizeof(reset))==sizeof(reset));
 assert(transport.sends==1);
 assert(dev->send(dev,BT_EVT,reset,sizeof(reset))==-EPROTONOSUPPORT);
 assert(dev->send(dev,BT_CMD,reset,2)==-EMSGSIZE);
 transport.send_error=-EAGAIN;
 assert(dev->send(dev,BT_CMD,reset,sizeof(reset))==-EAGAIN);
 transport.send_error=1;
 assert(dev->send(dev,BT_CMD,reset,sizeof(reset))==-EIO);
 assert(transport.rx(transport.arg,4,complete,sizeof(complete))==0);
 assert(transport.receives==1);
 assert(transport.rx(transport.arg,4,complete,2)==-EMSGSIZE);
 assert(transport.receives==1);
 dev->close(dev); dev->close(dev);
 assert(transport.stops==1 && !transport.rx);
 assert(dev->send(dev,BT_CMD,reset,sizeof(reset))==-ENOTCONN);
 assert(dev->open(dev)==0);
 c6_ble_driver_destroy(dev);
 assert(transport.stops==2);
 c6_ble_driver_destroy(NULL);
 return 0;
}
''')
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-pthread', '-fsanitize=address,undefined',
                        '-I', str(root), '-I', str(TOOLS / 'c6'),
                        str(TOOLS / 'c6/ble_driver.c'), str(test),
                        '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=15)
    print('PASS: real adapter lifecycle, H4 TX/RX, error propagation and reopen')


if __name__ == '__main__':
    main()
