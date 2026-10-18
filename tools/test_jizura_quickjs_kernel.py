#!/usr/bin/env python3
"""Benchmark the lossless kernel on board-version QuickJS (Linux host only)."""
from pathlib import Path
import argparse
import json
import os
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--apps', type=Path, required=True)
p.add_argument('--probe-cache', type=Path, required=True, help='BIGNUM-off probe_jizura_quickjs.py output')
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
repo = Path(__file__).resolve().parents[1]
cache = json.loads((args.probe_cache/'quickjs-probe.json').read_text())
assert cache['bignum'] is False and cache['quickjs_version'] == '2024-02-14'
args.output.mkdir(parents=True, exist_ok=True)
qjs = args.apps/'interpreters/quickjs/quickjs'
names = ['quickjs.c', 'libregexp.c', 'libunicode.c', 'cutils.c', 'libbf.c']
binary = args.output/'quickjs-kernel'
subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
    '-I'+str(qjs),str(repo/'app/jizura/tests/quickjs-kernel.c'),
    *[str(args.probe_cache/'qjs-host'/(n+'.o')) for n in names],'-lm','-ldl','-pthread','-o',str(binary)],check=True)
text = (repo/'app/jizura/upstream/src/02_fonts.js').read_text(encoding='utf-8')
start = text.index('  const N = S * S, A = new Uint8Array(N);')
end = text.index('  const pieces = [];',start)
oracle = 'function reference(img,S,res){\n'+text[start:end]+'\nreturn {labels:L,boxes,remap,nl};}\n'
kernel = (repo/'app/jizura/port/glyph-kernel.js').read_text(encoding='utf-8')
fixture = '''
const side=743, rgba=new Uint8Array(side*side*4);
for(let y=0;y<side;y++)for(let x=0;x<side;x++)rgba[(y*side+x)*4+3]=((x%97<23&&y>30&&y<710)||(y%131<22&&x>60&&x<690))?255:0;
const samples=[];let last;
for(let repeat=0;repeat<3;repeat++) {const t=Date.now();last=RUN; samples.push(Date.now()-t);}
samples.sort((a,b)=>a-b);
JSON.stringify({medianMs:samples[1],samplesMs:samples,components:last.nl});
'''
runs = []
for mode, call in [('reference','reference(rgba,side,512)'),('optimized','JizuraGlyphKernel.segment(rgba,side,side,512)')]:
    source = args.output/(mode+'-kernel.js')
    source.write_text(oracle+kernel+fixture.replace('RUN',call),encoding='utf-8')
    for limit in [4,6,8,12,16]:
        result = subprocess.run([str(binary),str(source),str(limit)],capture_output=True,text=True,
                                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=60)
        item = dict(mode=mode,limitMiB=limit,returncode=result.returncode)
        if result.returncode == 0:
            item['result'] = json.loads(result.stdout)
        else:
            item['error'] = result.stderr.strip()
        runs.append(item)
report = dict(hostOnly=True,quickjsVersion=cache['quickjs_version'],bignum=False,
    scope='Standalone segmentation kernel with synthetic RGBA input; excludes full engine and native canvas.',runs=runs)
(args.output/'quickjs-kernel-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
for run in runs:
    print(run['mode'],run['limitMiB'],run.get('result',run.get('error')))
assert all(any(r['returncode']==0 and r['mode']==mode for r in runs) for mode in ['reference','optimized'])
assert all(r['result']['components']==3 for r in runs if r['returncode']==0)
assert not any('AddressSanitizer' in r.get('error','') or 'runtime error:' in r.get('error','') for r in runs)
