#!/usr/bin/env python3
"""Publish new checked PV banks and an original audio fixture to the attached SD."""
import argparse,hashlib,ipaddress,json,re,secrets,shutil,socket,sys,threading,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import serial
from serial.tools import list_ports
from render_jizura_banks import track_key
def main():
    p=argparse.ArgumentParser();p.add_argument('--banks',type=Path,required=True);p.add_argument('--audio',type=Path,required=True);p.add_argument('--board',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--http-port',type=int,default=8774)
    p.add_argument('--resume-from',type=Path);p.add_argument('--fast',action='store_true');p.add_argument('--finalize-only',action='store_true')
    p.add_argument('--banks-only',action='store_true');p.add_argument('--target',choices=['pv','pv-jpeg','pv-jpeg-next'],default='pv');a=p.parse_args()
    if a.finalize_only:assert a.resume_from
    assert ipaddress.ip_address(a.board).is_private
    ports=[v for v in list_ports.comports() if v.device=='COM4'];assert len(ports)==1 and (ports[0].vid,ports[0].pid)==(0x303a,0x1001) and (ports[0].serial_number or '').lower()=='e8:f6:0a:e7:6f:f1'
    manifest=json.loads((a.banks/'manifest.json').read_text(encoding='utf-8'));assert manifest['complete']
    a.output.mkdir(parents=True,exist_ok=True);lrc=a.output/'demo.lrc';lrc.write_text(manifest['project']['lyrics']+'\n',encoding='utf-8')
    files=[]
    for name,record in sorted(manifest['files'].items()):
        assert re.fullmatch(r'[a-f0-9]{8}-\d{2}\.jpv',name);file=a.banks/name
        assert file.stat().st_size==record['bytes'] and hashlib.sha256(file.read_bytes()).hexdigest()==record['sha256'];files.append((name,file))
    files.append(('manifest.json',a.banks/'manifest.json'))
    if not a.banks_only:files.extend([('jizura-demo.mp3',a.audio),('demo.lrc',lrc)])
    route=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);route.connect((a.board,9));host=route.getsockname()[0];route.close()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            m=re.fullmatch(r'/(\d+)',self.path)
            if self.client_address[0]!=a.board or not m or int(m[1])>=len(files):self.send_error(404);return
            file=files[int(m[1])][1];self.send_response(200);self.send_header('Content-Length',str(file.stat().st_size));self.end_headers()
            with file.open('rb') as f:shutil.copyfileobj(f,self.wfile)
        def log_message(self,*args):pass
    http=ThreadingHTTPServer((host,a.http_port),Handler);thread=threading.Thread(target=http.serve_forever,daemon=True);thread.start()
    ser=serial.Serial(port=None,baudrate=115200,timeout=.08,write_timeout=3);ser.dtr=ser.rts=False;ser.port='COM4';ser.open()
    stage='/sdcard/.jpv-'+secrets.token_hex(4);report=dict(published=False,stage=stage,target='/sdcard/jizura/'+a.target,files=[])
    if a.resume_from:
        report=json.loads(a.resume_from.read_text());assert not report['published'] and re.fullmatch(r'/sdcard/\.jpv-[a-f0-9]{8}',report['stage'])
        stage=report['stage']
    with (a.output/'console.log').open('w',encoding='utf-8') as log:
        def drain(seconds):
            data=bytearray();until=time.monotonic()+seconds
            while time.monotonic()<until:
                block=ser.read(ser.in_waiting or 1)
                if block:data+=block
            text=data.decode(errors='replace');log.write(text);log.flush()
            if re.search(r'PANIC|Assertion failed|access fault',text,re.I):raise RuntimeError('Board fault')
            return text
        def command(text,timeout=10):
            if len(text.encode())>=79:raise ValueError('NSH line too long')
            drain(.1);ser.write(b'\x15');drain(.15)
            for b in (text+'\r\n').encode():ser.write(bytes([b]));time.sleep(.008)
            result='';until=time.monotonic()+timeout
            while time.monotonic()<until:
                result+=drain(.15)
                if 'nsh>' in result:
                    if re.search(r'nsh:.*(?:failed|not found|invalid)',result,re.I):raise RuntimeError(result)
                    return result
            raise TimeoutError(text)
        def checked_size(name,expected):
            # USB console output may drop a character during Wi-Fi logging.
            # Retry only the read-only observation, never the file transfer.
            for _ in range(4):
                if re.search(r'\b'+str(expected)+r'\b',command('ls -l '+name)):return
            raise AssertionError('SD size could not be verified: '+name)
        try:
            command('');assert '/sdcard type vfat' in command('mount')
            if re.search(r'(?im)^\s*'+re.escape(a.target)+r'/\s*$',command('ls /sdcard/jizura')):raise RuntimeError('Existing PV assets preserved; choose a new publication directory')
            if not re.search(r'(?im)^\s*music/\s*$',command('ls /sdcard')):command('mkdir /sdcard/music')
            if re.search(r'(?im)^.*jizura-demo\.mp3\s*$',command('ls /sdcard/music')) and not a.finalize_only and not a.banks_only:raise RuntimeError('Existing demo audio preserved')
            space=command('df');m=re.search(r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+/sdcard',space)
            assert m and int(m[1])*int(m[4])>sum(f.stat().st_size for _,f in files)+1024*1024
            if not a.resume_from:command('mkdir '+stage)
            command('cd '+stage)
            for i,(name,file) in enumerate(files):
                if a.finalize_only and name in ('jizura-demo.mp3','demo.lrc'):continue
                if i<len(report['files']):
                    assert report['files'][i]['name']==name and report['files'][i]['bytes']==file.stat().st_size
                    checked_size(name,file.stat().st_size);continue
                print('PV_SD_TRANSFER',i+1,len(files),name,file.stat().st_size,flush=True);began=time.monotonic()
                if re.search(r'(?im)^\s*f'+str(i)+r'\.part\s*$',command('ls')):command(f'mv f{i}.part p{i}-'+secrets.token_hex(3)+'.saved')
                request=f'desktop jizura-get http://{host}:{a.http_port}/{i} f{i}.part' if a.fast else f'wget -o f{i}.part http://{host}:{a.http_port}/{i}'
                reply=command(request,max(120,file.stat().st_size/32768+30))
                if a.fast:assert 'error=0' in reply,reply
                checked_size(f'f{i}.part',file.stat().st_size)
                command(f'mv f{i}.part {name}');report['files'].append(dict(name=name,bytes=file.stat().st_size,seconds=round(time.monotonic()-began,2)))
                (a.output/'deployment.json').write_text(json.dumps(report,indent=2))
            if a.banks_only:
                command('cd /');command('mv '+stage+' '+report['target']);report['published']=True
                print('JIZURA_PV_BANKS_PUBLISHED',sum(r['bytes'] for r in report['files']),flush=True);return
            command('cd /sdcard/music');listing=command('ls')
            if not re.search(r'(?im)^\s*lyrics/\s*$',listing):command('mkdir lyrics')
            if not a.finalize_only:
                command(f'mv {stage}/jizura-demo.mp3 jizura-demo.mp3')
                command(f'mv {stage}/demo.lrc lyrics/jizura-demo.lrc')
            command('cd lyrics');short=f'{track_key(manifest["project"]["title"],manifest["project"]["artist"]):08x}.lrc'
            if re.search(r'(?im)^\s*'+re.escape(short)+r'\s*$',command('ls')):raise RuntimeError('Existing hashed lyric preserved')
            command('mv jizura-demo.lrc '+short)
            command('cd /');command('mv '+stage+' /sdcard/jizura/pv');report['published']=True
            print('JIZURA_PV_SD_PUBLISHED',sum(r['bytes'] for r in report['files']),flush=True)
        finally:
            (a.output/'deployment.json').write_text(json.dumps(report,indent=2));ser.close();http.shutdown();http.server_close();thread.join(timeout=3)
if __name__=='__main__':main()
