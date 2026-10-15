#!/usr/bin/env python3
"""Upload raw SD assets through existing paired file portal, then hash on board."""
import argparse,http.client,json,re,time
from pathlib import Path
from urllib.parse import quote
from grok_board_io import Console
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='COM4');p.add_argument('--board',default='192.168.31.33:8080');p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(exist_ok=True,parents=True);manifest=json.loads((a.root/'manifest.json').read_text());results=[]
    with (a.output/'sd-verify-console.log').open('w',encoding='utf-8') as log:
        usb=Console(a.port,log);usb.command('');usb.command('mkdir /sdcard/grok')
        code=re.search(r'PORTAL_PAIR=(\d{6})',usb.command('desktop portal-pair',private=True))
        if not code:raise RuntimeError('Device portal pairing unavailable')
        conn=http.client.HTTPConnection(a.board,timeout=15);body=json.dumps({'code':code[1]});conn.request('POST','/api/pair',body,{'Content-Type':'application/json'});response=conn.getresponse();data=json.loads(response.read())
        if response.status!=200:raise RuntimeError('Portal pairing failed')
        token=data['token'];del code,data,body;conn.close()
        def request(method,path,body=None):
            conn=http.client.HTTPConnection(a.board,timeout=600);conn.request(method,path,body,{'Authorization':'Bearer '+token,'Content-Type':'application/json'});r=conn.getresponse();payload=r.read();conn.close();return r.status,json.loads(payload)
        for bank in manifest['banks']:
            name=bank['file'];local=a.root/name;size=local.stat().st_size
            status,listing=request('GET','/api/files?path=sdcard/grok');existing=next((v for v in listing.get('items',[]) if v['name']==name),None)
            if existing and existing['size']!=size:
                usb.command('mv /sdcard/grok/'+name+' /sdcard/grok/'+name+'.bak')
                existing=None
            if not existing:
                conn=http.client.HTTPConnection(a.board,timeout=600);conn.putrequest('PUT','/api/file?path='+quote('sdcard/grok/'+name,safe='/'));conn.putheader('Authorization','Bearer '+token);conn.putheader('Content-Length',str(size));conn.endheaders()
                began=last=time.monotonic();sent=0;print('UPLOAD',name,size,flush=True)
                with local.open('rb') as f:
                    while chunk:=f.read(65536):
                        conn.send(chunk);sent+=len(chunk)
                        if time.monotonic()-last>=20:print(name,round(sent/size*100,1),'%',round(time.monotonic()-began),'s',flush=True);last=time.monotonic()
                response=conn.getresponse();payload=response.read();conn.close()
                if response.status!=200:raise RuntimeError('Upload failed '+name+' HTTP '+str(response.status)+' '+payload.decode(errors='replace'))
                print('UPLOADED',name,round(time.monotonic()-began,1),'s',flush=True)
            digest=re.search(r'GROK_VERIFY '+re.escape(name)+r' ([a-f0-9]{64})',usb.command('desktop grok-verify '+name,timeout=600))
            if not digest or digest[1]!=bank['sha256']:raise RuntimeError('Device SHA256 mismatch: '+name)
            results.append({'file':name,'bytes':size,'device_sha256':digest[1],'verified':True});(a.output/'sd-verified.json').write_text(json.dumps(results,indent=2));print('DEVICE_SHA256_OK',name,flush=True)
        status,listing=request('GET','/api/files?path=sdcard/grok')
        if not any(v['name']=='manifest.json' for v in listing.get('items',[])):
            status,_=request('PUT','/api/file?path=sdcard/grok/manifest.json',(a.root/'manifest.json').read_bytes())
            if status!=200:raise RuntimeError('Manifest upload failed')
        usb.close();token=None
    print('GROK_SD_UPLOAD_VERIFIED_OK',flush=True)
if __name__=='__main__':main()
