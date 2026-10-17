#!/usr/bin/env python3
"""Connect passive RSSI telemetry to the prepared ESP-Hosted RPC owner."""
from pathlib import Path
def apply(apps):
    path=Path(apps)/'system/c6probe/esp_hosted_rpc.c'
    text=path.read_text()
    include='#include "status_rssi.inc"'
    if include in text:return
    if 'static FAR Rpc *rpc_transact' not in text and 'rpc_transact(' not in text:
        raise ValueError('Missing serialized ESP-Hosted RPC implementation')
    path.write_text(text.rstrip()+'\n\n'+include+'\n')
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apps',required=True,type=Path)
    apply(p.parse_args().apps)
