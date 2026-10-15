#!/usr/bin/env python3
"""Host checks for native dual-volume launcher, SD-only models and backup integrity."""
import argparse,hashlib,importlib.util,json,subprocess,tempfile,urllib.parse
from pathlib import Path
from render_all_ui import function
p=argparse.ArgumentParser();p.add_argument('--apps',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
spec=importlib.util.spec_from_file_location('backup',repo/'tools/backup_storage_export.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
with tempfile.TemporaryDirectory(prefix='layout-check-') as tmp:
 root=Path(tmp);data=root/'data';sd=root/'sdcard';data.mkdir();sd.mkdir();(data/'qpk').mkdir();(sd/'qpk').mkdir()
 def app(base,folder,package,source):
  d=base/'qpk'/folder;d.mkdir();(d/'manifest.json').write_text(json.dumps({'name':folder,'package':package,'entry':'app.js'}));(d/'app.js').write_text(source)
 app(data,'one','test.one','internal');app(sd,'two','test.two','sdcard');app(sd,'mirror','test.one','wrong duplicate');app(sd,'reserved','com.openvela.homeassistant','reserved')
 (sd/'qpk/link').symlink_to(data/'qpk/one',target_is_directory=True)
 source=(desktop/'desktop_main.c').read_text();start=source.index('struct qpk_entry_s\n{');structure=source[start:source.index('};',start)+2]
 body='\n'.join(function(source,n) for n in ['qpk_path_component_valid','json_get_string','qpk_probe','qpk_scan','qpk_load_entry'])
 harness=r'''
#include <assert.h>
#include <stdint.h>
#include <stdint.h>
#include <limits.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cJSON.h>
#include "qpk_volume.h"
#include "qpk_security.h"
#define NAME_MAXLEN 48
'''+structure+'''
static struct {int nqpk,qpk_capacity;bool qpk_scan_failed;struct qpk_entry_s *qpk;} g_desktop;
'''+body+r'''
int main(int argc,char **argv){assert(argc==3);qpk_scan();assert(g_desktop.nqpk==2&&!g_desktop.qpk_scan_failed);
 struct qpk_entry_s card={0};
 for(int i=0;i<2;i++){struct qpk_entry_s *e=&g_desktop.qpk[i];char file[256];size_t n;char *code=qpk_load_entry(e,file,sizeof(file),&n);assert(code);
 if(e->volume==QPK_VOLUME_SD){assert(!strcmp(code,"sdcard"));card=*e;}else assert(!strcmp(code,"internal"));free(code);}
 assert(!rename(argv[1],argv[2]));char file[256];size_t n;assert(!qpk_load_entry(&card,file,sizeof(file),&n));qpk_scan();assert(g_desktop.nqpk==1);assert(!rename(argv[2],argv[1]));free(g_desktop.qpk);
 puts("NATIVE_VOLUME_LAUNCHER_OK: actual scan/load, both volumes, duplicate priority, reserved identity, symlink and missing SD");}
'''
 (root/'launcher.c').write_text(harness)
 common=['cc','-D_GNU_SOURCE','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-I'+str(desktop),'-I'+str(a.apps/'netutils/cjson/cJSON'),'-I'+str(a.apps/'system/desktop')]
 flags=['-DQPK_VOLUME_INTERNAL_ROOT="'+str(data/'qpk')+'"','-DQPK_VOLUME_SD_ROOT="'+str(sd/'qpk')+'"']
 subprocess.run([*common,*flags,str(root/'launcher.c'),str(a.apps/'netutils/cjson/cJSON/cJSON.c'),'-lm','-o',str(root/'launcher')],check=True)
 subprocess.run([str(root/'launcher'),str(sd),str(root/'sd-retired')],check=True)
 # No Flash-read stub/library is linked: any fallback would fail this link.
 harness=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *test_models;
static FILE *model_open(const char *name,const char *mode){char path[512];assert(!strncmp(name,"/sdcard/espdl/",14));snprintf(path,sizeof(path),"%s/%s",test_models,name+14);return fopen(path,mode);}
#define fopen model_open
#include "qpk_espdl_store.c"
#undef fopen
bool qpk_dl_should_cancel(void){return false;}
int main(int argc,char **argv){assert(argc==3);test_models=argv[1];void *data=NULL;size_t bytes=0;int expected=atoi(argv[2]);int r=qpk_dl_model_load(1,&data,&bytes);assert(r==expected);if(!r){assert(data&&bytes==61248);qpk_dl_model_free(data);}else assert(!data&&!bytes);return 0;}
'''
 (root/'models.c').write_text(harness);subprocess.run([*common,str(root/'models.c'),str(desktop/'qpk_espdl_sha256.c'),'-o',str(root/'model-test')],check=True)
 models=root/'models';models.mkdir();subprocess.run([str(root/'model-test'),str(models),'-2'],check=True)
 name='human_face_detect_msr_s8_v1.espdl';original=(repo/'app/espdl-quickapp/models'/name).read_bytes();(models/name).write_bytes(original)
 subprocess.run([str(root/'model-test'),str(models),'0'],check=True);bad=bytearray(original);bad[-1]^=1;(models/name).write_bytes(bad)
 subprocess.run([str(root/'model-test'),str(models),'-74'],check=True);print('SD_ONLY_MODEL_STORE_OK: missing, valid and corrupted SD model; no Flash symbol linked')
 # Read-only backup mocked at the HTTP boundary, not in archive/hash handling.
 files={'settings/config.json':b'{"theme":1}','qpk/app/app.js':b'ui.text("ok");'}
 def fetch(request):
  action,query=request.split('?',1);key=urllib.parse.parse_qs(query)['path'][0];relative=key[5:] if key.startswith('data/') else ''
  if action=='file':return files[relative]
  prefix=relative+'/' if relative else '';items={}
  for name,content in files.items():
   if not name.startswith(prefix):continue
   rest=name[len(prefix):];first=rest.split('/')[0];directory='/' in rest;items[first]={'name':first,'directory':directory,'size':0 if directory else len(content)}
  return json.dumps({'items':list(items.values()),'truncated':False}).encode()
 archive=root/'backup.zip';assert mod.backup(fetch,archive)==(2,sum(map(len,files.values())));assert len(mod.verify(archive)['files'])==2
 try:mod.backup(fetch,archive);raise AssertionError('must preserve existing backup')
 except FileExistsError:pass
 def truncated(request):return b'{"items":[],"truncated":true}'
 try:mod.backup(truncated,root/'bad.zip');raise AssertionError('must reject truncated listing')
 except ValueError:pass
 assert not (root/'bad.zip').exists()
 calls=0
 def changed(request):
  global calls
  if request.startswith('file?'):
   calls+=1
   if calls>2:return b'changed'
  return fetch(request)
 try:mod.backup(changed,root/'changed.zip');raise AssertionError('must reject changed data')
 except ValueError:pass
 assert not (root/'changed.zip').exists();print('BACKUP_HOST_OK: complete export hashes, existing backup preservation, truncated listing and changed-file rejection')
