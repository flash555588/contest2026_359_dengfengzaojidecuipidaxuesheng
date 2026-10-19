#!/usr/bin/env python3
"""Exercise the production SD decoder against damaged inputs and real renders."""
import argparse,ctypes,ctypes.util,json,re,struct,subprocess,zlib
from pathlib import Path
from render_jizura_banks import HEADER,track_key
def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--banks',type=Path);a=p.parse_args()
    repo=Path(__file__).resolve().parents[1];d=repo/'app/espdl-quickapp/overlay/apps/system/desktop';a.output.mkdir(parents=True,exist_ok=True)
    exe=a.output/'pv-bank-host';subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(d),str(repo/'app/espdl-quickapp/tests/music_pv_bank_host.c'),str(d/'qpk_mjpeg.c'),str(d/'qpk_tjpgd.c'),'-o',str(exe)],check=True)
    raw=struct.pack('<H',0x4312)*(640*360);header=bytearray(HEADER)
    struct.pack_into('<8s7I',header,0,b'JZPV01\0\0',1,640,360,12,1,83,0)
    for at,s in [(36,'慢下来的光'),(164,'原创演示')]:header[at:at+len(s.encode())]=s.encode()
    table=struct.pack('<III',HEADER+12,len(raw)|0x80000000,zlib.adler32(raw));valid=header+table+raw
    cases={'raw':(valid,True)}
    rle=b''.join(struct.pack('<HH',32768|(n-1),0x4312) for n in [32768]*7+[1024])
    cases['rle']=(header+struct.pack('<III',HEADER+12,len(rle),zlib.adler32(raw))+rle,True)
    for label,at,value in [('magic',0,255),('version',8,9),('width',12,1),('height',16,1),('fps-zero',20,0),('fps-big',20,25),('count-zero',24,0),('count-big',26,1),('duration',28,82),('style',32,1),('name',36,0),('artist',164,0),('checksum',HEADER+8,0)]:
        bad=bytearray(valid);bad[at]=value;cases[label]=(bad,False)
    for label,at,value in [('offset-header',HEADER,4),('offset-overflow',HEADER,0xfffffffe),('length-large',HEADER+4,0x7fffffff),('raw-short',HEADER+4,0x80000002)]:
        bad=bytearray(valid);struct.pack_into('<I',bad,at,value);cases[label]=(bad,False)
    for cut in [0,100,HEADER+3,len(valid)-1]:cases['truncated-'+str(cut)]=(valid[:cut],False)
    bad=bytearray(cases['rle'][0]);bad[-2]^=1;cases['damaged-rle']=(bad,False)
    for label,payload in [('run-overflow',struct.pack('<HH',65535,1)*8),('literal-short',b'\xff\x7f\0\0'),('trailing',rle+b'\0\0')]:
        cases[label]=(header+struct.pack('<III',HEADER+12,len(payload),zlib.adler32(raw))+payload,False)
    lib=ctypes.CDLL(ctypes.util.find_library('lz4') or 'liblz4.so.1');target=ctypes.create_string_buffer(len(raw)+4096)
    length=lib.LZ4_compress_default(raw,target,len(raw),len(target));assert length>0
    for label,payload,ok in [('lz4',target.raw[:length],True),('lz4-zero-offset',b'\x10\x01\0\0',False),('lz4-back-before-start',b'\x00\x01\0',False),('lz4-long-literal',b'\xf0'+b'\xff'*1810,False),('lz4-short',b'\xff',False),('lz4-truncated',target.raw[:length-1],False),('lz4-tail',target.raw[:length]+b'\0\0',False)]:
        cases[label]=(header+struct.pack('<III',HEADER+12,len(payload)|0x40000000,zlib.adler32(raw))+payload,ok)
    indexed=bytearray(header);indexed[260:264]=b'IDX2'
    palette=[struct.pack('<H',i*257) for i in range(256)];indices=bytes(range(256))*225
    pixels=b''.join(b''.join(palette[v]*2 for v in indices[y*320:(y+1)*320])*2 for y in range(180))
    packed=b''.join(palette)+indices
    length=lib.LZ4_compress_default(packed,target,len(packed),len(target));assert length>0
    for label,payload,codec,ok in [('indexed-raw',packed,2,True),('indexed-lz4',target.raw[:length],1,True),('indexed-short',packed[:-1],2,False),('indexed-rle',packed,0,False),('indexed-jpeg',packed,3,False),('indexed-lz4-tail',target.raw[:length]+b'\0\0',1,False)]:
        cases[label]=(indexed+struct.pack('<III',HEADER+12,len(payload)|(codec<<30),zlib.adler32(packed))+payload,ok)
    bad=bytearray(cases['indexed-raw'][0]);bad[-1]^=1;cases['indexed-damaged']=(bad,False)
    bad=bytearray(cases['indexed-raw'][0]);bad[260]=ord('X');cases['indexed-unknown-format']=(bad,False)
    for name,(data,ok) in cases.items():
        path=a.output/(name+'.jpv');path.write_bytes(data);output=subprocess.check_output([str(exe),str(path),'0',str(int(ok))],text=True)
        if name in ('indexed-raw','indexed-lz4'):assert f'DECODED 0 0 {zlib.adler32(pixels)}' in output,'Indexed expansion differs from independent reference'
    checked=0;decoded={}
    if a.banks:
        manifest=json.loads((a.banks/'manifest.json').read_text());assert manifest['complete']
        for name in manifest['files']:
            style=int(Path(name).stem.split('-')[1]);output=subprocess.check_output([str(exe),str(a.banks/name),str(style),'1'],text=True)
            decoded[name]=[int(m[3]) for m in re.finditer(r'DECODED (\d+) (\d+) (\d+)',output)];assert len(decoded[name])==manifest['frames_per_style'];checked+=len(decoded[name])
    (a.output/'decoded-adlers.json').write_text(json.dumps(decoded))
    result=dict(pass_=True,malformed_and_valid_cases=len(cases),rendered_frames=checked,buffer_bytes=2*640*360*2)
    (a.output/'verification.json').write_text(json.dumps(result,indent=2));print('MUSIC_PV_BANKS_OK',result)
if __name__=='__main__':main()
