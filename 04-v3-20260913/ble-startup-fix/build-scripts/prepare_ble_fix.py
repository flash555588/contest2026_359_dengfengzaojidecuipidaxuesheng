"""Create a cumulative BLE fix over the delivered UI firmware sources."""
from pathlib import Path
import shutil
import difflib

root = Path(__file__).resolve().parent.parent
base = root / '04-v3-20260913/ui-layout-fix'
out = root / '04-v3-20260913/ble-startup-fix'
out.mkdir(exist_ok=True)
(out / 'evidence').mkdir(exist_ok=True)
shutil.copytree(base / 'overlay', out / 'overlay', dirs_exist_ok=True)
shutil.copyfile(base / 'resolved.config', out / 'resolved.config')
relative = Path('nuttx/net/bluetooth/bluetooth_conn.c')
before = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
after = before.replace('      /* Mark as unbound */', '''      /* Initialize the socket lock before publishing the connection. */

      conn_init(&conn->bc_conn);

      /* Mark as unbound */''', 1)
after = after.replace('  /* Free the connection structure */', '''  /* Release resources initialized by conn_init before returning the slot. */

  conn_uninit(&conn->bc_conn);

  /* Free the connection structure */''', 1)
assert after.count('conn_init(&conn->bc_conn);') == 1
assert after.count('conn_uninit(&conn->bc_conn);') == 1
dest = out / 'overlay' / relative
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(after, encoding='utf-8', newline='\n')
(out / 'bluetooth-startup.patch').write_text(''.join(difflib.unified_diff(
    before.splitlines(True), after.splitlines(True),
    fromfile='a/' + relative.as_posix(), tofile='b/' + relative.as_posix())), encoding='utf-8')
print('Prepared BLE connection lifecycle fix')

# HCI controller numbers count Bluetooth radios, not Ethernet interfaces.
relative = Path('nuttx/net/bluetooth/bluetooth_finddev.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
text = text.replace('#include <nuttx/net/net.h>', '#include <netpacket/bluetooth.h>\n#include <nuttx/net/net.h>')
text = text.replace('  FAR const bt_addr_t       *bf_addr;', '  FAR const bt_addr_t       *bf_addr;\n  int                       bf_hci;')
text = text.replace('  /* First, check if this network device', '''  /* Raw HCI uses a controller number, independent of Ethernet ifindex. */

  if (match->bf_hci >= 0)
    {
      if (dev->d_lltype == NET_LL_BLUETOOTH && match->bf_hci-- == 0)
        {
          match->bf_radio = (FAR struct radio_driver_s *)dev;
          return 1;
        }

      return 0;
    }

  /* First, check if this network device''', 1)
text = text.replace('  match.bf_addr  = addr;', '  match.bf_hci   = conn->bc_proto == BTPROTO_HCI ? conn->bc_ldev : -1;\n  match.bf_addr  = addr;')
(out / 'overlay' / relative).write_text(text, encoding='utf-8', newline='\n')

relative = Path('nuttx/net/bluetooth/bluetooth_sendmsg.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
start = text.index('  if (psock->s_proto == BTPROTO_L2CAP)', text.index('static ssize_t bluetooth_sendto('))
end = text.index('  /* Perform the send operation */', start)
text = text[:start] + '''  if (psock->s_proto != BTPROTO_L2CAP && psock->s_proto != BTPROTO_HCI)
    {
      return -EOPNOTSUPP;
    }

  radio = bluetooth_find_device(conn, &conn->bc_laddr);
  if (radio == NULL)
    {
      return -ENODEV;
    }

''' + text[end:]
(out / 'overlay' / relative).write_text(text, encoding='utf-8', newline='\n')

# Use the same device lock as socket send/receive while invoking callbacks.
relative = Path('nuttx/wireless/bluetooth/bt_netdev.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
text = text.replace('#include <nuttx/config.h>', '#include <nuttx/config.h>\n#include <nuttx/mutex.h>')
start = text.index('static void btnet_hci_received(FAR struct bt_buf_s *buf, FAR void *context)\n{')
end = text.index('\n#endif', text.index('drop:', start))
part = text[start:end].replace('  net_lock();', '  priv = (FAR struct btnet_driver_s *)context;\n  netdev_lock(&priv->bd_dev.r_dev);', 1)
part = part.replace('  priv = (FAR struct btnet_driver_s *)context;\n  if', '  if', 1)
part = part.replace('  net_unlock();', '  netdev_unlock(&priv->bd_dev.r_dev);')
text = text[:start] + part + text[end:]
start = text.index('static void btnet_txavail_work(FAR void *arg)\n{')
end = text.index('\n/****************************************************************************', start)
part = text[start:end].replace('  net_lock();', '  netdev_lock(&priv->bd_dev.r_dev);').replace('  net_unlock();', '  netdev_unlock(&priv->bd_dev.r_dev);')
text = text[:start] + part + text[end:]
dest = out / 'overlay' / relative
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(text, encoding='utf-8', newline='\n')

# Registration leaves the raw network device down. Bring it up before NimBLE.
relative = Path('apps/system/c6ble/ble_socket_register.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
text = text.replace('#include "c6net.h"', '#include "c6net.h"\n#include "netutils/netlib.h"')
text = text.replace('registers hci0', 'registers bnep0')
text = text.replace('      pthread_mutex_unlock(&g_registration_lock);\n      return 0;', '      goto ifup;')
text = text.replace('out:\n', '''ifup:
  if (g_registered && netlib_ifup("bnep0") < 0) ret = -errno;
out:
''', 1)
dest = out / 'overlay' / relative
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(text, encoding='utf-8', newline='\n')

patch = []
for name in ['nuttx/net/bluetooth/bluetooth_conn.c', 'nuttx/net/bluetooth/bluetooth_finddev.c',
             'nuttx/net/bluetooth/bluetooth_sendmsg.c', 'nuttx/wireless/bluetooth/bt_netdev.c',
             'apps/system/c6ble/ble_socket_register.c']:
    before = (root / 'diagnostics/ble-baseline' / name).read_text(encoding='utf-8')
    after = (out / 'overlay' / name).read_text(encoding='utf-8')
    patch.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True), 'a/' + name, 'b/' + name))
(out / 'bluetooth-startup.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
print('Prepared HCI device selection, interface startup and callback locks')

relative = Path('apps/system/c6probe/esp_hosted.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
text = text.replace('  real_framelen = framelen;\n', '', 1)
text = text.replace('  /* Wait until the slave has buffers for a frame of this size. */', '''  /* H4's type byte lives in the Hosted header, so exclude it from the
   * SDIO window length as well as from the payload length.
   */

  real_framelen = framelen;

  /* Wait until the slave has buffers for a frame of this size. */''', 1)
dest = out / 'overlay' / relative
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(text, encoding='utf-8', newline='\n')
before = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
with (out / 'bluetooth-startup.patch').open('a', encoding='utf-8', newline='\n') as stream:
    stream.write(''.join(difflib.unified_diff(before.splitlines(True), text.splitlines(True), 'a/' + relative.as_posix(), 'b/' + relative.as_posix())))
print('Prepared HCI SDIO window length fix')

relative = Path('apps/system/c6ble/ble_hosted.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
text = text.replace('  /* Callback/arg remain immutable from claim until release completes. */\n  (void)priv->receive(priv->arg, type, data, length);', '''  (void)type;
  /* Controller-to-host packets carry H4 inline. Only host-to-controller
   * traffic moves the H4 type into the ESP-Hosted header.
   */
  if (!data || length < 1) return;
  /* Callback/arg remain immutable from claim until release completes. */
  (void)priv->receive(priv->arg, data[0], data + 1, length - 1);''')
(out / 'overlay' / relative).write_text(text, encoding='utf-8', newline='\n')
before = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
with (out / 'bluetooth-startup.patch').open('a', encoding='utf-8', newline='\n') as stream:
    stream.write(''.join(difflib.unified_diff(before.splitlines(True), text.splitlines(True), 'a/' + relative.as_posix(), 'b/' + relative.as_posix())))
print('Prepared controller-to-host H4 parsing fix')

relative = Path('nuttx/net/procfs/net_procfs.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
text = text.replace('copy = strdup(relpath);', 'copy = nx_strdup(relpath);')
text = text.replace('lib_free(copy);', 'kmm_free(copy);')
dest = out / 'overlay' / relative
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(text, encoding='utf-8', newline='\n')
before = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
with (out / 'bluetooth-startup.patch').open('a', encoding='utf-8', newline='\n') as stream:
    stream.write(''.join(difflib.unified_diff(before.splitlines(True), text.splitlines(True), 'a/' + relative.as_posix(), 'b/' + relative.as_posix())))
print('Prepared network procfs kernel heap pairing fix')

relative = Path('apps/wireless/bluetooth/nimble/mynewt-nimble/porting/npl/nuttx/src/os_callout.c')
text = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
text = text.replace('''    /* TODO: seek native posix method to determine whether timer_t is active.
       TODO: fix bug where one-shot timer is still active after fired. */

    return c->c_active;''', '''    struct itimerspec its;

    /* A one-shot timer becomes inactive at expiry. A stale c_active flag
     * prevents the host from scheduling later GAP scan/connect deadlines.
     */
    if (!c->c_timer || timer_gettime(c->c_timer, &its) != 0) {
        return false;
    }
    return its.it_value.tv_sec != 0 || its.it_value.tv_nsec != 0;''')
dest = out / 'overlay' / relative
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(text, encoding='utf-8', newline='\n')
before = (root / 'diagnostics/ble-baseline' / relative).read_text(encoding='utf-8')
with (out / 'bluetooth-startup.patch').open('a', encoding='utf-8', newline='\n') as stream:
    stream.write(''.join(difflib.unified_diff(before.splitlines(True), text.splitlines(True), 'a/' + relative.as_posix(), 'b/' + relative.as_posix())))
print('Prepared one-shot timer active-state fix')
