#!/usr/bin/env python3
"""Measure stage-1/stage-2 hot paths on the existing BIGNUM-off QuickJS host probe."""
from pathlib import Path
import argparse
import json
import os
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True, help='Built quickjs-kernel binary')
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
repo = Path(__file__).resolve().parents[1]
root = repo/'app/jizura'
args.output.mkdir(parents=True, exist_ok=True)
old = (root/'tests/fixtures/stage1-sprites.js').read_text(encoding='utf-8')
start = old.index('      const write = (i, a) =>')
end = old.index('      x.putImageData(image, 0, 0);',start)
fill = 'function oldFill(mask,d){'+old[start:end]+'}\n'
render = (root/'upstream/src/09_render.js').read_text(encoding='utf-8')
start = render.index('    let spike = 0, shake = 0, beatPulse = 0;')
end = render.index('    if (plan.beats && plan.beats.length)',start)
body = render[start:end]
camera = 'function oldCamera(events,t){'+body+'return spike+shake;}\n'
camera += 'function newCamera(events,t){'+body.replace('let i = 0;', 'let i = JizuraFrameHot.recentStart(events,t);')+'return spike+shake;}\n'
header = (root/'port/sprites.js').read_text(encoding='utf-8')+'\n'+(root/'port/frame-hot.js').read_text(encoding='utf-8')+'\n'+fill+camera
fixture = '''
const alpha=new Uint8Array(743*743);for(let i=0;i<alpha.length;i++)alpha[i]=i%743<330?255:0;
const mask=new JizuraPortSprites().registerMask(alpha,743,743),rgba=new Uint8ClampedArray(alpha.length*4);
const events=Array.from({length:20000},(_,i)=>({t:i*.06,dur:.18,type:i%2?'shake':'chroma',amp:.01}));
events.unshift({t:0,dur:2000,type:'flash',amp:.1});
const times=[];let sum=0;
for(let repeat=0;repeat<3;repeat++){
 const began=Date.now();for(let i=0;i<12;i++){WORK}
 times.push((Date.now()-began)/12);
}
let hash=2166136261;const words=new Uint32Array(rgba.buffer);
for(let i=0;i<words.length;i++)hash=Math.imul(hash^words[i],16777619)>>>0;
times.sort((a,b)=>a-b);JSON.stringify({medianMs:times[1],samplesMs:times,checksum:sum+rgba[0]+rgba[3],payloadHash:hash});
'''
work = {
    'stage1-decode':'rgba.fill(0);oldFill(mask,rgba);',
    'stage2-decode':'rgba.fill(0);JizuraPortSprites.fillMask(mask,rgba);',
    'stage1-events':'const t=1100+i/24;sum+=oldCamera(events,t);sum+=events.filter(ev=>t>=ev.t&&t<ev.t+Math.max(ev.dur,1/24)).length;',
    'stage2-events':'const t=1100+i/24;sum+=newCamera(events,t);sum+=JizuraFrameHot.activeEvents(events,t,24).length;',
}
runs = {}
for key, code in work.items():
    path = args.output/(key+'.js')
    path.write_text(header+fixture.replace('WORK',code),encoding='utf-8')
    run = subprocess.run([str(args.runner),str(path),'16'],capture_output=True,text=True,
                         env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=60)
    if run.returncode:
        raise RuntimeError(key+': '+run.stderr)
    runs[key] = json.loads(run.stdout)
    print(key,runs[key],flush=True)
assert runs['stage1-decode']['checksum']==runs['stage2-decode']['checksum']
assert runs['stage1-events']['checksum']==runs['stage2-events']['checksum']
assert runs['stage1-decode']['payloadHash']==runs['stage2-decode']['payloadHash']
report = dict(pass_checks=True,host_only=True,quickjs_version='2024-02-14',bignum=False,limit_mib=16,
    scope='Isolated synthetic decode/event queries; excludes Canvas and P4 hardware.',runs=runs)
(args.output/'quickjs-hot-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
