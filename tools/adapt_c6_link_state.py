"""Wire synchronized state into c6net after daemon retry adaptation."""
import argparse
import difflib
from pathlib import Path
from adapt_c6_rx_dispatch import once


def section(text, start, end, replacement):
    a = text.index(start)
    b = text.index(end, a)
    return text[:a] + replacement + '\n\n' + text[b:]


def adapt(text):
    if 'start_daemon:' not in text:
        raise ValueError('Apply daemon retry adaptation first')
    text = once(text, '#include "c6net.h"', '#include "c6net.h"\n#include "link_state.h"')
    for field in ('volatile bool initialized', 'bool ifup', 'volatile bool associated',
                  'volatile bool carrier_ready', 'volatile bool event_pending'):
        text = once(text, '  ' + field + ';\n', '')
    text = once(text, 'static struct c6net_state_s g_c6net;', '''static struct c6net_state_s g_c6net;
/* Singleton companion radio; keep mutex outside the resettable netdev. */
static struct c6_link_state g_c6link = C6_LINK_STATE_INITIALIZER;

int c6net_get_link_snapshot(struct c6_link_snapshot *out)
{
  return c6_link_read(&g_c6link, out);
}

static void c6net_sync_carrier(struct net_driver_s *dev)
{
  struct c6_link_snapshot value;
  netdev_lock(dev);
  if (c6_link_read(&g_c6link, &value) == 0)
    {
      bool ready = value.initialized && value.ifup && value.associated;
      if (ready) netdev_carrier_on(dev);
      else netdev_carrier_off(dev);
      if (c6_link_publish_carrier(&g_c6link, value.generation, ready) < 0)
        netdev_carrier_off(dev);
    }
  else netdev_carrier_off(dev);
  netdev_unlock(dev);
}''')
    a = text.index('  while (priv->initialized)')
    b = text.index('      while (esp_hosted_poll()', a)
    text = text[:a] + '''  while (c6net_is_initialized())
    {
      c6net_sync_carrier(&priv->dev);

''' + text[b:]
    text = section(text, 'static int c6net_ifup(', 'static int c6net_txavail(', '''static int c6net_ifup(FAR struct net_driver_s *dev)
{
  int ret = c6_link_update(&g_c6link, C6_LINK_IFUP);
  if (ret < 0) return ret;
  dev->d_flags |= IFF_UP | IFF_RUNNING;
  return 0;
}

static int c6net_ifdown(FAR struct net_driver_s *dev)
{
  int ret = c6_link_update(&g_c6link, C6_LINK_IFDOWN);
  dev->d_flags &= ~(IFF_UP | IFF_RUNNING);
  netdev_carrier_off(dev);
  return ret;
}''')
    text = section(text, 'static void c6net_wifi_event(', 'static void c6net_rx(', '''static void c6net_wifi_event(FAR void *arg, bool connected)
{
  UNUSED(arg);
  (void)c6_link_update(&g_c6link, connected ? C6_LINK_ASSOCIATED :
                                           C6_LINK_DISCONNECTED);
}''')
    text = once(text, '  if (!priv->ifup || len < 14 || len > sizeof(priv->buf))',
                '''  struct c6_link_snapshot value;
  if (c6_link_read(&g_c6link, &value) < 0 || !value.ifup ||
      len < 14 || len > sizeof(priv->buf))''')
    text = once(text, '  if (priv->initialized)', '  if (c6net_is_initialized())')
    text = once(text, '''      priv->associated = false;
      priv->carrier_ready = false;
      priv->event_pending = true;''', '''      ret = c6_link_update(&g_c6link, C6_LINK_DISCONNECTED);
      if (ret < 0) goto out;''')
    text = once(text, '''  priv->initialized = true;
  priv->ifup = true;
  priv->associated = false;
  c6net_ifup(&priv->dev);''', '''  ret = c6_link_update(&g_c6link, C6_LINK_STARTED);
  if (ret < 0) goto out;
  netdev_lock(&priv->dev);
  ret = c6net_ifup(&priv->dev);
  netdev_unlock(&priv->dev);
  if (ret < 0)
    {
      (void)c6_link_update(&g_c6link, C6_LINK_STOPPED);
      goto out;
    }''')
    text = once(text, '''      priv->initialized = false;
      priv->ifup = false;
      c6net_ifdown(&priv->dev);''', '''      (void)c6_link_update(&g_c6link, C6_LINK_STOPPED);
      netdev_lock(&priv->dev);
      c6net_ifdown(&priv->dev);
      netdev_unlock(&priv->dev);''')
    text = once(text, '  return g_c6net.initialized;', '''  struct c6_link_snapshot value;
  return c6_link_read(&g_c6link, &value) == 0 && value.initialized;''')
    text = once(text, '  return g_c6net.associated && g_c6net.carrier_ready;', '''  struct c6_link_snapshot value;
  return c6_link_read(&g_c6link, &value) == 0 && value.initialized &&
         value.ifup && value.associated && value.carrier_ready;''')
    for field in ('initialized', 'associated', 'ifup', 'carrier_ready', 'event_pending'):
        if 'priv->' + field in text or 'g_c6net.' + field in text:
            raise ValueError('Legacy state access remains: ' + field)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    folder = args.apps / 'system/c6probe'
    path = folder / 'c6net.c'
    original = path.read_text()
    updated = adapt(original)
    if args.apply:
        if (folder / 'link_state.h').exists():
            raise SystemExit('Existing link header; not overwritten')
        (folder / 'link_state.h').write_bytes((Path(__file__).parent / 'c6/link_state.h').read_bytes())
        path.write_text(updated)
    else:
        print(''.join(difflib.unified_diff(original.splitlines(True), updated.splitlines(True))))


if __name__ == '__main__':
    main()
