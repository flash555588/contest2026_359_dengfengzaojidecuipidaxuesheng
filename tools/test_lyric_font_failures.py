#!/usr/bin/env python3
"""Reject missing/truncated/same-size corrupt fonts in an isolated fixture."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer',type=Path,required=True)
    parser.add_argument('--font-root',type=Path,required=True)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((args.font_root/'manifest.json').read_text(encoding='utf-8'))
    record=manifest[0]
    original=(args.font_root/record['file']).read_bytes()
    results={}
    with tempfile.TemporaryDirectory(prefix='lyric-font-fixture-') as folder:
        root=Path(folder)
        for case in ['missing','truncated','checksum']:
            fixture=root/record['file']
            if case=='truncated':fixture.write_bytes(original[:4])
            elif case=='checksum':fixture.write_bytes(original[:-1]+bytes([original[-1]^1]))
            env=dict(os.environ,LYRIC_FONT_TEST_ROOT=str(root),LYRIC_FONT_EXPECT_FAILURE='1',
                ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            run=subprocess.run([str(args.renderer.resolve()),str(args.base.resolve()),str(args.output.resolve())],
                env=env,capture_output=True,text=True,timeout=60)
            (args.output/(case+'.log')).write_text(run.stdout+run.stderr)
            assert run.returncode==0 and 'LYRIC_FONT_REJECTED' in run.stdout,run.stdout+run.stderr
            results[case]=True
    (args.output/'verification.json').write_text(json.dumps(results,indent=2)+'\n')
    print('LYRIC_FONT_FAILURES_OK',','.join(results))

if __name__=='__main__':main()
