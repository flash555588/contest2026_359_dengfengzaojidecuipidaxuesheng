#!/usr/bin/env python3
"""Paired synthetic sensitivity/endpoint experiment; no human accuracy claim."""
import argparse,ctypes,hashlib,json,subprocess,time
from pathlib import Path
import numpy as np
from wake_streaming import stage_sources
from wake_augmentation import audio_profile,PROFILES
p=argparse.ArgumentParser(description=__doc__)
for name in ['corpus','model','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
d=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop'
sources=stage_sources(d,a.output/'baseline',a.model/'glass_wake_model.h')
original=(a.output/'baseline/glass_custom_wake.c').read_text()
# Freeze these parameters before evaluating validation/test voices.
candidate=original.replace('fmaxf(150,s->noise*3)','fmaxf(90,s->noise*2.5f)')
candidate=candidate.replace('if(s->below>=32||s->samples>=GLASS_WAKE_MAX_SAMPLES){','bool final=s->below>=32||s->samples>=GLASS_WAKE_MAX_SAMPLES;\n  bool early=s->below==16&&s->samples>=8000;\n  if(final||early){')
candidate=candidate.replace('event=ratio<s->threshold;','event=ratio<s->threshold-(early&&!final?.03f:0);')
candidate=candidate.replace('s->speaking=false;s->samples=s->above=s->below=s->pre_count=0;\n    if(event)', 'if(final||event){s->speaking=false;s->samples=s->above=s->below=s->pre_count=0;}\n    if(event)')
assert candidate!=original
candidate_sources=stage_sources(d,a.output/'candidate',a.model/'glass_wake_model.h')
(a.output/'candidate/glass_custom_wake.c').write_text(candidate)
libs={}
for label,items in [('baseline',sources),('candidate',candidate_sources)]:
 binary=a.output/(label+'.so');subprocess.run(['gcc','-shared','-fPIC','-O3','-I'+str(d),*map(str,items),'-lm','-o',str(binary)],check=True)
 lib=ctypes.CDLL(str(binary));lib.glass_custom_wake_create.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p)]
 lib.glass_custom_wake_feed.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_int16),ctypes.c_size_t]
 lib.glass_custom_wake_destroy.argtypes=[ctypes.c_void_p];libs[label]=lib
runtime=a.output/'runtime/custom';runtime.mkdir(parents=True,exist_ok=True)
(runtime/'nihao_openvela.gkw').write_bytes((a.model/'nihao_openvela.gkw').read_bytes())
rows=[r for r in json.loads((a.corpus/'corpus.json').read_text())['records'] if r['split'] in ['validation','test']]
def evaluate(lib,pcm):
 state=ctypes.c_void_p();assert not lib.glass_custom_wake_create(str(runtime.parent).encode(),ctypes.byref(state))
 events=[]
 try:
  for offset in range(0,len(pcm),320):
   chunk=pcm[offset:offset+320];ret=lib.glass_custom_wake_feed(state,chunk.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),len(chunk))
   assert ret>=0
   if ret:events.append((offset+len(chunk))*1000/16000)
 finally:lib.glass_custom_wake_destroy(state)
 return events
rng=np.random.default_rng(20261019);report={'synthetic_only':True,'human_tested':False,'parameters':{'attack_floor_rms':90,'attack_noise_ratio':2.5,'early_pause_ms':320,'early_confidence_margin':.03},'profiles':{},'paired_faster_ms':[],'failures':[]}
for profile in [*PROFILES,'far_quiet']:
 result={label:{'positive':0,'detected':0,'negative':0,'false':0} for label in libs}
 for row in rows:
  pcm=np.frombuffer((a.corpus/row['pcm']).read_bytes(),dtype='<i2').copy()
  sound=audio_profile(pcm,'quiet' if profile=='far_quiet' else profile,rng)
  if profile=='far_quiet':sound=np.round(sound*.2).astype('<i2')
  events={label:evaluate(lib,sound) for label,lib in libs.items()}
  for label,found in events.items():
   result[label]['positive' if row['label'] else 'negative']+=1
   result[label]['detected' if row['label'] else 'false']+=int(len(found)==1 if row['label'] else bool(found))
  if row['label'] and len(events['baseline'])==len(events['candidate'])==1:
   report['paired_faster_ms'].append(events['baseline'][0]-events['candidate'][0])
  if (not row['label'] and events['candidate']) or (row['label'] and events['baseline'] and not events['candidate']):
   report['failures'].append({'profile':profile,'split':row['split'],'text':row['text'],'pcm':row['pcm'],'events':events})
 report['profiles'][profile]=result;(a.output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
 print('WAKE_COMPARE',profile,result,flush=True)
report['median_paired_faster_ms']=float(np.median(report['paired_faster_ms']))
report['no_recall_regression']=all(v['candidate']['detected']>=v['baseline']['detected'] for v in report['profiles'].values())
report['no_false_increase']=all(v['candidate']['false']<=v['baseline']['false'] for v in report['profiles'].values())
(a.output/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print('WAKE_COMPARE_DONE',report['no_recall_regression'],report['no_false_increase'],report['median_paired_faster_ms'],flush=True)
