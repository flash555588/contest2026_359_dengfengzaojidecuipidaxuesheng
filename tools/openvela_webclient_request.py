"""Standards fixes for the request webclient writes; no public ABI change.

1. Host carries ":port" when the port is not the scheme default
   (RFC 9110 7.2), so servers on e.g. :8080 route the request correctly.
2. The built-in User-Agent is only added when the caller did not pass one.
   Two User-Agent fields make some servers (www.bing.com) answer 400.
"""
MARKER = '/* openvela: caller User-Agent replaces the default */'


def apply(source):
    if MARKER in source:
        return source
    host = ('          dest = append(dest, ep, g_httphost);\n'
            '          dest = append(dest, ep, ws->target.hostname);\n'
            '          dest = append(dest, ep, g_httpcrnl);\n')
    if source.count(host) != 1:
        raise ValueError('unsupported webclient Host writer')
    source = source.replace(host,
        '          dest = append(dest, ep, g_httphost);\n'
        '          dest = append(dest, ep, ws->target.hostname);\n'
        '          if (ws->target.port !=\n'
        '              (strcmp(ws->target.scheme, "https") ? 80 : 443))\n'
        '            {\n'
        '              char host_port[sizeof(":65535")];\n'
        '              snprintf(host_port, sizeof(host_port), ":%u",\n'
        '                       ws->target.port);\n'
        '              dest = append(dest, ep, host_port);\n'
        '            }\n'
        '\n'
        '          dest = append(dest, ep, g_httpcrnl);\n')

    ua = '          dest = append(dest, ep, g_httpuseragentfields);\n'
    if source.count(ua) != 1:
        raise ValueError('unsupported webclient User-Agent writer')
    source = source.replace(ua,
        '          ' + MARKER + '\n'
        '          dest = append(dest, ep,\n'
        '                        ((ctx->flags & WEBCLIENT_FLAG_TUNNEL) == 0 &&\n'
        '                         wget_has_header(ctx->headers, ctx->nheaders,\n'
        '                                         "User-Agent:")) ||\n'
        '                        (((ctx->flags & WEBCLIENT_FLAG_TUNNEL) != 0 ||\n'
        '                          ctx->proxy != NULL) &&\n'
        '                         wget_has_header(ctx->proxy_headers,\n'
        '                                         ctx->proxy_nheaders,\n'
        '                                         "User-Agent:")) ?\n'
        '                        g_httpcrnl : g_httpuseragentfields);\n')

    anchor = 'static char *append(char *dest, char *ep, const char *src)\n'
    if source.count(anchor) != 1:
        raise ValueError('unsupported webclient append helper')
    helper = ('static bool wget_has_header(FAR const char * FAR const *headers,\n'
              '                            unsigned int n, FAR const char *name)\n'
              '{\n'
              '  size_t len = strlen(name);\n'
              '  unsigned int i;\n'
              '\n'
              '  for (i = 0; headers != NULL && i < n; i++)\n'
              '    {\n'
              '      if (strncasecmp(headers[i], name, len) == 0)\n'
              '        {\n'
              '          return true;\n'
              '        }\n'
              '    }\n'
              '\n'
              '  return false;\n'
              '}\n'
              '\n')
    return source.replace(anchor, helper + anchor)
