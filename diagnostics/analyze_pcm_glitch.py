"""Locate periodic glitches in a PCM hexdump captured from the board.

Usage: analyze_pcm_glitch.py <evidence.json> [skip-bytes] [jump-threshold]

The evidence file may contain several hexdumps; the one that holds the PCM is
selected by <skip-bytes>, which is the file offset the dump started at.  Every
header dump in the same file is ignored.  The report lists the strongest
sample-to-sample jumps and how their positions distribute modulo the buffer
sizes that the capture path can produce.
"""
import json
import re
import statistics
import sys


def dumps(path):
    """Return {dump name: bytes} for every hexdump in the capture."""
    text = '\n'.join(item.get('output', '') for item in json.loads(
        open(path, encoding='utf-8').read()))
    result = {}
    name = None
    for line in text.splitlines():
        start = re.match(r'^/tmp/\S+ at ([0-9a-f]{8}):', line)
        if start:
            name = line.split(' at ')[0]
            result.setdefault(name, bytearray())
            continue
        m = re.match(r'^([0-9a-f]{4,}):((?: [0-9a-f]{2}){1,16})\s', line + ' ')
        if m and name is not None:
            result[name].extend(int(b, 16) for b in m.group(2).split())
    return result


def main():
    path = sys.argv[1]
    skip = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    threshold = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    raws = dumps(path)
    raw = max(raws.values(), key=len)[skip:]
    if len(raw) % 2:
        raw = raw[:-1]
    s = [int.from_bytes(raw[i:i + 2], 'little', signed=True)
         for i in range(0, len(raw), 2)]
    print(f'== {path}: {len(s)} samples, min={min(s)} max={max(s)} '
          f'rms={(sum(v * v for v in s) / len(s)) ** 0.5:.0f}')

    jumps = sorted(((abs(s[i + 1] - s[i]), i * 2) for i in range(len(s) - 1)),
                   reverse=True)
    print('largest jumps (bytes into data):')
    for magnitude, offset in jumps[:20]:
        print(f'  +{offset:<6} {magnitude:>6}  '
              f'ctx={s[max(0, offset // 2 - 3):offset // 2 + 4]}')

    strong = [offset for magnitude, offset in jumps if magnitude > threshold]
    print(f'jumps > {threshold}: {len(strong)}; first {strong[:12]}')
    for period in (1024, 2016, 2048, 4032, 8064):
        counts = {}
        for offset in strong:
            counts.setdefault(offset % period, 0)
            counts[offset % period] += 1
        top = sorted(counts.items(), key=lambda kv: -kv[1])[:4]
        print(f'  modulo {period:<5}: {top}')

    step = statistics.median(
        abs(s[i + 1] - s[i]) for i in range(len(s) - 1))
    print(f'median step={step}')


main()
