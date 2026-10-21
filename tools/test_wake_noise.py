"""Frozen offline comparison; synthetic signals are not room/TV acceptance."""
from pathlib import Path
import argparse, ctypes, hashlib, json, subprocess, sys
import numpy as np

parser=argparse.ArgumentParser(description=__doc__)
for name in ['corpus','baseline-source','output']:parser.add_argument('--'+name,type=Path,required=True)
args=parser.parse_args()
root=args.output.resolve();root.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(repo/'tools'))
from wake_streaming import stage_sources
from wake_augmentation import audio_profile, PROFILES
from test_wake_scenarios import Detector
desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
corpus=args.corpus
model=repo/'app/espdl-quickapp/models/nihao_openvela.gkw'
runtime=root/'runtime/custom';runtime.mkdir(parents=True,exist_ok=True)
(runtime/'nihao_openvela.gkw').write_bytes(model.read_bytes())
libraries={}
for name in ['baseline','candidate']:
    staged=root/name
    sources=stage_sources(args.baseline_source if name=='baseline' else desktop,staged)
    library=root/(name+'.so')
    subprocess.run(['gcc','-shared','-fPIC','-O3','-I'+str(staged),'-I'+str(desktop),*map(str,sources),'-lm','-o',str(library)],check=True)
    lib=ctypes.CDLL(str(library))
    lib.glass_custom_wake_create.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p)]
    lib.glass_custom_wake_feed.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_int16),ctypes.c_size_t]
    lib.glass_custom_wake_destroy.argtypes=[ctypes.c_void_p]
    lib.glass_custom_wake_evaluations.argtypes=[ctypes.c_void_p]
    libraries[name]=lib

def measure(lib,sound):
    detector=Detector(lib,runtime.parent)
    try:
        events=detector.feed(sound,(320,))
        return {'events':events,'evaluations':int(lib.glass_custom_wake_evaluations(detector.state))}
    finally:detector.close()

rows=json.loads((corpus/'corpus.json').read_text())['records']
rows=[r for r in rows if r['split'] in ['validation','test']]
audio={r['pcm']:np.frombuffer((corpus/r['pcm']).read_bytes(),dtype='<i2').copy() for r in rows}
rng=np.random.default_rng(20261021)
report={'synthetic_only':True,'human_room_tested':False,'model_sha256':hashlib.sha256(model.read_bytes()).hexdigest(),'profiles':{},'environment':{},'differences':[]}
for profile in [*PROFILES,'far_quiet','bed_800','bed_1200','bed_2000','bed_3000']:
    stats={n:{'positive':0,'detected':0,'negative':0,'false':0} for n in libraries}
    for row in rows:
        if profile.startswith('bed_'):
            level=int(profile.split('_')[1])
            raw=audio[row['pcm']].astype(float)
            raw*=level*3/(float(np.sqrt(np.mean(raw**2)))+1)
            sound=np.pad(raw,(9600,19200))
            sound=np.clip(sound+rng.normal(0,level,len(sound)),-32768,32767).astype('<i2')
        else:sound=audio_profile(audio[row['pcm']],'quiet' if profile=='far_quiet' else profile,rng)
        if profile=='far_quiet':sound=np.round(sound*.2).astype('<i2')
        found={n:measure(lib,sound) for n,lib in libraries.items()}
        for name,value in found.items():
            positive=bool(row['label']);stats[name]['positive' if positive else 'negative']+=1
            stats[name]['detected' if positive else 'false']+=int(len(value['events'])==1 if positive else bool(value['events']))
        if len(found['baseline']['events'])!=len(found['candidate']['events']):
            report['differences'].append({'profile':profile,'pcm':row['pcm'],'label':row['label'],'events':{n:len(v['events']) for n,v in found.items()}})
    report['profiles'][profile]=stats
    print('PAIRED',profile,stats,flush=True)

# A listener can begin in the middle of a real wake phrase. Keep this
# counterexample separate from the stable-noise-prefix checks above.
startup={}
for gain in [1,2,4,8]:
    stats={name:0 for name in libraries};total=0
    for row in [r for r in rows if r['label']]:
        raw=audio[row['pcm']];first=0
        while first+320<len(raw) and np.sqrt(np.mean(raw[first:first+320].astype(float)**2))<150:first+=320
        sound=np.concatenate([np.clip(raw[first:].astype(float)*gain,-32768,32767).astype('<i2'),np.zeros(19200,dtype='<i2')])
        total+=1
        for name,lib in libraries.items():stats[name]+=int(len(measure(lib,sound)['events'])==1)
    startup[str(gain)]={'positive':total,**stats}
    print('STARTUP_GAIN',gain,startup[str(gain)],flush=True)
report['startup']=startup
report['no_recall_regression']=all(v['candidate']['detected']>=v['baseline']['detected'] for v in report['profiles'].values())
report['no_false_increase']=all(v['candidate']['false']<=v['baseline']['false'] for v in report['profiles'].values())
(root/'comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print('COMPARISON_DONE',report['no_recall_regression'],report['no_false_increase'],flush=True)
assert report['no_recall_regression'] and report['no_false_increase']
assert all(v['candidate']>=v['baseline'] for v in startup.values())
assert all(report['profiles'][p]['candidate']['detected']>report['profiles'][p]['baseline']['detected'] for p in ['bed_2000','bed_3000'])
print('WAKE_NOISE_PAIRED_OK: synthetic development evidence; real-room acceptance pending',flush=True)
