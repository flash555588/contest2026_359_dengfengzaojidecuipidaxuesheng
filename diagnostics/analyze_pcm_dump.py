"""Parse NSH hexdump evidence and look for periodic glitches in PCM."""
import json, re, sys, statistics

def load(path):
    data = json.loads(open(path, encoding='utf-8').read())
    text = '\n'.join(item.get('output', '') for item in data)
    out = bytearray()
    for line in text.splitlines():
        m = re.match(r'^([0-9a-f]{4,}):((?: [0-9a-f]{2}){1,16})\s', line + ' ')
        if m:
            out.extend(int(b, 16) for b in m.group(2).split())
    return out

def report(path, base):
    raw = load(path)
    if len(raw) % 2:
        raw = raw[:-1]
    s = [int.from_bytes(raw[i:i+2], 'little', signed=True) for i in range(0, len(raw), 2)]
    if not s:
        print(path, 'empty'); return
    diffs = [abs(s[i+1]-s[i]) for i in range(len(s)-1)]
    med = statistics.median(s)
    print(f'== {path} base(data off)={base} samples={len(s)} min={min(s)} max={max(s)} '
          f'median={med:.0f} rms={ (sum(v*v for v in s)/len(s))**0.5 :.0f} maxjump={max(diffs)}')
    # glitches: samples deviating strongly from local median
    spikes = [(base + 2*i, s[i]) for i in range(len(s))
              if abs(s[i] - statistics.median(s[max(0,i-8):i+9])) > 3000]
    print('  spikes(>3000 dev):', [(off, v) for off, v in spikes][:40])
    for off, v in spikes:
        i = (off - base)//2
        lo = max(0, i-4); hi = min(len(s), i+5)
        print(f'    spike at data={off} file={off+44} value={v} ctx={s[lo:hi]}')
    for i in range(1, len(s)):
        if base + 2*i == (base + 2*i) // 1024 * 1024:
            print(f'    boundary data={base+2*i}: {s[max(0,i-3):i+4]}')
for arg in sys.argv[1:]:
    p, b = arg.split('=')
    report(p, int(b))
