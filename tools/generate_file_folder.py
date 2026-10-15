#!/usr/bin/env python3
"""Generate an authored yellow folder as one immutable LVGL RGB565+A8 asset."""
from pathlib import Path
import argparse,struct,zlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--preview',type=Path);a=p.parse_args()
w,h,ss=40,32,4

def rounded(x,y,left,top,right,bottom,r):
    if x<left or x>=right or y<top or y>=bottom:return False
    cx=max(left+r,min(right-r,x));cy=max(top+r,min(bottom-r,y))
    return (x-cx)**2+(y-cy)**2<=r*r

def sample(x,y):
    rgba=(0,0,0,0)
    if rounded(x,y,1,12,39,31,4):rgba=(118,76,0,24)
    if rounded(x,y,2,2,18,15,3):rgba=(245,185,50,255)
    if rounded(x,y,2,6,38,29,3):rgba=(235,169,35,255)
    if rounded(x,y,5,8,35,22,2):rgba=(255,231,156,255)
    if rounded(x,y,0,11,40,30,4):
        t=(y-11)/19;rgba=(255,round(220-26*t),round(106-50*t),255)
        if y<11.7:rgba=(255,229,139,255)
    return rgba
pixels=[]
for y in range(h):
 for x in range(w):
    samples=[sample(x+(sx+.5)/ss,y+(sy+.5)/ss) for sy in range(ss) for sx in range(ss)]
    alpha=sum(v[3] for v in samples)
    rgb=[round(sum(v[c]*v[3] for v in samples)/alpha) if alpha else 0 for c in range(3)]
    pixels.append((*rgb,round(alpha/(ss*ss))))
packed=b''.join(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b,alpha in pixels)+bytes(v[3] for v in pixels)
lines=['/* SPDX-License-Identifier: Apache-2.0 */','/* Authored by tools/generate_file_folder.py; shared flash asset, no pixel heap. */','static const unsigned char files_folder_pixels[] = {']
lines += ['  '+','.join('0x%02x'%b for b in packed[i:i+24])+',' for i in range(0,len(packed),24)]
lines += ['};','static const lv_image_dsc_t files_folder_image = {','  .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565A8,','           .w=40,.h=32,.stride=80},','  .data_size=sizeof(files_folder_pixels),.data=files_folder_pixels','};','']
root=Path(__file__).resolve().parents[1]
(root/'app/espdl-quickapp/overlay/apps/system/desktop/glass_file_folder.inc').write_bytes('\n'.join(lines).encode())
if a.preview:
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
 raw=b''.join(b'\0'+bytes(channel for px in pixels[y*w:(y+1)*w] for channel in px) for y in range(h))
 a.preview.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
print('Yellow folder asset:',len(packed),'bytes; one shared immutable image')
