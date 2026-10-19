#!/usr/bin/env python3
"""Losslessly repack browser RGB565 banks with system LZ4 for SD bandwidth."""
import argparse,ctypes,ctypes.util,hashlib,json,struct,zlib
from pathlib import Path
from render_jizura_banks import HEADER,unpack
def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    lib=ctypes.CDLL(ctypes.util.find_library('lz4') or 'liblz4.so.1')
    lib.LZ4_compress_default.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    lib.LZ4_decompress_safe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
    manifest=json.loads((a.source/'manifest.json').read_text());assert manifest['complete'];a.output.mkdir(parents=True,exist_ok=True)
    if any(a.output.glob('*.jpv')):raise ValueError('Use a fresh output directory')
    report={**manifest,'files':{},'codec':'RGB565 RLE16 / LZ4 / raw (smallest per frame)','decoded_frames_equal':0}
    dest=ctypes.create_string_buffer(640*360*2+4096);check=ctypes.create_string_buffer(640*360*2)
    total_before=total_after=0
    for name,meta in manifest['files'].items():
        source=a.source/name;assert hashlib.sha256(source.read_bytes()).hexdigest()==meta['sha256']
        with source.open('rb') as f,(a.output/name).open('w+b') as out:
            header=f.read(HEADER);count=struct.unpack_from('<I',header,24)[0];index=f.read(count*12)
            out.write(header);out.write(bytes(count*12));entries=[]
            for i in range(count):
                offset,encoded,adler=struct.unpack_from('<III',index,i*12);f.seek(offset);original=f.read(encoded&0x3fffffff)
                if header[260:264]==b'IDX2':
                    assert encoded>>30==2 and len(original)==512+320*180;raw=original
                else:raw=unpack(original,encoded>>30==2)
                assert zlib.adler32(raw)==adler
                n=lib.LZ4_compress_default(raw,dest,len(raw),len(dest));assert n>0
                compressed=dest.raw[:n];assert lib.LZ4_decompress_safe(compressed,check,n,len(raw))==len(raw) and check.raw[:len(raw)]==raw
                payload,codec=(compressed,1) if n<len(original) else (original,encoded>>30)
                entries.append(struct.pack('<III',out.tell(),len(payload)|(codec<<30),adler));out.write(payload);report['decoded_frames_equal']+=1
            out.seek(HEADER);out.write(b''.join(entries))
        file=a.output/name;size=file.stat().st_size;total_before+=source.stat().st_size;total_after+=size
        report['files'][name]={'bytes':size,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()}
        print('PACKED',name,size,flush=True)
    report.update(source_bytes=total_before,packed_bytes=total_after)
    if manifest.get('codec_kind')=='indexed8':report['codec']='IDX2 palette + LZ4 / raw (smallest per frame)'
    (a.output/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print('JIZURA_PACKED',total_before,total_after,report['decoded_frames_equal'])
if __name__=='__main__':main()
