"""Board regression: quick-app exits return to the current launch origin."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--origin',choices=['home','apps'],required=True)
args=parser.parse_args()
ws=Path(__file__).resolve().parent.parent
out=ws/'04-v3-20260913/espdl-quickapp/evidence'/f'navigation-62-{args.origin}.json'
commands=[]
for app in ['ha','ouo','hello']:
    commands += [f'desktop ui {args.origin}',f'desktop ui {app}',
                 'desktop ui navigation-status','desktop ui app-back',
                 'desktop ui navigation-status',f'desktop ui {app}',
                 'desktop ui edge-back','desktop ui navigation-status']
commands += [f'desktop ui {args.origin}','desktop camera',
             'desktop ui navigation-status','desktop ui app-back',
             'desktop ui navigation-status','desktop ui home']
subprocess.run([sys.executable,str(ws/'diagnostics/espdl_console.py'),
                '--commands',*commands,'--seconds','0.7','--output',str(out)],
               check=True,stdout=subprocess.DEVNULL)
results=json.loads(out.read_text(encoding='utf-8'))
states=[r['output'] for r in results if r['command']=='desktop ui navigation-status']
expected=[]
for app in ['ha','ouo','hello']:
    expected += [f'page=qapp return={args.origin}',f'page={args.origin} return=home',f'page={args.origin} return=home']
expected += [f'page=qapp return={args.origin}',f'page={args.origin} return=home']
assert len(states)==len(expected)
for actual,want in zip(states,expected):
    assert want in actual,(want,actual)
print(f'PASS {args.origin}: HA, OuO, Hello header/edge exits and deferred camera exit')
