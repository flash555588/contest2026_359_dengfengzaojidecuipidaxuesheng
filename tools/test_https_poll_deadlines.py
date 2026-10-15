from pathlib import Path
import argparse,subprocess
from render_all_ui import function
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];a.output.mkdir(exist_ok=True)
s=(repo/'app/espdl-quickapp/overlay/apps/system/desktop/glass_https.c').read_text()
(a.output/'https_poll_subset.inc').write_text(function(s,'request_state')+'\n'+function(s,'glass_https_poll'))
binary=a.output/'https-poll-deadlines'
subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(a.output),str(repo/'app/espdl-quickapp/tests/https_poll_deadline_host.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
