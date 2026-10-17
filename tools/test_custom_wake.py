#!/usr/bin/env python3
"""Test the trained model through the exact streaming C detector used on board."""
import argparse,ctypes,hashlib,json,shutil,subprocess
from pathlib import Path
import numpy as np
from wake_streaming import stage_sources

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--corpus',type=Path,required=True);p.add_argument('--model',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    trained=json.loads((a.model/'training.json').read_text(encoding='utf-8'))
    assert trained['synthetic_gate_passed'] and trained['model_sha256']==hashlib.sha256((a.model/'nihao_openvela.gkw').read_bytes()).hexdigest()
    repo=Path(__file__).resolve().parents[1];d=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
    root=a.output/'runtime';(root/'custom').mkdir(parents=True,exist_ok=True);(root/'test').mkdir(exist_ok=True)
    shutil.copy2(a.model/'nihao_openvela.gkw',root/'custom/nihao_openvela.gkw')
    rows=json.loads((a.corpus/'corpus.json').read_text(encoding='utf-8'))['records']
    positive=next(r for r in rows if r['split']=='test' and r['label']==1 and r['rate']=='+0%')
    voice=positive['speaker'];negative=[next(r for r in rows if r['speaker']==voice and r['text']==text) for text in ['你好','OpenVela','你好小爱']]
    silence=bytes(32000)
    pos=root/'test/wake-positive.pcm';pos.write_bytes((a.corpus/positive['pcm']).read_bytes()+silence)
    neg=root/'test/wake-negative.pcm';neg.write_bytes(b''.join((a.corpus/r['pcm']).read_bytes()+silence for r in negative))
    corrupt=a.output/'corrupt';(corrupt/'custom').mkdir(parents=True,exist_ok=True)
    data=bytearray((root/'custom/nihao_openvela.gkw').read_bytes());data[-5]^=1;(corrupt/'custom/nihao_openvela.gkw').write_bytes(data)
    # Quoted includes prefer the C source directory. Stage the candidate header
    # alongside the source so a stale repository SHA cannot validate another model.
    staged=a.output/'sources'
    sources=stage_sources(d,staged,a.model/'glass_wake_model.h')
    includes=['-I'+str(staged),'-I'+str(d)]
    binary=a.output/'custom-wake-host'
    subprocess.run(['gcc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*includes,str(repo/'app/espdl-quickapp/tests/custom_wake_host.c'),*map(str,sources),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary),str(root),str(pos),str(neg),str(corrupt)],check=True)
    library=a.output/'streaming.so'
    subprocess.run(['gcc','-shared','-fPIC','-O3',*includes,*map(str,sources),'-lm','-o',str(library)],check=True)
    lib=ctypes.CDLL(str(library));ptr=ctypes.c_void_p;sp=ctypes.POINTER(ctypes.c_int16)
    lib.glass_custom_wake_create.argtypes=[ctypes.c_char_p,ctypes.POINTER(ptr)]
    lib.glass_custom_wake_feed.argtypes=[ptr,sp,ctypes.c_size_t]
    lib.glass_custom_wake_destroy.argtypes=[ptr]
    counts={1:[0,0],0:[0,0]}
    failures=[]
    for r in [r for r in rows if r['split']=='test']:
        state=ptr();assert lib.glass_custom_wake_create(str(root).encode(),ctypes.byref(state))==0
        pcm=np.frombuffer((a.corpus/r['pcm']).read_bytes()+bytes(32000),dtype=np.int16).copy();events=0
        for offset in range(0,len(pcm),1023):
            chunk=pcm[offset:offset+1023];ret=lib.glass_custom_wake_feed(state,chunk.ctypes.data_as(sp),len(chunk));assert ret>=0;events+=ret
        lib.glass_custom_wake_destroy(state)
        counts[r['label']][0]+=(events==1 if r['label'] else events>0);counts[r['label']][1]+=1
        if (events==1)!=(r['label']==1):failures.append({'pcm':r['pcm'],'text':r['text'],'events':events,'label':r['label']})
    report={'synthetic_only':True,'test_positive_detected':counts[1][0],'test_positive':counts[1][1],
      'test_false_positive':counts[0][0],'test_negative':counts[0][1],'failures':failures,'fixture_speaker':voice}
    (a.output/'streaming.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('CUSTOM_WAKE_STREAM_TEST',report['test_positive_detected'],report['test_positive'],report['test_false_positive'],report['test_negative'])
    assert counts[1][0]/counts[1][1]>=.85 and counts[0][0]/counts[0][1]<=.01,'Streaming gate failed; do not deploy'

if __name__=='__main__':main()
