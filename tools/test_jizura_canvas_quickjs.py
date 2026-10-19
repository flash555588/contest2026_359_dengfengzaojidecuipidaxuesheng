#!/usr/bin/env python3
"""Compare real text layout with/without bounded line preparation on host QuickJS."""
from pathlib import Path
import argparse,json,os,subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner',type=Path,required=True)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]/'app/jizura'
header='''const J={isSmallKana:c=>'ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ'.includes(c),VERT_ROTATE:'ー〜～…‥―—-()（）「」『』【】'};
J.metrics={m:new Map(),adv(font,ch){const k=font+'\\0'+ch;let v=this.m.get(k);if(v===undefined){v=/[A-Za-z0-9]/.test(ch)?.53125:1;this.m.set(k,v);}return v;}};
'''
fixture='''
const lines=['把喧嚣留在身后','Hello light 世界','カタカナの光はここに','ひらがなと漢字 A123','🙂光\\nEvery stroke echoes'];
let sum=0,hash=2166136261;const samples=[];
for(let repeat=0;repeat<3;repeat++){
 const began=Date.now();
 for(let i=0;i<2000;i++){
  J.setTypeset(i%2===0);
  const lay=J.layoutText({text:lines[i%lines.length],size:31+i%37,font:'fixed',track:.03,vertical:i%3===0,align:i%4===0?'left':'center'});
  sum+=lay.W+lay.H;
  for(const g of lay)hash=Math.imul(hash^Math.round((g.x+g.y+g.w+g.h)*1000),16777619)>>>0;
 }
 samples.push((Date.now()-began)/2000);
}
samples.sort((a,b)=>a-b);JSON.stringify({medianMs:samples[1],samplesMs:samples,checksum:sum,payloadHash:hash,cache:J.portTextLines?{...J.portTextLines.stats,entries:J.portTextLines.map.size,chars:J.portTextLines.chars}:null});
'''
runs={}
for name,file in [('stage2','stage2-engine.js'),('stage3','optimized-engine.js')]:
    text=(a.build/file).read_text(encoding='utf-8')
    start=text.index('/* ============================================================\n   JIZURA — text items:')
    end=text.index('/* Blurred / glowing items',start)
    source=text[start:end]+'\n})();\n'
    helper=(root/'port/canvas-hot.js').read_text(encoding='utf-8') if name=='stage3' else ''
    path=a.output/(name+'-layout.js');path.write_text(header+helper+'\n'+source+fixture,encoding='utf-8')
    r=subprocess.run([str(a.runner),str(path),'16'],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=60)
    if r.returncode:raise RuntimeError(r.stderr)
    runs[name]=json.loads(r.stdout);print(name,runs[name],flush=True)
assert runs['stage2']['checksum']==runs['stage3']['checksum']
assert runs['stage2']['payloadHash']==runs['stage3']['payloadHash']
report=dict(pass_checks=True,host_only=True,quickjs_version='2024-02-14',bignum=False,limit_mib=16,
            scope='Synthetic text layout workload, fixed numeric advances, all glyph coordinates hashed. No Canvas or P4 hardware.',runs=runs)
(a.output/'quickjs-layout-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
