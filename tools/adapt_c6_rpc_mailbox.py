#!/usr/bin/env python3
"""Prepare or explicitly apply RPC mailbox adaptation after size/serialize fixes."""
import argparse
import difflib
from pathlib import Path


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'Unexpected RPC source anchor: {old[:70]}')
    return text.replace(old, new, 1)


def adapt(text):
    text = once(text, '#include <nuttx/semaphore.h>', '#include "rpc_mailbox.h"')
    start = text.index('struct rpc_client_s\n{')
    end = text.index('static esp_hosted_wifi_event_cb_t', start)
    text = text[:start] + 'static struct c6_rpc_mailbox g_rpc = C6_RPC_MAILBOX_INITIALIZER;\n\n' + text[end:]
    text = text.replace('  FAR struct rpc_client_s *cli = &g_rpc;\n', '')
    start = text.index('  printf("rpc: rx type=')
    end = text.index('  else\n', start)
    text = text[:start] + '''  if (msg->msg_type == RPC_TYPE__Resp &&
      c6_rpc_accept(&g_rpc, msg->uid, msg))
    {
      /* Ownership moved to the waiting transaction. */
    }
''' + text[end:]
    text = once(text, '  cli->wait_uid = req->uid;\n  cli->resp     = NULL;',
                '  if (!c6_rpc_begin(&g_rpc, req->uid)) return NULL;')
    text = once(text, '''  ret = esp_hosted_register(ESP_HOSTED_IF_SERIAL, rpc_rx_cb, NULL);
  if (ret < 0)
    {
      return NULL;
    }''', '''  ret = esp_hosted_register(ESP_HOSTED_IF_SERIAL, rpc_rx_cb, NULL);
  if (ret < 0)
    {
      goto failed;
    }''')
    text = once(text, '''      printf("rpc: %s send failed %d\\n", label, ret);
      return NULL;''', '''      printf("rpc: %s send failed %d\\n", label, ret);
      goto failed;''')
    text = once(text, '''      esp_hosted_poll();
      if (sem_trywait(&cli->resp_sem) == 0)''', '''      ret = esp_hosted_poll();
      if (ret < 0) goto failed;
      if (c6_rpc_ready(&g_rpc))''')
    text = once(text, '  return cli->resp;', '''  return c6_rpc_finish(&g_rpc);

failed:
  {
    FAR Rpc *discard = c6_rpc_finish(&g_rpc);
    if (discard != NULL) rpc__free_unpacked(discard, NULL);
  }
  return NULL;''')
    text = once(text, '  response = rpc_transact_locked(req, label);', '''  if (req == NULL)
    {
      pthread_mutex_unlock(&g_rpc_transaction_lock);
      return NULL;
    }
  Rpc request = *req;
  request.uid = c6_rpc_next_uid(&g_rpc);
  response = request.uid ? rpc_transact_locked(&request, label) : NULL;''')
    if text.count('cli->busy = false;') != 2:
        raise ValueError('Unexpected legacy busy assignments')
    text = text.replace('      cli->busy = false;\n', '')
    text = text.replace('  cli->busy = false;\n', '')
    if 'cli->' in text or 'struct rpc_client_s' in text:
        raise ValueError('Legacy response-slot access remains')
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    folder = args.apps / 'system/c6probe'
    path = folder / 'esp_hosted_rpc.c'
    header = Path(__file__).resolve().parent / 'c6/rpc_mailbox.h'
    original = path.read_text()
    if '#include "rpc_mailbox.h"' in original:
        if (folder / 'rpc_mailbox.h').read_bytes() != header.read_bytes():
            raise SystemExit('Existing mailbox header differs; not overwritten')
        print('Mailbox already present; no changes')
        return
    updated = adapt(original)
    if args.apply:
        target = folder / 'rpc_mailbox.h'
        if target.exists():
            raise SystemExit('Existing mailbox header; refusing overwrite')
        target.write_bytes(header.read_bytes())
        path.write_text(updated)
        print('Applied RPC response ownership and unique UID adaptation')
    else:
        print(''.join(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                                         fromfile=str(path), tofile=str(path))))


if __name__ == '__main__':
    main()
