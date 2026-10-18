#!/usr/bin/env python3
"""Physical P4 tests: checked SD frames, audio clock, lifecycle and recovery."""
import argparse,hashlib,json,re,struct,subprocess,sys,time,zlib
from pathlib import Path
import serial

class Console:
    def __init__(self,log):
        self.log=log;self.serial=serial.Serial(port=None,baudrate=115200,timeout=.08,write_timeout=3)
        self.serial.dtr=self.serial.rts=False;self.serial.port='COM4';self.serial.open()
    def collect(self,seconds):
        data=bytearray();end=time.monotonic()+seconds
        while time.monotonic()<end:
            block=self.serial.read(self.serial.in_waiting or 1)
            if block:data+=block
        text=data.decode(errors='replace').replace('\r','');self.log.write(text);self.log.flush()
        if re.search(r'PANIC|Assertion failed|(?:Load|Store) access fault|Unhandled exception',text,re.I):raise RuntimeError('Board runtime fault')
        return text
    def command(self,text,timeout=30):
        for _ in range(4):
            value=self._command_once(text,timeout)
            rejected=re.search(r'(?m)^nsh: [^:\n]+: command not found\s*$',value)
            if not rejected:return value
            self.log.write('CONSOLE_REJECTED_BEFORE_DISPATCH: retrying command\n');self.log.flush();self.collect(.3)
        raise RuntimeError('Console rejected command before dispatch: '+text)
    def _command_once(self,text,timeout):
        wire=text
        if text.startswith('desktop ui ') and ' ' in text[11:]:wire='desktop ui "'+text[11:]+'"'
        assert len(wire.encode())<79,wire
        self.collect(.05);self.serial.write(b'\x15');self.collect(.1);self.log.write('\nCOMMAND '+text+'\n')
        # NSH treats CR and LF as separate submissions; one CR avoids a stale
        # empty-command prompt being mistaken for the next command's result.
        for b in (wire+'\r').encode():self.serial.write(bytes([b]));time.sleep(.008)
        value='';end=time.monotonic()+timeout
        while time.monotonic()<end:
            value+=self.collect(.12)
            if 'nsh>' in value:return value
        raise TimeoutError(text)
    def close(self):self.serial.close()

def main():
    p=argparse.ArgumentParser();p.add_argument('--banks',type=Path,required=True);p.add_argument('--catalog',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--decoded-reference',type=Path)
    p.add_argument('--resume-styles',action='store_true')
    p.add_argument('--phase',choices=['smoke','hashes','styles','cycles','fallback','reboot','final'],required=True);p.add_argument('--cycles',type=int,default=60);p.add_argument('--style-limit',type=int,default=27);a=p.parse_args()
    assert 1<=a.style_limit<=27
    a.output.mkdir(parents=True,exist_ok=True);proof=a.output/'verification.json'
    result=json.loads(proof.read_text(encoding='utf-8')) if proof.exists() else {'speaker_human_confirmed':False,'phases':{}}
    styles=json.loads(a.catalog.read_text(encoding='utf-8'))['styles'];manifest=json.loads((a.banks/'manifest.json').read_text(encoding='utf-8'))
    sd_directory='/sdcard/jizura/pv-jpeg' if manifest.get('codec_kind') in ('jpeg','indexed8') else '/sdcard/jizura/pv'
    decoded=json.loads(a.decoded_reference.read_text()) if a.decoded_reference else None
    def save():proof.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    began=time.monotonic();phase=result['phases'].get('styles',{}) if a.resume_styles and a.phase=='styles' else {}
    phase['passed']=False;phase.pop('error',None);run={};phase.setdefault('runs',[]).append(run);result['phases'][a.phase]=phase;save()
    with (a.output/(a.phase+'.log')).open('a' if a.resume_styles else 'w',encoding='utf-8') as log:
        c=Console(log)
        def music():
            for _ in range(4):
                text=c.command('desktop ui music-status');m=re.search(r'music state=(\d+) error=(-?\d+).*?position_ms=(\d+)',text)
                if m:return dict(state=int(m[1]),error=int(m[2]),ms=int(m[3]))
            raise AssertionError('Incomplete read-only music status: '+text)
        def pv():
            for _ in range(4):
                text=c.command('desktop ui music-pv-status');m=re.search(r'music-pv active=(\d+)([^\n]*)',text)
                if not m:continue
                state={key:int(value) for key,value in re.findall(r'(\w+)=(-?\d+)',m[0])}
                if 'reads' not in state:
                    if not state.get('active') and 'failed' in state:return state
                    continue
                if {'active','style','frame','fps','reads','displayed','skipped','mean_us','max_us','io_us','check_us','buffers','error'}<=state.keys() and state['buffers']==921600:return state
            raise AssertionError('Incomplete read-only PV status: '+text)
        def active():
            end=time.monotonic()+8
            while time.monotonic()<end:
                s=pv()
                if s['active']:assert s['error']==0;return s
                c.collect(.1)
            raise AssertionError('SD frame never became visible: '+str(s))
        def click(label):assert '-> 1' in c.command('desktop ui click:'+label),label
        def open_player():
            c.command('desktop ui home');c.command('desktop ui music');click('全屏歌词');click('文字 PV')
        def choose(index):
            label=None
            for _ in range(28):
                state=pv();current=state['style']
                if current==index:return
                if label is None:label=styles[current]['name']
                reply=c.command('desktop ui click:'+label)
                if '-> 0' in reply and label!='SD 风格':
                    label='SD 风格';reply=c.command('desktop ui click:'+label)
                assert '-> 1' in reply,reply
            raise AssertionError('Style selector did not reach '+str(index))
        def heap():
            text=c.command('free');rows={}
            for m in re.finditer(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(Umem|Kmem)',text,re.M):
                rows[m[8]]=dict(zip(['total','used','free','maxused','maxfree','nused','nfree'],map(int,m.groups()[:7])))
            assert len(rows)==2,text;return rows
        def capture(name,state):
            c.command('desktop ui frame');reply=c.command('desktop frame-export',240)
            m=re.search(r'FRAME_DATA (\d+)',reply);assert m;size=int(m[1]);data=bytearray(size);received=set()
            def collect(value):
                for row in re.finditer(r'^F([a-f0-9]{8}):([a-f0-9]+)$',value,re.M):
                    if len(row[2])%2:continue
                    at=int(row[1],16);raw=bytes.fromhex(row[2]);expected=min(64,size-at)
                    if 0<=at<size and at%64==0 and len(raw)==expected:data[at:at+len(raw)]=raw;received.add(at)
            collect(reply)
            for at in range(0,size,64):
                for retry in range(4):
                    if at in received:break
                    collect(c.command('desktop frame-chunk '+str(at)))
                assert at in received,at
            c.command('desktop ui frame-free')
            pixels=bytearray()
            for count,color in struct.iter_unpack('<HH',data):pixels+=struct.pack('<H',color)*count
            assert len(pixels)==1024*600*2
            crop=b''.join(pixels[((78+y)*1024+192)*2:((78+y)*1024+832)*2] for y in range(360))
            filename=next(n for n in manifest['files'] if n.endswith(f'-{state["style"]:02d}.jpv'))
            with (a.banks/filename).open('rb') as f:f.seek(288+state['frame']*12+8);expected=struct.unpack('<I',f.read(4))[0]
            if decoded:expected=decoded[filename][state['frame']]
            assert zlib.adler32(crop)==expected,'Displayed image differs from source RGB565 frame'
            (a.output/(name+'.rgb565')).write_bytes(pixels)
            rgb=bytearray()
            for color, in struct.iter_unpack('<H',pixels):rgb.extend(((color>>11)*255//31,((color>>5)&63)*255//63,(color&31)*255//31))
            def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
            scan=b''.join(b'\0'+rgb[y*3072:(y+1)*3072] for y in range(600))
            png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1024,600,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan,6))+chunk(b'IEND',b'')
            (a.output/(name+'.png')).write_bytes(png)
            return dict(actual_framebuffer=True,source_rgb565_adler_equal=True,style=state['style'],frame=state['frame'],rgb565_sha256=hashlib.sha256(pixels).hexdigest())
        try:
            c.command('');assert '/sdcard type vfat' in c.command('mount')
            if a.phase=='reboot':
                c.command('desktop ui music-stop');c.command('desktop ui home');c.collect(2);c.close()
                with (a.output/'reset.log').open('w') as reset:
                    subprocess.run([sys.executable,'-m','esptool','--chip','esp32p4','--port','COM4','--before','usb-reset','--after','watchdog-reset','run'],stdout=reset,stderr=subprocess.STDOUT,check=True,timeout=35)
                end=time.monotonic()+30
                while True:
                    try:c=Console(log);break
                    except serial.SerialException:
                        if time.monotonic()>end:raise
                        time.sleep(.3)
                c.collect(5);c.command('');assert 'esp32p4-function-ev-board' in c.command('uname -a')
                mounts=c.command('mount');assert '/data type smartfs' in mounts and '/sdcard type vfat' in mounts
                open_player();c.command('desktop ui music-pv-demo');phase['sd_after_reset']=active();assert music()['state']==2
                phase['heap_after_reset']=heap();c.command('desktop ui music-stop')
            elif a.phase=='hashes':
                phase['files']=[]
                for index in range(27):
                    for _ in range(4):
                        reply=c.command('desktop jizura-verify '+str(index),180);m=re.search(r'JIZURA_VERIFY style=(\d+) sha256=([a-f0-9]{64})',reply)
                        if m:break
                    assert m and int(m[1])==index,reply
                    expected=next(v['sha256'] for n,v in manifest['files'].items() if n.endswith(f'-{index:02d}.jpv'));assert m[2]==expected
                    phase['files'].append(index);save();print('BOARD_SD_SHA256_OK',index+1,27,flush=True)
            elif a.phase in ('smoke','final'):
                open_player();c.command('desktop ui music-pv-demo');s=active();assert music()['state']==2
                c.command('desktop ui music-toggle');c.collect(.4);before=music();s=active();reads=s['reads'];frame=s['frame'];c.collect(2);after=music();still=pv()
                assert before['state']==after['state']==3 and abs(after['ms']-before['ms'])<=100
                assert still['frame']==frame and still['reads']==reads
                phase['paused']={'before':before,'after':after,'sd':still};phase['heap_playing_paused']=heap()
                phase['capture']=capture(a.phase,still);print('BOARD_FRAME_SOURCE_EQUAL',flush=True)
                if a.phase=='smoke':
                    c.command('desktop ui music-toggle');c.collect(1);resumed=music();assert resumed['state']==2 and resumed['ms']>after['ms'];phase['resumed']=resumed
                    c.command('desktop ui music-stop')
            elif a.phase=='styles':
                open_player();phase.setdefault('styles',[]);start=len(phase['styles']);run['first_style']=start
                assert all(row['style']==i for i,row in enumerate(phase['styles']))
                for index in range(start,a.style_limit):
                    choose(index);c.command('desktop ui render-metrics-reset');assert 'accepted=1' in c.command('desktop ui music-pv-demo');first=active();clock=time.monotonic();c.collect(2.5);last=active();elapsed=time.monotonic()-clock;playing=music()
                    assert last['style']==index and last['frame']>first['frame'] and last['displayed']>first['displayed']
                    assert playing['state']==2 and playing['error']==0
                    fps=(last['displayed']-first['displayed'])/elapsed;assert fps>=8,('Playback fell below 8 fps',index,fps,last)
                    metrics=c.command('desktop ui render-metrics')
                    phase['styles'].append(dict(style=index,first=first,last=last,audio=playing,observed_fps=round(fps,2),ui_metrics=metrics));save();print('BOARD_STYLE_PLAY_OK',index+1,27,last['displayed'],last['mean_us'],round(fps,2),flush=True)
                    c.command('desktop ui music-stop');c.collect(.6)
                c.command('desktop ui music-stop');click('返回音乐')
            elif a.phase=='cycles':
                open_player();c.command('desktop ui music-pv-demo');active();c.command('desktop ui music-stop');click('返回音乐');c.collect(2)
                phase['baseline']=heap();phase['samples']=[]
                for n in range(a.cycles):
                    c.command('desktop ui music-pv-demo');click('全屏歌词');click('文字 PV');active();click('返回音乐')
                    assert music()['state']==2
                    c.command('desktop ui music-stop');c.collect(.15)
                    if (n+1)%10==0:
                        sample=heap();phase['samples'].append(dict(cycle=n+1,heap=sample));save();print('BOARD_PV_REOPEN_OK',n+1,a.cycles,sample['Umem']['free'],flush=True)
                c.collect(2);phase['final']=heap()
                phase['user_heap_growth']=phase['final']['Umem']['used']-phase['baseline']['Umem']['used']
                phase['kernel_heap_growth']=phase['final']['Kmem']['used']-phase['baseline']['Kmem']['used']
                assert phase['user_heap_growth']<65536 and phase['kernel_heap_growth']<2048,phase
            elif a.phase=='fallback':
                c.command('desktop ui music-stop');open_player();choose(0);c.command('cd '+sd_directory)
                filename=next(n for n in manifest['files'] if n.endswith('-00.jpv'));backup=filename+'.saved'
                assert not re.search(r'(?im)^\s*'+re.escape(backup)+r'\s*$',c.command('ls'))
                c.command('mv '+filename+' '+backup)
                try:
                    c.command('desktop ui music-pv-demo');c.collect(1.5);status=pv();audio=music()
                    assert status['active']==0 and status['failed']==1 and audio['state']==2 and audio['error']==0
                    phase['missing_asset']={'pv':status,'audio':audio}
                finally:c.command('mv '+backup+' '+filename);c.command('cd /')
                c.command('desktop ui music-pv-demo');phase['restored']=active();c.command('desktop ui music-stop')
            phase['passed']=True;run['seconds']=round(time.monotonic()-began,2);phase['seconds']=sum(item.get('seconds',0) for item in phase['runs']);save();print('JIZURA_BOARD_PHASE_OK',a.phase,phase['seconds'],flush=True)
        except Exception as error:
            phase['error']=str(error);run['error']=str(error);run['seconds']=round(time.monotonic()-began,2);save();print('JIZURA_BOARD_PHASE_FAILED',a.phase,str(error),flush=True)
            try:c.command('desktop ui music-stop')
            except Exception:pass
            raise
        finally:c.close()
if __name__=='__main__':main()
