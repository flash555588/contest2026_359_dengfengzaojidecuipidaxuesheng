#!/usr/bin/env python3
"""Read-only /data backup through the existing paired portal; never flashes or formats."""
import argparse,getpass,hashlib,ipaddress,json,urllib.parse,urllib.request,zipfile
from pathlib import Path

class NoRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,*args,**kwargs):raise ValueError('Redirect rejected')

def component(name):
 return isinstance(name,str) and name not in ('','.', '..') and not any(c in name for c in '/\\') and all(ord(c)>=32 for c in name)

def snapshot(fetch):
 records={};folders=[]
 def walk(relative,depth):
  if depth>12:raise ValueError('Directory depth exceeds backup limit')
  page=json.loads(fetch('files?path='+urllib.parse.quote('data'+('/'+relative if relative else ''),safe='')))
  if page.get('truncated'):raise ValueError('Directory listing truncated; backup is incomplete')
  for item in page['items']:
   name=item['name']
   if not component(name):raise ValueError('Unsafe filename')
   child=relative+'/'+name if relative else name
   if item['directory']:folders.append(child);walk(child,depth+1)
   else:
    size=item['size']
    if not isinstance(size,int) or size<0 or size>16*1024*1024:raise ValueError('Invalid file size')
    if child in records:raise ValueError('Duplicate path')
    records[child]=size
 walk('',0);return records,sorted(folders)

def backup(fetch,output):
 records,folders=snapshot(fetch);digests={};output=Path(output)
 try:
  with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED) as z:
   for name in folders:z.writestr('data/'+name+'/',b'')
   for name,size in records.items():
    data=fetch('file?path='+urllib.parse.quote('data/'+name,safe=''))
    if len(data)!=size:raise ValueError('File changed or download incomplete')
    digests[name]={'bytes':size,'sha256':hashlib.sha256(data).hexdigest()};z.writestr('data/'+name,data)
   # Refuse a partial backup if any observed path, size or content changes.
   if snapshot(fetch)!=(records,folders):raise ValueError('Storage changed during backup; stop apps and retry')
   for name,r in digests.items():
    if hashlib.sha256(fetch('file?path='+urllib.parse.quote('data/'+name,safe=''))).hexdigest()!=r['sha256']:raise ValueError('File changed during backup')
   z.writestr('MANIFEST.json',json.dumps({'format':1,'source':'/data','files':digests,'directories':folders},ensure_ascii=False,indent=2))
 except FileExistsError:raise
 except BaseException:
  output.unlink(missing_ok=True);raise
 output.chmod(0o600);verify(output);return len(records),sum(records.values())

def verify(path):
 with zipfile.ZipFile(path) as z:
  if z.testzip():raise ValueError('Archive CRC mismatch')
  manifest=json.loads(z.read('MANIFEST.json'))
  for name,r in manifest['files'].items():
   if not all(component(c) for c in name.split('/')):raise ValueError('Unsafe archive path')
   data=z.read('data/'+name)
   if len(data)!=r['bytes'] or hashlib.sha256(data).hexdigest()!=r['sha256']:raise ValueError('Backup hash mismatch')
 return manifest

def main():
 p=argparse.ArgumentParser(description=__doc__);g=p.add_mutually_exclusive_group(required=True);g.add_argument('--url');g.add_argument('--verify',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 if a.verify:print('BACKUP_VERIFIED',len(verify(a.verify)['files']));return
 if not a.output:p.error('--output is required with --url')
 u=urllib.parse.urlsplit(a.url);address=ipaddress.ip_address(u.hostname or '')
 if u.scheme!='http' or not (address.is_private or address.is_loopback) or u.port!=8080 or u.path not in ('','/') or u.query or u.fragment or u.username:p.error('Use the device LAN IPv4 address: http://IP:8080')
 code=getpass.getpass('Device pairing code (6 digits): ')
 if len(code)!=6 or not code.isascii() or not code.isdigit():p.error('Invalid pairing code')
 opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect());base=a.url.rstrip('/')+'/api/'
 body=json.dumps({'code':code}).encode();req=urllib.request.Request(base+'pair',data=body,headers={'Content-Type':'application/json'})
 with opener.open(req,timeout=20) as r:token=json.load(r)['token']
 def fetch(path):
  req=urllib.request.Request(base+path,headers={'Authorization':'Bearer '+token})
  with opener.open(req,timeout=30) as r:return r.read(16*1024*1024+1)
 count,size=backup(fetch,a.output);print('BACKUP_VERIFIED',count,'files',size,'bytes')
 print('Keep this backup locally. It includes application data and saved settings.')
if __name__=='__main__':main()
