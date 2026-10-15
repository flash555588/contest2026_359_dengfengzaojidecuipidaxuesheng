"""Run actual timeout functions with real QuickJS under sanitizers."""
from pathlib import Path
import argparse,re,subprocess
from render_all_ui import function
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--cache',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];out=a.cache/'deadline-tests';out.mkdir(exist_ok=True)
source=(repo/'app/espdl-quickapp/overlay/apps/system/desktop/qpk_runtime.c').read_text()
assert 'qpk_startup_budget' not in source and 'QPK_BUILTIN_UI_STARTUP_BUDGET' not in source
names=['qpk_now_ms','qpk_interrupt','qpk_deadline_begin','qpk_deadline_end','qpk_hang_notify','qpk_run_jobs','qpk_budget_report','qpk_call','qpk_call_args']
(out/'qpk_deadlines_subset.inc').write_text('\n'.join(function(source,n) for n in names).replace('struct qpk_runtime_s *qpk = opaque;','typeof(g_qpk) *qpk = opaque;'))
qjs=a.apps/'interpreters/quickjs/quickjs'
flags=['-std=gnu11','-O1','-g','-fwrapv','-D_GNU_SOURCE','-DCONFIG_BIGNUM','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(qjs),'-I'+str(out)]
objects=[a.cache/(name+'.o') for name in ['quickjs.c','libregexp.c','libunicode.c','cutils.c','libbf.c']]
binary=out/'qpk-deadlines'
subprocess.run(['gcc',*flags,str(repo/'app/espdl-quickapp/tests/qpk_deadlines_host.c'),*map(str,objects),'-lm','-ldl','-pthread','-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True,timeout=15)
