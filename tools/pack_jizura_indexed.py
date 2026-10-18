#!/usr/bin/env python3
"""Create a bounded 320x180 palette profile; compress with pack_jizura_banks.py."""
import argparse,hashlib,json,math,struct,zlib
from pathlib import Path
from PIL import Image,ImageChops
from render_jizura_banks import HEADER,unpack

def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    manifest=json.loads((a.source/'manifest.json').read_text(encoding='utf-8'));assert manifest['complete']
    a.output.mkdir(parents=True,exist_ok=True)
    if any(a.output.glob('*.jpv')):raise ValueError('Preserve existing assets; use a fresh directory')
    report={**manifest,'files':{},'codec_kind':'indexed8','codec':'IDX2 palette + raw indices','lossy':True,
            'decode_width':320,'decode_height':180,'psnr_reference':'resized source','minimum_psnr_db':100}
    for name,meta in manifest['files'].items():
        source=a.source/name;assert hashlib.sha256(source.read_bytes()).hexdigest()==meta['sha256']
        with source.open('rb') as f,(a.output/name).open('w+b') as out:
            header=bytearray(f.read(HEADER));count=struct.unpack_from('<I',header,24)[0];table=f.read(count*12)
            header[260:264]=b'IDX2';out.write(header);out.write(bytes(count*12));entries=[]
            for i in range(count):
                offset,encoded,adler=struct.unpack_from('<III',table,i*12);f.seek(offset)
                raw=unpack(f.read(encoded&0x3fffffff),encoded>>30==2);assert zlib.adler32(raw)==adler
                image=Image.frombytes('RGB',(640,360),raw,'raw','BGR;16').resize((320,180),Image.Resampling.LANCZOS)
                indexed=image.quantize(colors=256,method=Image.Quantize.FASTOCTREE,dither=Image.Dither.NONE)
                palette=indexed.getpalette();palette=palette+[0]*(768-len(palette))
                colors=[(palette[j]>>3)<<11|(palette[j+1]>>2)<<5|(palette[j+2]>>3) for j in range(0,768,3)]
                indices=indexed.tobytes();payload=struct.pack('<256H',*colors)+indices
                reference=b''.join(struct.pack('<H',colors[v]) for v in indices)
                decoded=Image.frombytes('RGB',(320,180),reference,'raw','BGR;16')
                hist=ImageChops.difference(image,decoded).histogram();mse=sum((j%256)**2*n for j,n in enumerate(hist))/(320*180*3)
                psnr=10*math.log10(255**2/mse) if mse else 100
                assert psnr>=30,(name,i,psnr)
                report['minimum_psnr_db']=min(report['minimum_psnr_db'],psnr)
                entries.append(struct.pack('<III',out.tell(),len(payload)|0x80000000,zlib.adler32(payload)));out.write(payload)
            out.seek(HEADER);out.write(b''.join(entries))
        dest=a.output/name;report['files'][name]={'bytes':dest.stat().st_size,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
        print('INDEXED_PACKED',name,dest.stat().st_size,flush=True)
    report['minimum_psnr_db']=round(report['minimum_psnr_db'],3)
    (a.output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('INDEXED_READY',report['minimum_psnr_db'],flush=True)

if __name__=='__main__':main()
