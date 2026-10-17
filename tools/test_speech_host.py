#!/usr/bin/env python3
"""Offline ASan/UBSan Chinese inference and adapter tests; no board access."""
import argparse
from pathlib import Path
import subprocess
from build_speech_runtime import prepare_compute_sources

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--apps',type=Path,required=True)
p.add_argument('--prepared',type=Path,required=True)
p.add_argument('--quickjs-cache',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--g2p-vectors',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
v=a.prepared/'vendor';q=a.apps/'interpreters/quickjs/quickjs'
a.output.mkdir(exist_ok=True,parents=True)
flags=['gcc','-O2','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCONFIG_BIGNUM',
       '-I'+str(desktop),'-I'+str(v),'-I'+str(q),'-I'+str(a.apps/'system/desktop')]
objects=[a.quickjs_cache/(n+'.o') for n in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
compute=prepare_compute_sources(v,a.output/'compute-sources')
# Compile the pinned original independently, so parity is not a comparison
# against another copy of the optimized implementation.
original=(v/'snt_piperlite.c').read_text()
start=original.index('static void pl_conv1d(')
end=original.index('\n}\n',start)+3
reference=a.output/'reference-conv.c'
reference.write_text(original[start:end].replace('static void pl_conv1d(', 'void reference_conv(',1))
subprocess.run([*flags,'-O3',str(repo/'app/espdl-quickapp/tests/speech_conv_host.c'),str(reference),str(desktop/'glass_speech_compute.c'),'-pthread','-o',str(a.output/'speech-conv-host')],check=True)
subprocess.run([str(a.output/'speech-conv-host')],check=True,timeout=60)
math_objects=[]
for name,source in compute.items():
    obj=a.output/(name+'.o')
    subprocess.run([*flags,'-O3','-c',str(source),'-o',str(obj)],check=True)
    math_objects.append(obj)
sources=[repo/'app/espdl-quickapp/tests/speech_tts_host.c',desktop/'qpk_espdl_sha256.c',desktop/'glass_speech_compute.c',*math_objects]
subprocess.run([*flags,*map(str,sources),*map(str,objects),'-lm','-ldl','-pthread','-o',str(a.output/'sano-zh-host')],check=True)
subprocess.run([str(a.output/'sano-zh-host'),str(a.prepared/'sdcard/speech'),str(a.output/'chinese.pcm'),str(a.g2p_vectors)],check=True,timeout=180)
subprocess.run(['gcc','-O2','-g','-fsanitize=address,undefined','-I'+str(desktop),str(repo/'app/espdl-quickapp/tests/speech_compute_host.c'),str(desktop/'glass_speech_compute.c'),'-pthread','-o',str(a.output/'speech-compute-host')],check=True)
subprocess.run([str(a.output/'speech-compute-host')],check=True,timeout=30)

subprocess.run(['gcc','-O1','-g','-fsanitize=address,undefined','-I'+str(desktop),'-I'+str(desktop/'speech_port'),'-I'+str(v),'-I'+str(a.apps/'netutils/cjson/cJSON'),
    str(repo/'app/espdl-quickapp/tests/speech_sr_host.c'),'-o',str(a.output/'sr-adapter-host')],check=True)
subprocess.run([str(a.output/'sr-adapter-host'),str(repo/'app/espdl-quickapp/speech/commands_cn.txt')],check=True,timeout=30)

subprocess.run(['gcc','-O1','-g','-fsanitize=address,undefined','-I'+str(desktop),str(repo/'app/espdl-quickapp/tests/speech_port_host.c'),'-lm','-pthread','-o',str(a.output/'speech-port-host')],check=True)
subprocess.run([str(a.output/'speech-port-host')],check=True,timeout=30)

import tempfile
with tempfile.TemporaryDirectory(prefix='speech-corrupt-') as tmp:
    bad=Path(tmp)/'sanotts-zh';bad.mkdir()
    content=bytearray((a.prepared/'sdcard/speech/sanotts-zh/phonemes.json').read_bytes());content[0]^=1
    (bad/'phonemes.json').write_bytes(content)
    subprocess.run([*flags,str(repo/'app/espdl-quickapp/tests/speech_assets_host.c'),str(desktop/'qpk_espdl_sha256.c'),'-o',str(a.output/'speech-assets-host')],check=True)
    subprocess.run([str(a.output/'speech-assets-host'),str(a.prepared/'sdcard/speech'),tmp],check=True,timeout=30)
