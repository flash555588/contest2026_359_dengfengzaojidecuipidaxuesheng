#!/usr/bin/env python3
"""P4 smoke test with original Japanese/Korean lines and a Chinese sidecar.

Uses the existing public jizura-demo.mp3. Never overwrites a differing sidecar.
Only counts, state and original test text are logged; no private credentials.
"""
import argparse,json,re,time
from pathlib import Path
from music_device_console import Console

class LanguageConsole(Console):
    def _command_once(self,text,timeout):
        wire='desktop ui "'+text[11:]+'"' if text.startswith('desktop ui ') and ' ' in text[11:] else text
        assert len(wire.encode())<79,wire
        self.collect(.05);self.serial.write(b'\x15');self.collect(.1)
        self.log.write('\nCOMMAND '+text+'\n')
        for byte in (wire+'\r').encode():self.serial.write(bytes([byte]));time.sleep(.008)
        value='';end=time.monotonic()+timeout
        while time.monotonic()<end:
            value+=self.collect(.12)
            # USB/radio diagnostics occasionally lose the leading prompt byte.
            # Accept that prompt only, without replaying a dispatched action.
            if re.search(r'(?m)^(?:nsh|sh|h)>[ \t]*(?:\x1b\[K)?',value):return value
        raise TimeoutError(text)

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);result={'passed':False,'styles':[]}
    proof=a.output/'verification.json'
    def save():proof.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    save()
    with (a.output/'console.log').open('w',encoding='utf-8') as log:
        c=LanguageConsole(log)
        def ui(s):return c.command('desktop ui '+s)
        def music():
            for _ in range(4):
                text=ui('music-status');m=re.search(r'music state=(\d+) error=(-?\d+).*?position_ms=(\d+)',text)
                if m:return dict(state=int(m[1]),error=int(m[2]),ms=int(m[3]))
            raise AssertionError('Missing music status')
        try:
            for _ in range(20):
                font=ui('font-status')
                if 'font state=2 error=0' in font:break
                c.collect(.3)
            assert 'unicode=45695' in font and 'missing=0' in font and 'language_missing=0' in font,font
            result['font']=font;result['heap_before']=c.command('free');save()
            assert 'jizura-demo.mp3' in c.command('ls /sdcard/music/jizura-demo.mp3')
            directory=c.command('ls /sdcard/music/lyrics')
            if re.search(r'nsh:.*failed',directory,re.I):c.command('mkdir /sdcard/music/lyrics')
            fixtures={'Language Demo - Vela.lrc':['[00:00.00]愛の歌を聴こう','[00:04.00]안녕하세요 음악'],
                      'Language Demo - Vela.zh.lrc':['[00:00.00]聽一首愛的歌','[00:04.00]你好，音樂']}
            c.command('cd /sdcard/music/lyrics')
            for name,lines in fixtures.items():
                existing=c.command('cat "'+name+'"')
                if not re.search(r'nsh:.*failed',existing,re.I):
                    assert all(line in existing for line in lines),'Differing existing sidecar preserved'
                    continue
                for i,line in enumerate(lines):
                    reply=c.command('echo "'+line+'" '+('>' if i==0 else '>>')+' "'+name+'"')
                    assert not re.search(r'nsh:.*failed',reply,re.I),reply
            c.command('cd /')
            ui('home');ui('music');assert 'accepted=1' in ui('music-language-demo')
            deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                playing=music()
                assert not playing['error'],playing
                if playing['state']==2 and playing['ms']>0:break
                c.collect(.1)
            else:raise AssertionError('SD audio did not advance')
            ui('music-toggle');assert music()['state']==3
            lyrics=ui('music-lyrics-status');assert 'lines=2 translated=2 bilingual=1' in lyrics,lyrics
            result['lyrics']=lyrics
            assert '-> 1' in ui('click:全屏歌词')
            assert '-> 1' in ui('click:双语');assert 'bilingual=0' in ui('music-lyrics-status')
            assert '-> 1' in ui('click:原文');assert 'bilingual=1' in ui('music-lyrics-status')
            assert '-> 1' in ui('click:文字 PV')
            names=['聚焦 · 1/3','强调 · 2/3','逐字 · 3/3']
            for style,name in enumerate(names):
                status=ui('music-pv-status')
                assert 'active=1 native=1 style='+str(style) in status,status
                result['styles'].append(status);save()
                if style<2:assert '-> 1' in ui('click:'+name)
            # Advance to the second line to rasterize Korean on the real panel.
            ui('music-toggle');deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                playing=music()
                if playing['state']==2 and playing['ms']>=4400:break
                assert playing['state']!=6,playing
            else:raise AssertionError('Korean line not reached')
            ui('music-toggle');assert music()['state']==3
            korean=ui('music-pv-status');assert 'line=1' in korean,korean
            result['korean_line']=korean
            result['heap_after']=c.command('free')
            assert not re.search(r'I2S transfer failed|Failed to allocate the DMA', (a.output/'console.log').read_text(encoding='utf-8'))
            result['passed']=True;save();print('MUSIC_LANGUAGES_DEVICE_OK: fonts, exact sidecar, bilingual toggle, 3 PV styles, Korean rasterization and PCM clock',flush=True)
        except Exception as error:result['error']=repr(error);save();raise
        finally:c.close()

if __name__=='__main__':main()
