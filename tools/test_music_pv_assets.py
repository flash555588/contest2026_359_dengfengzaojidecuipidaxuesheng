#!/usr/bin/env python3
"""ASan/UBSan host checks for the native SD style-catalog reader."""
from pathlib import Path
import argparse,json,os,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cjson',type=Path,required=True);p.add_argument('--catalog',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];a.output.mkdir(parents=True,exist_ok=True);exe=a.output/'music-pv-assets-host'
subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(a.cjson),str(repo/'app/espdl-quickapp/tests/music_pv_assets_host.c'),str(a.cjson/'cJSON.c'),'-lm','-o',str(exe)],check=True)
r=subprocess.run([str(exe),str(a.catalog),str(a.output/'malformed-style-fixture.json')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True,check=True)
(a.output/'assets-host.json').write_text(json.dumps({'pass':True,'stdout':r.stdout,'sanitizers':['ASan','UBSan']},indent=2)+'\n',encoding='utf-8');print(r.stdout)
