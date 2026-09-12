"""Serialize Wi-Fi callback lifetime. Preview by default; no radio operations."""
import argparse
import difflib
from pathlib import Path

from adapt_c6_rx_dispatch import once


def adapt(text):
    if '#include <pthread.h>' not in text:
        text = once(text, '#include <errno.h>',
                    '#include <errno.h>\n#include <pthread.h>')
    text = once(text, 'static FAR void *g_wifi_event_arg;', '''static FAR void *g_wifi_event_arg;
static pthread_mutex_t g_wifi_event_lock = PTHREAD_MUTEX_INITIALIZER;

/* RX owns dispatch before this lock. Callbacks only publish state: they must
 * not call synchronous RPCs, register handlers, or acquire connection locks.
 * Unregister waits for in-flight callbacks before the caller frees its arg.
 */
static void rpc_wifi_event_notify(bool connected)
{
  if (pthread_mutex_lock(&g_wifi_event_lock) != 0) return;
  if (g_wifi_event_cb != NULL)
    g_wifi_event_cb(g_wifi_event_arg, connected);
  pthread_mutex_unlock(&g_wifi_event_lock);
}''')
    for value in ('true', 'false'):
        text = once(text, f'''              if (g_wifi_event_cb != NULL)
                {{
                  g_wifi_event_cb(g_wifi_event_arg, {value});
                }}''', f'              rpc_wifi_event_notify({value});')
    return once(text, '''  g_wifi_event_cb = cb;
  g_wifi_event_arg = arg;
  return 0;''', '''  int ret = pthread_mutex_lock(&g_wifi_event_lock);
  if (ret != 0) return -ret;
  g_wifi_event_cb = cb;
  g_wifi_event_arg = cb != NULL ? arg : NULL;
  pthread_mutex_unlock(&g_wifi_event_lock);
  return 0;''')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    path = args.apps / 'system/c6probe/esp_hosted_rpc.c'
    original = path.read_text()
    if 'g_wifi_event_lock' in original:
        raise SystemExit('Event lock already present; inspect rather than overwrite')
    updated = adapt(original)
    if args.apply:
        path.write_text(updated)
    else:
        print(''.join(difflib.unified_diff(original.splitlines(True),
                                         updated.splitlines(True))))


if __name__ == '__main__':
    main()
