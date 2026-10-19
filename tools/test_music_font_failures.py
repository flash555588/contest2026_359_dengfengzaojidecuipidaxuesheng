#!/usr/bin/env python3
"""Exercise the real native font checker using a disposable host font copy."""
import argparse,json,os,subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--renderer',type=Path,required=True)
    p.add_argument('--font-copy',type=Path,required=True);p.add_argument('--base',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    assert a.font_copy.name=='test-font.ttf','Only a disposable fixture may be changed'
    data=a.font_copy.read_bytes();a.output.mkdir(parents=True,exist_ok=True)
    report={};env=dict(os.environ,MUSIC_FONT_EXPECT_FAILURE='1')
    try:
        for case in ['truncated','checksum','missing']:
            if case=='truncated':a.font_copy.write_bytes(data[:4])
            elif case=='checksum':a.font_copy.write_bytes(data[:-1]+bytes([data[-1]^1]))
            else:a.font_copy.unlink()
            result=subprocess.run([str(a.renderer.resolve()),str(a.base.resolve()),str(a.output.resolve())],
                                  env=env,capture_output=True,text=True,timeout=35)
            (a.output/(case+'.log')).write_text(result.stdout+result.stderr)
            assert result.returncode==0 and 'MUSIC_FONT_REJECTED' in result.stdout,result.stderr
            report[case]=True
    finally:a.font_copy.write_bytes(data)
    (a.output/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('MUSIC_FONT_FAILURES_OK',','.join(report))

if __name__=='__main__':main()
