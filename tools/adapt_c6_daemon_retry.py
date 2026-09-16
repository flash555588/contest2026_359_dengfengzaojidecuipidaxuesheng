"""Preserve registered netdev ownership across daemon creation failure."""
import argparse
import difflib
from pathlib import Path
from adapt_c6_rx_dispatch import once


def adapt(text):
    text = once(text, '  volatile bool initialized;',
                '  bool registered;\n  volatile bool initialized;')
    text = once(text, '  ret = esp_hosted_initialize(false);',
                '  if (priv->registered) goto start_daemon;\n\n'
                '  ret = esp_hosted_initialize(false);')
    text = once(text, '''  priv->initialized = true;
  priv->ifup = true;''', '''  priv->registered = true;

start_daemon:
  priv->initialized = true;
  priv->ifup = true;''')
    text = once(text, '''      priv->initialized = false;
      ret = -errno;
      goto out;''', '''      ret = -errno;
      priv->initialized = false;
      priv->ifup = false;
      c6net_ifdown(&priv->dev);
      goto out;''')
    # All connect callers must check initialization under the same mutex.
    start = text.index('int c6net_connect(')
    end = text.index('bool c6net_is_initialized(', start)
    return text[:start] + '''int c6net_connect(FAR const char *ssid, FAR const char *password)
{
  return c6net_initialize(ssid, password);
}

''' + text[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    path = args.apps / 'system/c6probe/c6net.c'
    original = path.read_text()
    updated = adapt(original)
    if args.apply:
        path.write_text(updated)
    else:
        print(''.join(difflib.unified_diff(original.splitlines(True),
                                         updated.splitlines(True))))


if __name__ == '__main__':
    main()
