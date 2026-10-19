#!/usr/bin/env python3
"""Create an explicit high-quality JPEG playback profile for bandwidth-limited SD."""
import argparse,hashlib,io,json,math,struct,zlib
from pathlib import Path
from PIL import Image,ImageChops
from render_jizura_banks import HEADER,unpack
def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--subsampling',type=int,choices=[0,2],default=2);p.add_argument('--scale',type=int,choices=[1,2],default=1);a=p.parse_args()
    manifest=json.loads((a.source/'manifest.json').read_text(encoding='utf-8'));assert manifest['complete'];a.output.mkdir(parents=True,exist_ok=True)
    if any(a.output.glob('*.jpv')):raise ValueError('Preserve existing output; choose a fresh directory')
    report={**manifest,'files':{},'codec_kind':'jpeg','codec':f'baseline JPEG / subsampling {a.subsampling} / nominal quality 92; compressed Adler-32',
            'lossy':True,'source_pixels':640*360,'decode_width':640//a.scale,'decode_height':360//a.scale,
            'psnr_reference':'resized source' if a.scale==2 else 'source','minimum_psnr_db':100,'minimum_quality':92,'maximum_frame_bytes':0}
    for name,meta in manifest['files'].items():
        file=a.source/name;assert hashlib.sha256(file.read_bytes()).hexdigest()==meta['sha256']
        with file.open('rb') as f,(a.output/name).open('w+b') as out:
            header=f.read(HEADER);count=struct.unpack_from('<I',header,24)[0];table=f.read(count*12);out.write(header);out.write(bytes(count*12));entries=[]
            for i in range(count):
                offset,encoded,adler=struct.unpack_from('<III',table,i*12);f.seek(offset);raw=unpack(f.read(encoded&0x3fffffff),encoded>>30==2);assert zlib.adler32(raw)==adler
                image=Image.frombytes('RGB',(640,360),raw,'raw','BGR;16')
                if a.scale==2:image=image.resize((320,180),Image.Resampling.LANCZOS)
                for sampling in ([2,0] if a.subsampling==2 else [0]):
                    for quality in [92,90,88,85,80]:
                        stream=io.BytesIO();image.save(stream,'JPEG',quality=quality,subsampling=sampling,optimize=True,progressive=False);payload=stream.getvalue()
                        if len(payload)<=131072:break
                    assert len(payload)<=131072
                    diff=ImageChops.difference(image,Image.open(io.BytesIO(payload)));hist=diff.histogram();mse=sum((i%256)**2*n for i,n in enumerate(hist))/(image.width*image.height*3)
                    psnr=10*math.log10(255**2/mse) if mse else 100
                    if psnr>=30:break
                assert psnr>=30,(name,i,psnr)
                report['minimum_psnr_db']=min(report['minimum_psnr_db'],psnr);report['minimum_quality']=min(report['minimum_quality'],quality);report['maximum_frame_bytes']=max(report['maximum_frame_bytes'],len(payload))
                entries.append(struct.pack('<III',out.tell(),len(payload)|0xc0000000,zlib.adler32(payload)));out.write(payload)
            out.seek(HEADER);out.write(b''.join(entries))
        dest=a.output/name;report['files'][name]={'bytes':dest.stat().st_size,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
        print('JPEG_PACKED',name,dest.stat().st_size,flush=True)
    report['packed_bytes']=sum(x['bytes'] for x in report['files'].values());report['minimum_psnr_db']=round(report['minimum_psnr_db'],3)
    (a.output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print('JPEG_BANKS_READY',report['packed_bytes'],report['minimum_psnr_db'],report['maximum_frame_bytes'])
if __name__=='__main__':main()
