#!/usr/bin/env python3
"""Physical P4 checks for the three live LRC effects (not SD frame playback)."""
import argparse,json,re,time
from pathlib import Path
from test_jizura_device import Console

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--phase',choices=['styles','cycles'],required=True);p.add_argument('--cycles',type=int,default=60);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);proof=a.output/'verification.json'
    result=json.loads(proof.read_text(encoding='utf-8')) if proof.exists() else {}
    phase={'passed':False,'samples':[]};result[a.phase]=phase
    def save():proof.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    save()
    with (a.output/(a.phase+'.log')).open('w',encoding='utf-8') as log:
        c=Console(log)
        def status():
            for _ in range(4):
                text=c.command('desktop ui music-pv-status')
                m=re.search(r'music-pv active=\d+ native=1[^\n]*',text)
                if m:
                    s={k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',m[0])}
                    if {'active','native','style','line','position_ms','plans','plan_max_ms','updates','buffers'}<=s.keys():
                        assert s['buffers']==0;return s
            raise AssertionError('Incomplete read-only native status')
        def click(name):assert '-> 1' in c.command('desktop ui click:'+name),name
        def open_pv():
            c.command('desktop ui home');c.command('desktop ui music');click('全屏歌词');click('文字 PV')
        def active():
            deadline=time.monotonic()+6
            while time.monotonic()<deadline:
                s=status()
                if s['active']:return s
                c.collect(.1)
            raise AssertionError('Live lyrics did not become ready')
        def heap():
            for _ in range(4):
                rows={}
                for m in re.finditer(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(Umem|Kmem)',c.command('free'),re.M):
                    rows[m[8]]=dict(zip(['total','used','free','maxused','maxfree','nused','nfree'],map(int,m.groups()[:7])))
                if len(rows)==2:return rows
            raise AssertionError('Incomplete heap status')
        try:
            c.command('desktop ui music-stop');open_pv()
            if a.phase=='styles':
                for i,name in enumerate(['聚焦 · 1/3','强调 · 2/3','逐字 · 3/3']):
                    if i:click(['聚焦 · 1/3','强调 · 2/3'][i-1])
                    assert 'accepted=1' in c.command('desktop ui music-pv-demo')
                    s=active();assert s['style']==i and s['plans']>0
                    c.command('desktop ui render-metrics-reset');c.collect(1.4)
                    metrics=c.command('desktop ui render-metrics');assert 'UI_RENDER_METRICS elapsed_ms=' in metrics
                    s=active();c.command('desktop ui music-toggle')
                    before=active();c.collect(.6);after=active()
                    assert before['position_ms']==after['position_ms'] and before['updates']==after['updates']
                    assert before['plans']==after['plans']
                    phase['samples'].append({'style':i,'status':s,'paused':after,'render':metrics,'heap':heap()});save()
                    c.command('desktop ui music-stop');print('NATIVE_STYLE_OK',i,flush=True)
            else:
                assert 'accepted=1' in c.command('desktop ui music-pv-demo');active();c.command('desktop ui music-toggle')
                active();click('返回音乐');c.command('desktop ui home');phase['baseline']=heap();save()
                for i in range(a.cycles):
                    open_pv();s=active();assert s['plans']==1
                    for label in ['聚焦 · 1/3','强调 · 2/3']:click(label)
                    assert active()['style']==2
                    click('返回音乐');c.command('desktop ui home')
                    if (i+1)%10==0:
                        phase['samples'].append({'cycle':i+1,'heap':heap()});save();print('NATIVE_CYCLE_OK',i+1,flush=True)
                phase['completed_cycles']=a.cycles
            phase['passed']=True;save()
        except Exception as error:
            phase['error']=repr(error);save();raise
        finally:c.close()
    print('MUSIC_NATIVE_DEVICE_OK',a.phase,flush=True)
if __name__=='__main__':main()
