#!/usr/bin/env python3
"""Validate the SD player and real chat event reducer with sanitizers."""
from pathlib import Path
import argparse,os,subprocess
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['apps','cache','output']:p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--assets',type=Path,required=True,help='Predecoded SD banks')
    a=p.parse_args();repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';a.output.mkdir(parents=True,exist_ok=True)
    qjs=a.apps/'interpreters/quickjs/quickjs';objects=sorted(a.cache.glob('lvgl-*.o'))+[a.cache.parent/'all-ui-review'/(name+'.o') for name in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
    assert all(o.exists() for o in objects)
    flags=['-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DCONFIG_BIGNUM','-DLV_CONF_INCLUDE_SIMPLE','-DGROK_HOST_TEST']+['-I'+str(x) for x in [desktop,qjs,a.apps/'graphics/lvgl',a.cache]]
    subprocess.run(['python3',str(repo/'tools/test_grok_sd.py'),'--apps',str(a.apps),'--cache',str(a.cache),'--output',str(a.output),'--assets',str(a.assets)],check=True)
    env=os.environ.copy();env.update(ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    # Real service callbacks -> compact activity projection -> production reducer.
    # This supplements the SD decoder/playback lifecycle fixture above.
    command=['gcc',*flags,'-ffunction-sections','-fdata-sections',
      '-I'+str(desktop.parent/'espclaw/include'),'-I'+str(a.apps/'netutils/cjson/cJSON'),
      str(repo/'app/espdl-quickapp/tests/chat_activity_host.c'),str(desktop/'glass_grok_state.c'),
      str(a.apps/'netutils/cjson/cJSON'/'cJSON.c'),
      '-Wl,--gc-sections','-o',str(a.output/'test-chat-activity')]
    with (a.output/'chat-activity-compile.log').open('w') as f:r=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
    if r.returncode:print((a.output/'chat-activity-compile.log').read_text());raise SystemExit(r.returncode)
    r=subprocess.run([str(a.output/'test-chat-activity')],env=env,text=True,capture_output=True)
    (a.output/'chat-activity.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);raise SystemExit(r.returncode)
if __name__=='__main__':main()
