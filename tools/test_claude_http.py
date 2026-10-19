#!/usr/bin/env python3
from pathlib import Path
import argparse,os,subprocess
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';cjson=a.apps/'netutils/cjson/cJSON'
a.output.mkdir(parents=True,exist_ok=True);(a.output/'netutils').mkdir(exist_ok=True)
(a.output/'netutils/webclient.h').write_text('''#pragma once
#include <stdbool.h>
#include <stddef.h>
#define WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1 1
struct webclient_context {
 const char *url,*method;const char *const *headers;unsigned nheaders,protocol_version,timeout_sec,http_status;
 char *buffer;size_t buflen;
 int (*sink_callback)(char **,int,int,int *,void *);void *sink_callback_arg;
 int (*header_callback)(const char *,bool,void *);void *header_callback_arg;
};
void webclient_set_defaults(struct webclient_context *);
void webclient_set_static_body(struct webclient_context *,const char *,size_t);
int webclient_perform(struct webclient_context *);
''')
exe=a.output/'claude-http-host'
subprocess.run(['gcc','-std=gnu11','-D_GNU_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-pthread',
 '-I'+str(a.output),'-I'+str(desktop),'-I'+str(cjson),str(repo/'app/espdl-quickapp/tests/claude_http_host.c'),str(desktop/'glass_agent_config.c'),str(cjson/'cJSON.c'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'},timeout=60)
(a.output/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
