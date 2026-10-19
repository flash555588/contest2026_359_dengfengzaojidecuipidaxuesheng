#!/usr/bin/env python3
"""Compare two authenticated models on the same frozen synthetic room clips."""
import argparse,ctypes,json,subprocess
from pathlib import Path
import numpy as np
from wake_streaming import stage_sources
from wake_augmentation import audio_profile,PROFILES
from test_wake_scenarios import Detector
p=argparse.ArgumentParser(description=__doc__)
for name in ['corpus','baseline-model','candidate-model','baseline-source','output']:p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
desktop=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop';models={}
for label,model,source in [('baseline',a.baseline_model,a.baseline_source),('candidate',a.candidate_model,desktop)]:
 sources=stage_sources(source,a.output/label,model/'glass_wake_model.h');runtime=a.output/label/'runtime/custom';runtime.mkdir(parents=True,exist_ok=True)
 (runtime/'nihao_openvela.gkw').write_bytes((model/'nihao_openvela.gkw').read_bytes())
 binary=a.output/(label+'.so');subprocess.run(['gcc','-shared','-fPIC','-O3','-I'+str(desktop),*map(str,sources),'-lm','-o',str(binary)],check=True)
 lib=ctypes.CDLL(str(binary));lib.glass_custom_wake_create.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p)]
 lib.glass_custom_wake_feed.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_int16),ctypes.c_size_t];lib.glass_custom_wake_destroy.argtypes=[ctypes.c_void_p]
 models[label]=(lib,runtime.parent)
rows=[r for r in json.loads((a.corpus/'corpus.json').read_text())['records'] if r['split'] in ['validation','test']]
report={'synthetic_only':True,'human_tested':False,'profiles':{},'paired_faster_ms':[]}
rng=np.random.default_rng(20261019)
for profile in [*PROFILES,'far_quiet']:
 result={name:{'positive':0,'detected':0,'negative':0,'false':0} for name in models}
 for row in rows:
  raw=np.frombuffer((a.corpus/row['pcm']).read_bytes(),dtype='<i2').copy()
  sound=audio_profile(raw,'quiet' if profile=='far_quiet' else profile,rng)
  if profile=='far_quiet':sound=np.round(sound*.2).astype('<i2')
  events={}
  for name,(lib,root) in models.items():
   detector=Detector(lib,root)
   try:events[name]=detector.feed(sound,(320,))
   finally:detector.close()
   result[name]['positive' if row['label'] else 'negative']+=1
   result[name]['detected' if row['label'] else 'false']+=int(len(events[name])==1 if row['label'] else bool(events[name]))
  if row['label'] and len(events['baseline'])==len(events['candidate'])==1:report['paired_faster_ms'].append((events['baseline'][0]-events['candidate'][0])*1000/16000)
 report['profiles'][profile]=result;print('MODEL_COMPARE',profile,result,flush=True)
report['median_paired_faster_ms']=float(np.median(report['paired_faster_ms']))
(a.output/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
