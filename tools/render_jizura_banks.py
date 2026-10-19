#!/usr/bin/env python3
"""Render full JIZURA in a real browser to bounded, seekable SD frame banks."""
import argparse,hashlib,json,struct,threading,zlib
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer

HEADER=288
def track_key(name,artist):
    h=2166136261
    for s in (name,artist):
        for b in s.encode()+b'\0':h=((h^b)*16777619)&0xffffffff
    return h
def unpack(data,raw=False):
    if raw:
        if len(data)!=640*360*2:raise ValueError('raw size')
        return data
    out=bytearray();at=0
    while at<len(data):
        if at+2>len(data):raise ValueError('short control')
        control=struct.unpack_from('<H',data,at)[0];at+=2;n=(control&32767)+1
        if len(out)+n*2>640*360*2:raise ValueError('run overflow')
        size=2 if control&32768 else n*2
        if at+size>len(data):raise ValueError('short data')
        out+=data[at:at+size]*n if control&32768 else data[at:at+size];at+=size
    if len(out)!=640*360*2:raise ValueError('pixel count')
    return bytes(out)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--project',type=Path);p.add_argument('--port',type=int,default=8773);p.add_argument('--fps',type=int,default=12)
    p.add_argument('--seconds',type=int,default=8);p.add_argument('--styles',default=','.join(map(str,range(27))));a=p.parse_args()
    if not 1<=a.fps<=24 or not 1<=a.seconds<=600:raise ValueError('Playback limits')
    styles=[int(s) for s in a.styles.split(',')];assert len(set(styles))==len(styles) and all(0<=s<27 for s in styles)
    root=a.assets.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if any(out.glob('*.jpv')):raise ValueError('Preserve existing banks: use a fresh output directory')
    source=Path(__file__).resolve().parents[1]/'app/jizura/port'
    project=json.loads((a.project or root/'projects/demo.json').read_text(encoding='utf-8'))
    name,artist=project['title'],project.get('artist','');assert len(name.encode())<128 and len(artist.encode())<96
    frames=a.seconds*a.fps;key=track_key(name,artist);lock=threading.Lock();writers={};report={'complete':False,'files':{},'project':project,'fps':a.fps,'frames_per_style':frames}
    job=json.dumps(dict(project=project,fps=a.fps,frames=frames,styles=styles),ensure_ascii=False).encode()
    html='''<!doctype html><meta charset="utf-8"><title>JIZURA SD 导出</title><link rel="stylesheet" href="/fonts.css"><style>body{background:#16171c;color:#eee;font-family:system-ui;max-width:900px;margin:40px auto}button{padding:14px}canvas{display:block;margin:24px 0;border:1px solid #555}pre{white-space:pre-wrap}</style><h1>完整版效果 → ESP32-P4 素材</h1><p>保持原版排版、字体、纹理和效果，在电脑渲染；设备从 SD 读取校验帧。</p><button id="start">生成 SD 播放素材</button><p id="status">准备就绪</p><canvas id="stage" width="640" height="360"></canvas><pre id="proof"></pre><script>globalThis.JIZURA_OFFLINE_FONTS=true;</script><script src="/vendor/mp4-muxer.min.js"></script><script src="/engine.js"></script><script src="/export.js"></script>'''.encode()
    class Handler(BaseHTTPRequestHandler):
        def reply(self,status,data=b'',kind='application/octet-stream'):
            self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        def do_GET(self):
            path=self.path.split('?')[0]
            if path=='/':return self.reply(200,html,'text/html; charset=utf-8')
            if path=='/job.json':return self.reply(200,job,'application/json')
            if path=='/export.js':return self.reply(200,(source/'sd-export.js').read_bytes(),'text/javascript')
            file=(root/path.lstrip('/')).resolve()
            if not file.is_relative_to(root) or not file.is_file():return self.reply(404)
            import mimetypes
            return self.reply(200,file.read_bytes(),mimetypes.guess_type(str(file))[0] or 'application/octet-stream')
        def do_POST(self):
            try:
                n=int(self.headers.get('Content-Length',0))
                if n<1 or n>640*360*2+72:raise ValueError('Request size')
                data=self.rfile.read(n)
                with lock:
                    if self.path=='/finish':
                        proof=json.loads(data);assert proof['pass'] and proof['frames']==frames*len(styles)
                        assert len(writers)==len(styles) and all(w['next']==frames for w in writers.values())
                        for i,w in writers.items():
                            f=w['file'];f.seek(HEADER);f.write(b''.join(w['index']));f.close()
                            file=out/w['name'];report['files'][w['name']]={'bytes':file.stat().st_size,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()}
                        report.update(complete=True,browser=proof);(out/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
                        print('JIZURA_BANKS_COMPLETE',len(writers),sum(v['bytes'] for v in report['files'].values()),flush=True)
                    else:
                        _,tag,style,frame=self.path.split('/');i=int(style);fnum=int(frame);assert tag=='frame' and i in styles and 0<=fnum<frames
                        checksum,raw=struct.unpack_from('<II',data);assert raw in (0,1);payload=data[8:]
                        decoded=unpack(payload,bool(raw));assert zlib.adler32(decoded)==checksum
                        if i not in writers:
                            assert fnum==0;filename=f'{key:08x}-{i:02d}.jpv';f=(out/filename).open('w+b')
                            header=bytearray(HEADER);struct.pack_into('<8s7I',header,0,b'JZPV01\0\0',1,640,360,a.fps,frames,a.seconds*1000,i)
                            header[36:36+len(name.encode())]=name.encode();header[164:164+len(artist.encode())]=artist.encode()
                            f.write(header);f.write(bytes(frames*12));writers[i]={'file':f,'name':filename,'next':0,'index':[]}
                        w=writers[i]
                        if fnum<w['next']:
                            prior=struct.unpack('<III',w['index'][fnum]);assert prior[1:]==(len(payload)|(0x80000000 if raw else 0),checksum)
                        else:
                            assert fnum==w['next'];f=w['file'];offset=f.tell();f.write(payload)
                            w['index'].append(struct.pack('<III',offset,len(payload)|(0x80000000 if raw else 0),checksum));w['next']+=1
                self.reply(200,b'OK')
            except Exception as error:self.reply(400,str(error).encode())
        def log_message(self,*args):pass
    server=ThreadingHTTPServer(('127.0.0.1',a.port),Handler)
    print(f'JIZURA_BANK_EXPORT http://127.0.0.1:{a.port}/',flush=True)
    try:server.serve_forever()
    finally:
        server.server_close()
        for w in writers.values():w['file'].close()
if __name__=='__main__':main()
