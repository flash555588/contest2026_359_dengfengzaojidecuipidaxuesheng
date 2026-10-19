#!/usr/bin/env python3
"""Host feasibility probe using the same QuickJS source as the ESP build.

No Canvas emulation: metrics/render calls deliberately report a missing backend.
Host timings and memory are not ESP frame-rate or exact 32-bit heap measurements.
"""
from pathlib import Path
import argparse,json,os,subprocess
from concurrent.futures import ThreadPoolExecutor

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apps',type=Path,required=True)
parser.add_argument('--objects',type=Path,help='Cached objects; only with explicitly matching --bignum')
parser.add_argument('--bignum',action='store_true',help='Enable optional BigFloat/BigDecimal (off on this board)')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();repo=Path(__file__).resolve().parents[1]
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=out/'engine.js'
code=Path(str(source)+'.qjbc')
code.unlink(missing_ok=True)
files=sorted((repo/'app/jizura/upstream/src').glob('*.js'))
source.write_text('\n'.join(p.read_text(encoding='utf-8') for p in files),encoding='utf-8')
qjs=args.apps/'interpreters/quickjs/quickjs'
names=['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']
flags=['cc','-std=gnu11','-D_GNU_SOURCE','-O1','-g','-fwrapv','-fsanitize=address,undefined',
       '-fno-omit-frame-pointer','-I'+str(qjs),'-DCONFIG_VERSION="'+(qjs/'VERSION').read_text().strip()+'"']
if args.bignum:flags.append('-DCONFIG_BIGNUM')
if args.objects:
    if not args.bignum:raise SystemExit('Cached renderer objects have BIGNUM enabled; build matching board objects instead.')
    objects=[args.objects/(name+'.o') for name in names]
else:
    folder=out/'qjs-host';folder.mkdir(exist_ok=True)
    objects=[folder/(name+'.o') for name in names]
    def compile_one(pair):
        name,obj=pair
        subprocess.run([*flags,'-c',str(qjs/name),'-o',str(obj)],check=True)
    with ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(compile_one,zip(names,objects)))
command=[*flags,str(repo/'app/jizura/tests/quickjs-port-probe.c'),
         *map(str,objects),'-lm','-ldl','-pthread','-o',str(out/'probe')]
subprocess.run(command,check=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
runs=[]
for limit in [8,12,16,24,32,48,64]:
    p=subprocess.run([str(out/'probe'),'source',str(source),str(limit),'1024'],env=env,capture_output=True,text=True,timeout=45)
    runs.append(dict(mode='source',limit_mib=limit,stack_kib=1024,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
if code.exists():
    for limit,stack in [(8,1024),(12,1024),(16,1024),(24,1024),(16,16),(16,32),(16,64),(16,128)]:
        p=subprocess.run([str(out/'probe'),'bytecode',str(code),str(limit),str(stack)],env=env,capture_output=True,text=True,timeout=45)
        runs.append(dict(mode='bytecode',limit_mib=limit,stack_kib=stack,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
report=dict(host_only=True,quickjs_version=(qjs/'VERSION').read_text().strip(),bignum=args.bignum,
            source_files=len(files),source_bytes=source.stat().st_size,
            bytecode_bytes=code.stat().st_size if code.exists() else None,runs=runs)
(out/'quickjs-probe.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
for run in runs:
    print(run['mode'],run['limit_mib'],run['stack_kib'],'PASS' if run['returncode']==0 else 'FAIL',
          run['stderr'].strip().splitlines()[-1:] if run['returncode'] else '')
    if run['returncode']==0:print(run['stdout'])
