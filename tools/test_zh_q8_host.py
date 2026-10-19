#!/usr/bin/env python3
"""Exercise the bounded Chinese Q8 adapter, cancellation and source parity."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prepared',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.prepared=a.prepared.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
manifest=json.loads((a.prepared/'manifest.json').read_text())
for row in manifest['sources']:
    if hashlib.sha256((a.prepared/row['path']).read_bytes()).hexdigest()!=row['sha256']:
        raise ValueError('Changed Chinese fixture: '+row['path'])
desktop=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop'
main=a.output/'main.c'
main.write_text('''#include "glass_zh_q8.h"
#include <assert.h>
#include <errno.h>
static int calls,limit=4;
static bool cancel(void *p){(void)p;return ++calls>=limit;}
int main(void){assert(!glass_zh_q8_probe(0,0,0));
assert(glass_zh_q8_probe(0,cancel,0)==-ECANCELED);
assert(glass_zh_q8_probe(6,0,0)==-EINVAL);
assert(!glass_zh_q8_probe(2,0,0));
assert(!glass_zh_q8_probe(4,0,0));
#ifdef TEST_ADAPTED
calls=0;limit=1200;assert(glass_zh_q8_probe(4,cancel,0)==-ECANCELED);
#endif
assert(!glass_zh_q8_probe(0,0,0));return 0;}
''',encoding='utf-8')
hashes=[];commands=[]
for label in ['vendor','generated']:
    binary=a.output/label
    command=['gcc','-O3','-g','-std=gnu11','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(desktop),'-I'+str(a.prepared/'vendor'),'-I'+str(a.prepared),str(main),
        str(desktop/'glass_zh_q8.c'),str(desktop/'glass_zh_q8_probe.c'),
        str(desktop/'glass_speech_compute.c'),str(desktop/'qpk_espdl_sha256.c'),
        str(a.prepared/label/'snt_front_q8.c'),str(a.prepared/label/'snt_piperlite_q8.c'),
        str(a.prepared/'blobs.S'),'-lm','-lpthread','-o',str(binary)]
    if label=='generated':command.insert(1,'-DTEST_ADAPTED')
    # Reference kernels have no inserted row checkpoints, so cancellation at
    # scope boundaries uses the same callback but trips at the next stage.
    subprocess.run(command,check=True);commands.append(command)
    result=subprocess.run([str(binary)],cwd=a.output,capture_output=True,text=True,timeout=60)
    (a.output/(label+'.log')).write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(label+' adapter failed: '+result.stdout+result.stderr)
    found=re.findall(r'pcm_sha256=([0-9a-f]{64})',result.stdout)
    if len(found)!=4 or 'result=-125' not in result.stdout:raise RuntimeError('Missing success/cancel evidence: '+result.stdout)
    hashes.extend(found)
if len(set(hashes))!=1:raise RuntimeError('Reference/adapted or reopen PCM differs: '+str(hashes))
(a.output/'verification.json').write_text(json.dumps({'passed':True,'sanitizers':['address','undefined'],
    'reference_and_adapted_bit_exact':True,'cancel_and_reopen':True,'pcm_sha256':hashes[0],
    'commands':commands,'project_source_sha256':{name:hashlib.sha256((desktop/name).read_bytes()).hexdigest()
        for name in ['glass_zh_q8.c','glass_zh_q8.h','glass_zh_q8_probe.c','glass_zh_q8_pie.S','glass_speech_compute.c','glass_speech_compute.h']},
    'fixture_manifest_sha256':hashlib.sha256((a.prepared/'manifest.json').read_bytes()).hexdigest()},indent=2)+'\n')
print('ZH_Q8_HOST_OK',hashes[0])
