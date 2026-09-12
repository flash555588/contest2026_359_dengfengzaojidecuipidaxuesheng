"""Serialize Hosted RX ownership; preview by default, never operate hardware."""
import argparse
import difflib
from pathlib import Path


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Unexpected source anchor: {old[:80]}")
    return text.replace(old, new, 1)


def adapt(text):
    text = once(text, '#include <errno.h>', '#include <errno.h>\n#include <pthread.h>')
    text = once(text, 'int esp_hosted_register(uint8_t if_type, esp_hosted_rx_cb_t cb,',
                '''/* Lock order: RX ownership before bus. Callbacks may send, but must
 * not register callbacks or issue synchronous RPCs from dispatch context.
 */
static pthread_mutex_t g_rx_dispatch_lock = PTHREAD_MUTEX_INITIALIZER;

int esp_hosted_register(uint8_t if_type, esp_hosted_rx_cb_t cb,''')
    text = once(text, '''  g_hosted.rx_cb[if_type]  = cb;
  g_hosted.rx_arg[if_type] = arg;
  return OK;''', '''  int ret = pthread_mutex_lock(&g_rx_dispatch_lock);
  if (ret != 0) return -ret;
  g_hosted.rx_cb[if_type]  = cb;
  g_hosted.rx_arg[if_type] = arg;
  pthread_mutex_unlock(&g_rx_dispatch_lock);
  return OK;''')
    start = text.index('int esp_hosted_poll(void)')
    end = text.index('/****************************************************************************', start)
    text = text[:start] + '''int esp_hosted_poll(void)
{
  uint32_t framelen = 0;
  int ret = pthread_mutex_trylock(&g_rx_dispatch_lock);
  if (ret == EBUSY) return 0;
  if (ret != 0) return -ret;

  ret = nxmutex_lock(&g_hosted.lock);
  if (ret < 0) goto out;
  if (!g_hosted.up)
    {
      ret = -ENOTCONN;
      nxmutex_unlock(&g_hosted.lock);
      goto out;
    }
  ret = hosted_read_frame(&framelen);
  nxmutex_unlock(&g_hosted.lock);
  if (ret < 0) goto out;
  if (framelen == 0)
    {
      ret = 0;
      goto out;
    }

  /* Keep frame ownership while allowing callbacks to acquire the bus. */
  hosted_dispatch_stream(g_frame_buf, framelen);
  ret = 1;
out:
  pthread_mutex_unlock(&g_rx_dispatch_lock);
  return ret;
}

''' + text[end:]
    return once(text, '  cb = g_hosted.rx_cb[if_type];',
                '  if (if_type >= ESP_HOSTED_IF_MAX) return;\n\n'
                '  cb = g_hosted.rx_cb[if_type];')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    path = args.apps / 'system/c6probe/esp_hosted.c'
    original = path.read_text()
    if 'g_rx_dispatch_lock' in original:
        raise SystemExit('RX lock already present; inspect rather than overwrite')
    updated = adapt(original)
    if args.apply:
        path.write_text(updated)
    else:
        print(''.join(difflib.unified_diff(original.splitlines(True),
                                         updated.splitlines(True))))


if __name__ == '__main__':
    main()
