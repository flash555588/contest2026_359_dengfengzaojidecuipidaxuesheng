#!/usr/bin/env python3
"""Stage the pinned public font on SD; no firmware or private config writes."""
import argparse,hashlib,ipaddress,json,re,secrets,socket,threading,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from serial.tools import list_ports
from test_jizura_device import Console

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,required=True);p.add_argument('--board',required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    board=ipaddress.ip_address(a.board);assert board.version==4 and board.is_private and not board.is_loopback
    ports=[x for x in list_ports.comports() if x.device=='COM4']
    assert len(ports)==1 and (ports[0].vid,ports[0].pid)==(0x303a,0x1001)
    assert ports[0].serial_number.lower()=='e8:f6:0a:e7:6f:f1'
    meta=json.loads((a.assets/'manifest.json').read_text())
    header=(Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop/glass_sd_font_asset.h').read_text()
    assert meta['sha256']==re.search(r'SD_FONT_SHA256 "([a-f0-9]{64})"',header)[1]
    assert re.fullmatch(r'[A-Za-z0-9-]+\.ttf',meta['file'])
    font=a.assets/meta['file'];assert hashlib.sha256(font.read_bytes()).hexdigest()==meta['sha256']
    licenses=sorted(a.assets.glob('*-OFL.txt'));assert licenses
    for file in licenses:
        assert re.fullmatch(r'[a-z]+-OFL\.txt',file.name)
        assert 'SIL OPEN FONT LICENSE' in file.read_text(encoding='utf-8')
    route=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);route.connect((a.board,9));host=route.getsockname()[0];route.close()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            file=({'/f':font}|{'/l'+str(i):file for i,file in enumerate(licenses)}).get(self.path)
            if self.client_address[0]!=a.board or not file:self.send_error(404);return
            self.send_response(200);self.send_header('Content-Length',str(file.stat().st_size));self.end_headers()
            with file.open('rb') as stream:
                for block in iter(lambda:stream.read(65536),b''):self.wfile.write(block)
        def log_message(self,*args):pass
    server=ThreadingHTTPServer((host,8773),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    a.output.mkdir(parents=True,exist_ok=True);report={'published':False,'font':meta}
    try:
        with (a.output/'console.log').open('w',encoding='utf-8') as log:
            c=Console(log)
            try:
                def cmd(s,timeout=30):
                    reply=c.command(s,timeout)
                    if re.search(r'nsh:.*(?:failed|not found|invalid)',reply,re.I):raise RuntimeError(reply)
                    return reply
                assert '/sdcard type vfat' in cmd('mount')
                root=cmd('ls /sdcard')
                if not re.search(r'(?m)^\s*fonts/\s*$',root):cmd('mkdir /sdcard/fonts')
                target='/sdcard/fonts/'+meta['file']
                assert meta['file'] not in cmd('ls /sdcard/fonts'),'Existing font preserved'
                stage='/sdcard/fonts/f'+secrets.token_hex(3)+'.part'
                cmd('wget -o '+stage+' http://'+host+':8773/f',180)
                assert re.search(r'\b'+str(meta['bytes'])+r'\b',cmd('ls -l '+stage))
                cmd('mv '+stage+' '+target)
                for i,file in enumerate(licenses):
                    cmd('wget -o /sdcard/fonts/'+file.name+' http://'+host+':8773/l'+str(i),40)
                report['published']=True;report['target']=target
            finally:c.close()
    finally:
        server.shutdown();server.server_close();thread.join(3)
        (a.output/'deployment.json').write_text(json.dumps(report,indent=2)+'\n')
    print('SD_FONT_PUBLISHED',meta['bytes'],flush=True)

if __name__=='__main__':main()
