#!/usr/bin/env python3
"""Static dependency inventory and transparent RAM arithmetic; not an FPS test."""
from collections import Counter
from pathlib import Path
import argparse,json,math,re

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();repo=Path(__file__).resolve().parents[1]
root=repo/'app/jizura/upstream';out=args.output;out.mkdir(parents=True,exist_ok=True)
methods=set('save restore reset resetTransform translate rotate scale transform setTransform beginPath closePath moveTo lineTo bezierCurveTo quadraticCurveTo arc arcTo ellipse rect roundRect clip fill stroke fillRect strokeRect clearRect fillText strokeText measureText drawImage createImageData getImageData putImageData createPattern createLinearGradient createRadialGradient setLineDash getLineDash isPointInPath isPointInStroke'.split())
calls=Counter();locations={};modes=set();filters=set()
for file in sorted((root/'src').glob('*.js')):
    text=file.read_text(encoding='utf-8')
    for method in methods:
        count=len(re.findall(r'\.'+method+r'\s*\(',text))
        if count:calls[method]+=count;locations.setdefault(method,[]).append(file.relative_to(root).as_posix())
    for mode in ['source-over','source-in','source-out','source-atop','destination-over','destination-in','destination-out','destination-atop','lighter','copy','xor','multiply','screen','overlay','darken','lighten','color-dodge','color-burn','hard-light','soft-light','difference','exclusion','hue','saturation','color','luminosity']:
        if re.search(r'[\'\"]'+re.escape(mode)+r'[\'\"]',text):modes.add(mode)
    for filter_ in ['blur','invert','grayscale','brightness','contrast','saturate','sepia','opacity','hue-rotate','drop-shadow']:
        if filter_+'(' in text:filters.add(filter_)
buffers=[]
for w,h in [(512,288),(640,360),(800,450),(1024,576),(1024,600),(1920,1080)]:
    rgba=w*h*4
    buffers.append(dict(width=w,height=h,rgba_bytes=rgba,rgb565_bytes=w*h*2,
      rgba_surfaces_2=rgba*2,rgba_surfaces_4=rgba*4,rgba_surfaces_6=rgba*6,
      one_rgba_read_write_mib_per_s_30fps=rgba*2*30/1048576))
glyphs=[]
for res in [128,256,512,768]:
    side=math.ceil(res*1.45);pixels=side*side
    glyphs.append(dict(resolution=res,side=side,js_arrays_bytes=pixels*17,
        with_one_rgba_canvas_bytes=pixels*21,
        note='RGBA readback + alpha + labels + flood stack + one label copy; excludes sprite cache, VM and object overhead'))
report=dict(commit=json.loads((repo/'app/jizura/upstream.lock.json').read_text())['commit'],
  source_methods=dict(sorted(calls.items())),method_files=locations,
  blend_literals=sorted(modes),filter_literals=sorted(filters),
  buffer_scenarios=buffers,glyph_working_set=glyphs,
  renderer_grain_rgba_bytes=4*256*256*4,
  estimates_only=True,render_benchmark_on_board=False)
(out/'static-audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('CANVAS_API_INVENTORY',len(calls),'methods;',len(modes),'blend literals')
for item in glyphs:print('GLYPH',item['resolution'],item['with_one_rgba_canvas_bytes']/1048576,'MiB arithmetic')
