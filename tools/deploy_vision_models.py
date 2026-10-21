#!/usr/bin/env python3
"""Install only manifest-locked ESP-DL models to a fresh SD directory; never flash."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import argparse,hashlib,ipaddress,json,re,secrets,shutil,socket,threading,time
import serial
from serial.tools import list_ports
def verify(root):
    manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    header=(Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop/espdl_assets.h').read_text(encoding='utf-8')
    locked={}
    for name,size,digest in re.findall(r'\{"([^"\n]+)", (\d+), \d+, \{([^}]+)\}\}',header):
        tokens=re.findall(r'0x([0-9a-fA-F]{2})',digest)
        if len(tokens)!=32 or name in locked:raise ValueError('Invalid compiled model table')
        locked[name]={'bytes':int(size),'sha256':''.join(tokens).lower()}
    records={r['file']:{'bytes':r['bytes'],'sha256':r['sha256']} for r in manifest['records']}
    if len(locked)!=3 or records!=locked:raise ValueError('Model manifest differs from compiled table')
    for name,row in records.items():
        if not re.fullmatch(r'[A-Za-z0-9_.-]+',name):raise ValueError('Unsafe model name')
        f=root/name
        if f.is_symlink() or f.stat().st_size!=row['bytes'] or hashlib.sha256(f.read_bytes()).hexdigest()!=row['sha256']:
            raise ValueError('Model hash mismatch: '+name)
    license=root/'ESP-DL-LICENSE'
    records['ESP-DL-LICENSE']={'bytes':license.stat().st_size,'sha256':hashlib.sha256(license.read_bytes()).hexdigest()}
    return {'files':records}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--board',required=True);p.add_argument('--port',default='COM4');p.add_argument('--http-port',type=int,default=8775);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
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
    stage='/sdcard/.dl-'+secrets.token_hex(4);report={'published':False,'target':'/sdcard/espdl','staging':stage,'files':records,'board_model_selftest_verified':False}
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
            if re.search(r'(?m)^\s*espdl/\s*$',listing):raise RuntimeError('Existing espdl directory preserved; choose explicit update procedure')
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
            cmd('cd /');cmd('mv '+stage+' /sdcard/espdl');report['published']=True
            report['listing']=cmd('ls /sdcard/espdl')
            checked=cmd('desktop espdl test 0',120)
            report['board_model_selftest_verified']='espdl test completed error=0' in checked and '[espdl] model fixture test result=0' in checked
            if not report['board_model_selftest_verified']:raise RuntimeError('Board model verification failed; see console.log')
    finally:
        console.close();http.shutdown();http.server_close();worker.join(timeout=3)
        (a.output/'deployment.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('VISION_SD_DEPLOYED',sum(r['bytes'] for r in records),'bytes',flush=True)
if __name__=='__main__':main()
