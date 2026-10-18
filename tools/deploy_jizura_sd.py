#!/usr/bin/env python3
"""Copy a verified, public asset pack to a fresh SD directory; never flash."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import argparse,hashlib,ipaddress,json,re,secrets,shutil,socket,threading,time
import serial
from serial.tools import list_ports
from prepare_jizura_sd import verify

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--board',required=True);p.add_argument('--port',default='COM4');p.add_argument('--http-port',type=int,default=8772);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=a.source.resolve();manifest=verify(root)
    board=ipaddress.ip_address(a.board)
    if board.version!=4 or not board.is_private or board.is_loopback:raise ValueError('Use the attached board private IPv4')
    ports=[v for v in list_ports.comports() if v.device==a.port]
    if len(ports)!=1 or (ports[0].vid,ports[0].pid)!=(0x303a,0x1001):raise ValueError('Expected ESP USB Serial/JTAG port')
    route=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);route.connect((a.board,9));host=route.getsockname()[0];route.close()
    names=sorted(manifest['files'])+['manifest.json'];records=[]
    for name in names:
        if not re.fullmatch(r'[A-Za-z0-9_./\[\]-]+',name) or '..' in Path(name).parts:raise ValueError('Unsafe asset name')
        f=root/name;records.append({'name':name,'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()})
    served=set()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            m=re.fullmatch(r'/(\d+)',self.path)
            if self.client_address[0]!=a.board or not m or int(m[1])>=len(names):self.send_error(404);return
            i=int(m[1]);file=root/names[i];self.send_response(200);self.send_header('Content-Length',str(file.stat().st_size));self.end_headers()
            with file.open('rb') as stream:shutil.copyfileobj(stream,self.wfile)
            served.add(i)
        def log_message(self,*args):pass
    a.output.mkdir(parents=True,exist_ok=True);http=ThreadingHTTPServer((host,a.http_port),Handler);worker=threading.Thread(target=http.serve_forever,daemon=True);worker.start()
    console=serial.Serial(port=None,baudrate=115200,timeout=.08,write_timeout=3);console.dtr=False;console.rts=False;console.port=a.port
    stage='/sdcard/.jz-'+secrets.token_hex(4);report={'published':False,'target':'/sdcard/jizura','staging':stage,'files':records,'board_font_sha256_verified':False}
    try:
        console.open()
        with (a.output/'console.log').open('wb') as log:
            def collect(seconds):
                data=bytearray();until=time.monotonic()+seconds
                while time.monotonic()<until:
                    block=console.read(console.in_waiting or 1)
                    if block:log.write(block);log.flush();data.extend(block)
                text=data.decode('utf-8',errors='replace')
                if re.search(r'PANIC|Assertion failed|access fault',text,re.I):raise RuntimeError('Board fault during transfer')
                return text
            def cmd(text,timeout=5):
                if len(text.encode())>=79:raise ValueError('NSH line too long: '+text)
                collect(.1);console.write(b'\x15');collect(.15)
                for b in (text+'\r\n').encode():console.write(bytes([b]));time.sleep(.008)
                data='';until=time.monotonic()+timeout
                while time.monotonic()<until:
                    data+=collect(.15)
                    if re.search(r'nsh>\s*(?:\x1b\[K)?',data):
                        if re.search(r'nsh:.*(?:failed|not found|invalid)',data,re.I):raise RuntimeError(data)
                        return data
                raise RuntimeError('Console timeout: '+text)
            collect(.5);cmd('')
            if not re.search(r'/sdcard type (?:vfat|fatfs)',cmd('mount')):raise RuntimeError('SD is not mounted')
            listing=cmd('ls /sdcard')
            if re.search(r'(?m)^\s*jizura/\s*$',listing):raise RuntimeError('Existing jizura directory preserved; choose explicit update procedure')
            space=cmd('df');m=re.search(r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+/sdcard',space)
            if not m or int(m[1])*int(m[4])<sum(r['bytes'] for r in records)+1024*1024:raise RuntimeError('Insufficient SD space')
            cmd('mkdir '+stage);cmd('cd '+stage)
            dirs=set()
            for name in names:
                parent=Path(name).parent
                for sub in reversed([parent,*parent.parents]):
                    s=sub.as_posix()
                    if s!='.' and s not in dirs:cmd('mkdir '+s);dirs.add(s)
            for i,record in enumerate(records):
                print('SD_TRANSFER',i+1,len(records),record['name'],record['bytes'],flush=True)
                cmd(f'wget -o a{i}.part http://{host}:{a.http_port}/{i}',max(45,record['bytes']/65536+30))
                listing=cmd(f'ls -l a{i}.part')
                if not re.search(r'\b'+str(record['bytes'])+r'\b',listing):raise RuntimeError('SD download size mismatch: '+record['name'])
                cmd(f"mv a{i}.part '{record['name']}'")
            if served!=set(range(len(names))):raise RuntimeError('Incomplete transfer')
            readback=cmd('cat styles.json');start=readback.index('{"version":1,');catalog,_=json.JSONDecoder().raw_decode(readback[start:])
            if catalog!=json.loads((root/'styles.json').read_text(encoding='utf-8')):raise RuntimeError('Native style catalog readback mismatch')
            report['native_catalog_readback_equal']=True
            cmd('cd /');cmd('mv '+stage+' /sdcard/jizura');report['published']=True
            report['listing']=cmd('ls /sdcard/jizura')
    finally:
        console.close();http.shutdown();http.server_close();worker.join(timeout=3)
        (a.output/'deployment.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('JIZURA_SD_DEPLOYED',sum(r['bytes'] for r in records),'bytes',flush=True)
if __name__=='__main__':main()
