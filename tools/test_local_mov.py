#!/usr/bin/env python3
"""Build current local media sources and decode an actual H.264/AAC MOV on host."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--apps',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
src=repo/'app/bilibili_live'; desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
codec=a.apps.resolve()/'system/bilibili_live/vendor/openh264/codec'
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
inc=[src,src/'local_vendor',src/'local_vendor/helix',desktop,*[codec/d for d in ['api/wels','common/inc','decoder/core/inc','decoder/plus/inc']]]
flags=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-DUSE_DEFAULT_STDLIB',*['-I'+str(d) for d in inc]]
sources=[s for d in ['common/src','decoder/core/src','decoder/plus/src'] for s in sorted((codec/d).glob('*.cpp'))]
sources += [s for s in sorted((src/'local_vendor/helix').glob('*.c')) if not s.name.startswith('sbr')]
sources += [src/n for n in ['bili_sps.c','bili_cpu.c','bili_decoder.cpp','local_video_media.c','tests/test_local_video.c']]
sources += [desktop/'qpk_mjpeg.c',desktop/'qpk_tjpgd.c']
def run(args,**kwargs):return subprocess.run(list(map(str,args)),check=True,**kwargs)
def compile_file(source):
    obj=out/(source.stem+'-'+hashlib.sha256(str(source).encode()).hexdigest()[:10]+'.o')
    run(['g++' if source.suffix=='.cpp' else 'cc',*flags,'-c',source,'-o',obj])
    return obj
with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_file,sources))
run(['g++','-pthread','-fsanitize=address,undefined',*objects,'-lm','-o',out/'test_mov'])
run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i','testsrc2=size=320x180:rate=25','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','3','-c:v','libx264','-pix_fmt','yuv420p','-threads','1','-c:a','aac','-ac','2','-movflags','+faststart',out/'fixture.mov'])
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'}
# Helix's pre-existing signed-shift diagnostics are reported with UBSan recovery.
run([out/'test_mov',out/'fixture.mov'],env=env,timeout=40)
run(['ffmpeg','-hide_banner','-loglevel','error','-y','-i',out/'fixture.mov','-c','copy',out/'fixture.mp4'])
run([out/'test_mov',out/'fixture.mp4'],env=env,timeout=40)
metadata={'test':'current local media decoder, real H264/AAC MOV, three full repeats, late-frame handling, pause/resume/cancel','board_tested':False,'sources':{str(s):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources},'flags':flags,'fixture_sha256':hashlib.sha256((out/'fixture.mov').read_bytes()).hexdigest()}
(out/'VERIFICATION.json').write_text(json.dumps(metadata,indent=2)+'\n')
print('MOV_HOST_OK: actual H.264/AAC MOV and MP4 decoded; no hardware playback performed',flush=True)
