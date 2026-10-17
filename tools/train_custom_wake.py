#!/usr/bin/env python3
"""Train a small MFCC/DTW wake model and calibrate against confusable speech.

Template bank is fitted on training voices only. Threshold uses validation;
held-out test metrics are synthetic development evidence, not human FAR/hour.
"""
import argparse,ctypes,hashlib,json,struct,subprocess
from pathlib import Path
import numpy as np
from wake_streaming import calibration_detector
from wake_augmentation import audio_profile, PROFILES

COMMAND_NEGATIVES=['打开 OpenVela','关闭 OpenVela']

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--corpus',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--room-augmentation',action='store_true',help='Fit the same-size template bank to noise, echo and playback profiles')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    desktop=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop'
    shared=a.output/'wake-features.so'
    subprocess.run(['gcc','-shared','-fPIC','-O3',str(desktop/'glass_wake_features.c'),'-lm','-o',str(shared)],check=True)
    lib=ctypes.CDLL(str(shared));fp=ctypes.POINTER(ctypes.c_float);sp=ctypes.POINTER(ctypes.c_int16)
    lib.glass_wake_extract.argtypes=[sp,ctypes.c_size_t,fp];lib.glass_wake_extract.restype=ctypes.c_int
    lib.glass_wake_dtw.argtypes=[fp,fp];lib.glass_wake_dtw.restype=ctypes.c_float
    lib.glass_wake_ratio.argtypes=[fp,fp];lib.glass_wake_ratio.restype=ctypes.c_float
    manifest=json.loads((a.corpus/'corpus.json').read_text(encoding='utf-8'));rows=manifest['records'];rng=np.random.default_rng(20261016)
    speakers={split:{r['speaker'] for r in rows if r['split']==split} for split in ['train','validation','test']}
    assert not (speakers['train']&speakers['validation'] or speakers['train']&speakers['test'] or speakers['validation']&speakers['test'])
    def extract(sound):
        sound=np.clip(sound[:64000],-32768,32767).astype(np.int16);feature=np.zeros(416,dtype=np.float32)
        ret=lib.glass_wake_extract(sound.ctypes.data_as(sp),len(sound),feature.ctypes.data_as(fp))
        if ret:raise ValueError('Invalid/non-speech training input')
        return feature
    base=[];audio=[]
    for row in rows:
        raw=(a.corpus/row['pcm']).read_bytes();assert hashlib.sha256(raw).hexdigest()==row['sha256']
        sound=np.frombuffer(raw,dtype='<i2').astype(np.float32);audio.append(sound);base.append(extract(sound))
    base=np.array(base,dtype=np.float32)
    positive=[i for i,r in enumerate(rows) if r['split']=='train' and r['label']==1 and r['rate']=='+0%']
    negative=[]
    # Cover commands addressed to the device, then mine the remaining confusers
    # using only training voices. Do not select templates using test errors.
    for voice in sorted(speakers['train']):
        candidates=[i for i,r in enumerate(rows) if r['split']=='train' and r['label']==0 and r['speaker']==voice]
        other=[i for i in positive if rows[i]['speaker']!=voice]
        def hardness(index):
            distances=[float(lib.glass_wake_dtw(base[index].ctypes.data_as(fp),base[j].ctypes.data_as(fp))) for j in other]
            return sum(sorted(distances)[:3])/3
        anchors=[next(i for i in candidates if rows[i]['text']==text) for text in COMMAND_NEGATIVES]
        ranked=sorted((i for i in candidates if i not in anchors),key=hardness)
        negative.extend(anchors+ranked[:6])
    assert len(positive)==18 and len(negative)==48,'Template architecture expects six training voices'
    ids=positive+negative;bank=[];bank_meta=[]
    bank_rng=np.random.default_rng(20261018)
    for index in ids:
        for rate in [1,.85,1.15]:
            sound=audio[index]
            shifted=sound if rate==1 else np.interp(np.arange(0,len(sound),rate),np.arange(len(sound)),sound)
            profile='clean'
            if a.room_augmentation and rate!=1:
                profile=['noise_20db','echo','playback'][int(bank_rng.integers(3))]
                shifted=audio_profile(shifted,profile,bank_rng)
            bank.append(extract(shifted));bank_meta.append({**rows[index],'template_resample_rate':rate,'acoustic_profile':profile})
    templates=np.ascontiguousarray(bank,dtype=np.float32);tp=templates.ctypes.data_as(fp)
    stream=calibration_detector(desktop,a.output/'calibration',templates)
    streaming_validation=[]
    validation_scenarios={profile:[] for profile in PROFILES} if a.room_augmentation else {}
    validation_rng=np.random.default_rng(20261019)
    results={name:[] for name in speakers}
    for index,row in enumerate(rows):
        count=6 if row['split']=='train' else 10
        for variant in range(count):
            sound=audio[index].copy()
            if variant:
                rate=rng.uniform(.85,1.15);sound=np.interp(np.arange(0,len(sound),rate),np.arange(len(sound)),sound)
                delay=int(rng.uniform(.008,.045)*16000)
                if len(sound)>delay:sound[delay:]+=rng.uniform(0,.3)*sound[:-delay].copy()
                sound*=rng.uniform(.25,1.6);rms=np.sqrt(np.mean(sound**2)+1)
                sound+=rng.normal(0,rms*10**(-rng.uniform(12,35)/20),len(sound))
            feature=base[index] if not variant else extract(sound)
            ratio=float(lib.glass_wake_ratio(tp,feature.ctypes.data_as(fp)))
            results[row['split']].append((row['label'],ratio,row['speaker'],row['text'],variant))
            if row['split']=='validation':
                pcm=np.concatenate([np.clip(sound,-32768,32767).astype(np.int16),np.zeros(16000,dtype=np.int16)])
                streaming_validation.append((row['label'],stream(pcm),row['speaker'],row['text'],variant))
        if row['split']=='validation' and a.room_augmentation:
            for profile in PROFILES:
                pcm=audio_profile(audio[index],profile,validation_rng)
                validation_scenarios[profile].append((row['label'],stream(pcm),row['speaker'],row['text'],0))
        if index%50==0:print('WAKE_FIT',index,flush=True)
    val=results['validation'];positives=np.array([r[1] for r in val if r[0]]);negatives=np.array([r[1] for r in val if not r[0]])
    stream_pos=np.array([r[1] for r in streaming_validation if r[0]])
    stream_neg=np.array([r[1] for r in streaming_validation if not r[0]])
    # Reserve a margin beneath the documented 1% development false-positive
    # budget, with the stricter offline/streaming validation bound prevailing.
    ceiling=min(float(np.quantile(negatives,.008)),float(np.quantile(stream_neg,.008)))*.995
    if a.room_augmentation:
        ceiling=min(ceiling,*[float(np.quantile([r[1] for r in rr if not r[0]],.008))*.995 for rr in validation_scenarios.values()])
    desired=max(float(np.quantile(positives,.98)),float(np.quantile(stream_pos,.98)))
    threshold=(ceiling+desired)/2 if desired<ceiling else ceiling
    if not .5<threshold<1:raise RuntimeError('Calibration outside supported model range')
    def metrics(name):
        rr=streaming_validation if name=='streaming_validation' else validation_scenarios[name] if name in validation_scenarios else results[name];pos=[r[1] for r in rr if r[0]];neg=[r[1] for r in rr if not r[0]]
        return {'samples':len(rr),'positive':len(pos),'negative':len(neg),
          'true_positive':sum(x<threshold for x in pos),'false_positive':sum(x<threshold for x in neg),
          'recall':sum(x<threshold for x in pos)/len(pos),'false_positive_fraction':sum(x<threshold for x in neg)/len(neg),
          'min_negative_ratio':min(neg),'max_positive_ratio':max(pos),
          'clean_recall':sum(r[1]<threshold for r in rr if r[0] and not r[4])/sum(r[0]==1 and r[4]==0 for r in rr)}
    report={'wake_phrase':manifest['wake_phrase'],'architecture':'MFCC13x32; 54 positive + 144 hard-negative templates; banded DTW over first 8 coefficients; three-nearest ratio',
      'synthetic_only':True,'human_wake_tested':False,'human_false_alarms_per_hour':None,
      'split_policy':manifest['split_policy'],'speakers':{k:sorted(v) for k,v in speakers.items()},'ratio_threshold':threshold,
      'seed':20261016,'metrics':{k:metrics(k) for k in [*speakers,'streaming_validation']},'templates':bank_meta,
      'room_augmentation':a.room_augmentation,'validation_scenarios':{k:metrics(k) for k in validation_scenarios},
      'calibration_policy':'Training-only templates; offline and production-VAD validation threshold; test is synthetic development evidence, not an untouched final evaluation',
      'corpus_sha256':hashlib.sha256((a.corpus/'corpus.json').read_bytes()).hexdigest(),
      'source_sha256':{name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in [('train_custom_wake.py',Path(__file__)),('wake_streaming.py',Path(__file__).with_name('wake_streaming.py')),('wake_augmentation.py',Path(__file__).with_name('wake_augmentation.py')),('glass_custom_wake.c',desktop/'glass_custom_wake.c'),('glass_custom_wake.h',desktop/'glass_custom_wake.h'),('glass_wake_features.c',desktop/'glass_wake_features.c'),('glass_wake_features.h',desktop/'glass_wake_features.h')]}}
    passed=all(report['metrics'][k]['recall']>=.85 and report['metrics'][k]['clean_recall']>=.85 and report['metrics'][k]['false_positive_fraction']<=.01 for k in ['validation','streaming_validation','test'])
    if a.room_augmentation:
        passed=passed and all(r['recall']>=.85 and r['false_positive_fraction']<=.01 for r in report['validation_scenarios'].values())
    report['synthetic_gate_passed']=passed
    (a.output/'training.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (a.output/'scores.json').write_text(json.dumps(results,ensure_ascii=False)+'\n',encoding='utf-8')
    (a.output/'streaming-validation.json').write_text(json.dumps(streaming_validation,ensure_ascii=False)+'\n',encoding='utf-8')
    (a.output/'validation-scenarios.json').write_text(json.dumps(validation_scenarios,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps(report['metrics'],indent=2),flush=True)
    if a.room_augmentation:print('ROOM_VALIDATION',json.dumps(report['validation_scenarios'],indent=2),flush=True)
    if not passed:raise RuntimeError('Synthetic gate failed; model NOT exported')
    header=struct.pack('<4sIIIIfII',b'GKW3',416,54,144,16000,threshold,64000,1)
    data=header+templates.astype('<f4').tobytes();digest=hashlib.sha256(data).digest();(a.output/'nihao_openvela.gkw').write_bytes(data)
    report['model_bytes']=len(data);report['model_sha256']=digest.hex()
    (a.output/'training.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    auth='/* Generated by train_custom_wake.py. SPDX-License-Identifier: Apache-2.0 */\n#pragma once\n'
    auth+='#define GLASS_WAKE_PHRASE "你好 openvela"\n#define GLASS_WAKE_MODEL_BYTES '+str(len(data))+'\n'
    auth+='static const unsigned char glass_wake_model_sha[32]={'+','.join('0x%02x'%x for x in digest)+'};\n'
    (a.output/'glass_wake_model.h').write_text(auth,encoding='utf-8')
    print('CUSTOM_WAKE_TRAINED',digest.hex(),len(data),flush=True)

if __name__=='__main__':main()
