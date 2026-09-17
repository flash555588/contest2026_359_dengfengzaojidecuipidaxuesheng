"""Add owned scan results without scraping console output. Preview by default."""
import argparse
import difflib
from pathlib import Path


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Unexpected source anchor: {old[:80]}")
    return text.replace(old, new, 1)


def adapt(text):
    start = text.index('int esp_hosted_rpc_wifi_scan(void)')
    end = text.index('/****************************************************************************', start)
    scan = text[start:end]
    scan = once(scan, 'int esp_hosted_rpc_wifi_scan(void)',
                'int esp_hosted_rpc_wifi_scan_results(struct c6_scan_ap *records,\n'
                '                                    size_t capacity, size_t *count)')
    scan = once(scan, '  size_t i;', '''  size_t i;

  if (count == NULL) return -EINVAL;
  *count = 0;
  if (records == NULL || capacity == 0) return -EINVAL;
  if (capacity > C6_SCAN_LIMIT) capacity = C6_SCAN_LIMIT;''')
    scan = once(scan, 'resp->payload_case == RPC__PAYLOAD_RESP_WIFI_SCAN_START ?',
                'resp->payload_case == RPC__PAYLOAD_RESP_WIFI_SCAN_START &&\n'
                '           resp->resp_wifi_scan_start != NULL ?')
    for kind in ('NUM', 'RECORDS'):
        member = 'resp_wifi_scan_get_ap_' + kind.lower()
        anchor = f'if (resp->payload_case != RPC__PAYLOAD_RESP_WIFI_SCAN_GET_AP_{kind})'
        scan = once(scan, anchor,
                    anchor[:-1] + f' ||\n      resp->{member} == NULL)')
    scan = once(scan, '  if (number > 10)\n    {\n      number = 10;\n    }',
                '  if ((size_t)number > capacity) number = (int)capacity;')
    begin = scan.index('  printf("rpc: %u AP(s):')
    finish = scan.index('  rpc__free_unpacked(resp, NULL);', begin)
    scan = scan[:begin] + '''  RpcRespWifiScanGetApRecords *result = resp->resp_wifi_scan_get_ap_records;
  if (result->resp != 0 || result->n_ap_records > capacity ||
      (result->n_ap_records && result->ap_records == NULL))
    {
      rpc__free_unpacked(resp, NULL);
      return -EIO;
    }

  for (i = 0; i < result->n_ap_records; i++)
    {
      WifiApRecord *ap = result->ap_records[i];
      if (ap == NULL || c6_scan_ap_set(&records[i], ap->ssid.data,
                                     ap->ssid.len, ap->rssi, ap->primary) < 0)
        {
          rpc__free_unpacked(resp, NULL);
          return -EIO;
        }
    }
  *count = result->n_ap_records;

''' + scan[finish:]
    wrapper = '''int esp_hosted_rpc_wifi_scan(void)
{
  struct c6_scan_ap records[C6_SCAN_LIMIT];
  size_t count = 0;
  int ret = esp_hosted_rpc_wifi_scan_results(records, C6_SCAN_LIMIT, &count);
  if (ret < 0) return ret;
  printf("rpc: %u AP(s):\\n", (unsigned int)count);
  for (size_t i = 0; i < count; i++)
    printf("  ch%u rssi=%d %.*s\\n", (unsigned int)records[i].channel,
           (int)records[i].rssi, (int)records[i].ssid_length,
           (const char *)records[i].ssid);
  return 0;
}

'''
    return '#include "scan_results.h"\n' + text[:start] + scan + wrapper + text[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    folder = args.apps / 'system/c6probe'
    path = folder / 'esp_hosted_rpc.c'
    header = Path(__file__).parent / 'c6/scan_results.h'
    original = path.read_text()
    if '#include "scan_results.h"' in original:
        if (folder / 'scan_results.h').read_bytes() != header.read_bytes():
            raise SystemExit('Scan header mismatch; not overwritten')
        print('Scan results already present')
        return
    updated = adapt(original)
    if args.apply:
        if (folder / 'scan_results.h').exists():
            raise SystemExit('Existing scan header; not overwritten')
        (folder / 'scan_results.h').write_bytes(header.read_bytes())
        path.write_text(updated)
    else:
        print(''.join(difflib.unified_diff(original.splitlines(True),
                                         updated.splitlines(True))))


if __name__ == '__main__':
    main()
