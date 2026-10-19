import argparse,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
repo=Path(__file__).resolve().parents[1];(a.output/'netutils').mkdir(exist_ok=True)
(a.output/'netutils/webclient.h').write_text('''#pragma once
struct webclient_context {const char *url;char *buffer;int buflen,timeout_sec,http_status;int (*sink_callback)(char **,int,int,int *,void *);void *sink_callback_arg;};
void webclient_set_defaults(struct webclient_context *);int webclient_perform(struct webclient_context *);
''')
exe=a.output/'import-host';subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-include','stdbool.h','-I'+str(a.output),'-I'+str(repo/'app/espdl-quickapp/overlay/apps/system/desktop'),str(repo/'app/espdl-quickapp/tests/music_pv_import_host.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
