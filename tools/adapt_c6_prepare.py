"""Separate radio preparation from association, after link-state adaptation."""
from adapt_c6_rx_dispatch import once


def adapt(text):
    text = once(text, 'int c6net_initialize(FAR const char *ssid, FAR const char *password)',
                'static int c6net_setup(FAR const char *ssid, FAR const char *password,\n'
                '                       bool associate)')
    text = once(text, '  if (c6net_is_initialized())\n    {',
                '  if (c6net_is_initialized())\n    {\n'
                '      if (!associate) { ret = 0; goto out; }')
    anchor = '  ret = esp_hosted_rpc_wifi_connect(ssid, password);\n\nout:'
    text = once(text, anchor,
                '  ret = associate ? esp_hosted_rpc_wifi_connect(ssid, password) : 0;\n\nout:')
    return once(text, 'int c6net_connect(FAR const char *ssid, FAR const char *password)', '''int c6net_prepare(void)
{
  return c6net_setup(NULL, NULL, false);
}

int c6net_initialize(FAR const char *ssid, FAR const char *password)
{
  return c6net_setup(ssid, password, true);
}

int c6net_connect(FAR const char *ssid, FAR const char *password)''')
