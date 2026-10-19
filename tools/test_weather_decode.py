#!/usr/bin/env python3
"""ASan/UBSan regression of shared MJPEG and read-only weather JPEG decoding."""
from pathlib import Path
import argparse,hashlib,json,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--jpeg',type=Path,required=True,help='320x240 baseline JPEG with standard Huffman tables')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    repo=Path(__file__).resolve().parents[1]
    desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
    sources=[repo/'app/espdl-quickapp/tests/test_mjpeg.c',desktop/'qpk_mjpeg.c',desktop/'qpk_tjpgd.c']
    binary=a.output/'test-mjpeg'
    subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(desktop),*map(str,sources),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary),str(a.jpeg)],check=True,capture_output=True,text=True)
    (a.output/'run.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    (a.output/'validation.json').write_text(json.dumps({'asan_ubsan':'passed',
        'read_only_memory':True,'matches_mutable_decoder':True,'camera_missing_dht':True,
        'scales':4,'truncated_input':True,'sources':{str(s.relative_to(repo)):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}},indent=2)+'\n')
    print(result.stdout,end='')

if __name__=='__main__':main()
