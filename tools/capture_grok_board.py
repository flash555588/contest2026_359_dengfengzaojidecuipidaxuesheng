#!/usr/bin/env python3
"""Real board framebuffer capture through native immutable RLE snapshot."""
import argparse,json,re,struct,hashlib,zlib,time
from pathlib import Path
from grok_board_io import Console
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='COM4');p.add_argument('--output',type=Path,required=True);p.add_argument('--name',required=True);p.add_argument('--command',action='append',default=[])
    p.add_argument('--resume-export',action='store_true',help='Repair saved incomplete USB records without taking a new snapshot')
    p.add_argument('--settle',type=float,default=.5)
    a=p.parse_args();a.output.mkdir(exist_ok=True,parents=True)
    logfile=a.output/(a.name+'.log');saved=logfile.read_text(encoding='utf-8') if a.resume_export else None
    with logfile.open('a' if a.resume_export else 'w',encoding='utf-8') as log:
        usb=Console(a.port,log);usb.command('')
        if a.resume_export:reply=saved
        else:
            for cmd in a.command:usb.command(cmd)
            time.sleep(a.settle)
            usb.command('desktop ui frame');reply=usb.command('desktop frame-export',timeout=240)
        length=int(re.search(r'FRAME_DATA (\d+)',reply)[1]);data=bytearray(length);received=set()
        def collect(text):
            for row in re.finditer(r'^F([a-f0-9]{8}):([a-f0-9]+)$',text,re.M):
                if len(row[2])%2:continue
                off=int(row[1],16);raw=bytes.fromhex(row[2]);expected=min(64,length-off)
                if 0<=off<length and off%64==0 and len(raw)==expected:data[off:off+len(raw)]=raw;received.add(off)
        collect(reply)
        for off in range(0,length,64):
            for retry in range(3):
                if off in received:break
                collect(usb.command('desktop frame-chunk '+str(off)))
            if off not in received:raise RuntimeError('Snapshot record missing: '+str(off))
        usb.command('desktop ui frame-free');usb.close()
    pixels=bytearray()
    for count,color in struct.iter_unpack('<HH',data):pixels.extend(struct.pack('<H',color)*count)
    assert len(pixels)==1024*600*2
    rgb=bytearray()
    for color, in struct.iter_unpack('<H',pixels):rgb.extend(((color>>11)*255//31,((color>>5)&63)*255//63,(color&31)*255//31))
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    rows=b''.join(b'\0'+rgb[y*3072:(y+1)*3072] for y in range(600))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1024,600,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows,6))+chunk(b'IEND',b'')
    (a.output/(a.name+'.png')).write_bytes(png)
    (a.output/(a.name+'.json')).write_text(json.dumps({'method':'actual board framebuffer / lossless RLE / USB console','rle_bytes':length,'rgb565_sha256':hashlib.sha256(pixels).hexdigest()},indent=2))
    print('BOARD_CAPTURE_OK',a.output/(a.name+'.png'))
if __name__=='__main__':main()
