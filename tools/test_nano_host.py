#!/usr/bin/env python3
"""Check official nano against its RAM adaptation under ASan/UBSan."""
import argparse
from pathlib import Path
import re
import subprocess
from build_speech_runtime import prepare_nano_storage

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prepared',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.prepared=a.prepared.resolve();a.output=a.output.resolve()
a.output.mkdir(parents=True,exist_ok=True)
desktop=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop'
main=a.output/'main.c'
main.write_text('''#include "glass_nano_probe.h"
#include <assert.h>
#include <errno.h>
static int calls;
static bool cancel(void *p){(void)p;return ++calls==150;}
int main(void){assert(!glass_nano_probe(0,0,0));
assert(glass_nano_probe(0,cancel,0)==-ECANCELED);
assert(!glass_nano_probe(0,0,0));return 0;}
''',encoding='utf-8')
stub=a.output/'reference_storage.c'
stub.write_text('#include <stddef.h>\nsize_t glass_nano_storage_size(void){return 16;}\nvoid glass_nano_storage_bind(void *p){(void)p;}\n',encoding='utf-8')
adapted=prepare_nano_storage(a.prepared/'vendor/snt_nano.c',a.output/'snt_nano_storage.c')
hashes=[]
for label,sources in [('reference',[a.prepared/'vendor/snt_nano.c',stub]),('dynamic',[adapted])]:
    binary=a.output/label
    subprocess.run(['gcc','-O3','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(desktop),'-I'+str(a.prepared/'vendor'),'-I'+str(a.prepared/'generated'),
                    str(main),*map(str,sources),str(desktop/'glass_nano_probe.c'),
                    str(desktop/'glass_speech_compute.c'),str(desktop/'qpk_espdl_sha256.c'),
                    str(a.prepared/'generated/nano_blobs.S'),'-lm','-lpthread','-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],cwd=a.output,capture_output=True,text=True,check=True,timeout=60)
    (a.output/(label+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
    success=re.findall(r'backend=scalar result=0 [^\n]*\n\[nano\] pcm_sha256=([0-9a-f]{64})',result.stdout)
    assert len(success)==2 and 'result=-125' in result.stdout,result.stdout
    hashes+=success
assert len(set(hashes))==1,hashes
print('NANO_HOST_OK: official/reference and dynamic scratch PCM bit-exact; cancel/reopen; ASan/UBSan',hashes[0])
