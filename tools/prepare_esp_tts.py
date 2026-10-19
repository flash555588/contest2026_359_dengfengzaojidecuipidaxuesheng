#!/usr/bin/env python3
"""Fetch the pinned official ESP32-P4 Chinese ESP-TTS diagnostic inputs."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import tempfile
import time
import urllib.request
from esp_tts_inputs import source_path, upstream_lock, verify_bytes, verify_prepared


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name+'.', suffix='.tmp', delete=False) as file:
            temporary = Path(file.name)
            file.write(data)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def prepare(output, offline=False):
    lock = upstream_lock()
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)

    def fetch(row):
        target = source_path(output / 'upstream', row)
        if target.is_file():
            try:
                verify_bytes(target.read_bytes(), row)
                return row['bytes']
            except ValueError:
                pass
        if offline:
            raise ValueError('Missing or invalid offline ESP-TTS input: ' + row['path'])
        for attempt in range(3):
            try:
                with urllib.request.urlopen(row['url'], timeout=60) as response:
                    data = response.read(row['bytes'] + 1)
                verify_bytes(data, row)
                atomic_write(target, data)
                return len(data)
            except (OSError, ValueError):
                if attempt == 2:
                    raise
                time.sleep(attempt + 1)

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        sizes = list(pool.map(fetch, lock['sources']))
    # Publish the lock only after every input has passed, including LICENSE.
    atomic_write(output / 'upstream.lock.json', (json.dumps(lock, indent=2) + '\n').encode('utf-8'))
    verify_prepared(output)
    return {'revision': lock['revision'], 'files': len(sizes), 'bytes': sum(sizes)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--offline',action='store_true',help='Use verified cached inputs; never access the network')
    parser.add_argument('--verify-only',action='store_true',help='Check against the repository lock without modifying files')
    args=parser.parse_args()
    if args.verify_only:
        lock=verify_prepared(args.output)
        print('ESP_TTS_VERIFIED',len(lock['sources']),lock['revision'])
    else:
        report=prepare(args.output,args.offline)
        print('ESP_TTS_PREPARED',report['files'],report['bytes'],report['revision'])


if __name__=='__main__':main()
