"""Short read/preview smoke check. Sends no device control service call."""
import argparse,json,re,time
from pathlib import Path
from grok_board_io import Console
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'board-smoke-console.log').open('w',encoding='utf-8') as log:
    usb=Console(log=log)
    def status():
        for retry in range(5):
            m=re.search(r'^HA_STATUS (\{.*\})$',usb.command('desktop ui ha-status'),re.M)
            if m:
                try:
                    s=json.loads(m[1]);assert s['registeredTypes']==59 and s['registeredVariants']==127
                    assert {'connected','phase','sliders','cards','deviceCount','entityCount'}<=s.keys()
                    return s
                except (ValueError,KeyError,AssertionError):pass
        raise RuntimeError('Incomplete native HA status')
    try:
        usb.command('');usb.command('desktop ui ha');end=time.monotonic()+45
        while time.monotonic()<end:
            s=status()
            if s['connected'] and s['sliders']:break
            print('HA_STARTUP',s['phase'],s['message'],flush=True);time.sleep(1)
        else:raise RuntimeError('Live sliders not ready: '+s['message'])
        assert s['deviceCount']==7 and s['entityCount']>=195
        for retry in range(5):
            m=re.search(r'UI_LAYOUT_BEGIN\s*(\[.*?\])\s*UI_LAYOUT_END',usb.command('desktop ui audit'),re.S)
            try:
                nodes=json.loads(m[1]);root=next(n for n in nodes if n['parent']==1 and n['type']=='container' and n['w']>=640 and 400<=n['h']<600);break
            except (ValueError,TypeError,StopIteration):pass
        else:raise RuntimeError('Native root missing')
        rail=s['sliders'][0];target=80 if rail['value']<60 else 30
        def touch(kind,value):
            x=(rail['x']+rail['w']*value/100)/root['w'];y=(rail['y']+rail['h']/2)/root['h']
            assert re.search(r'->\s*1\b',usb.command(f'desktop ui touch:{kind}:{x:.6f}:{y:.6f}'))
        touch('down',25);touch('move',target)
        for retry in range(3):
            if str(target)+'%' in usb.command('desktop ui audit'):break
        else:raise AssertionError('Visible preview missing')
        touch('cancel',target);time.sleep(.5);after=status()
        assert after['roomPage']==s['roomPage'] and not after['detailOpen']
        result={'passed':True,'checks':['live Home Assistant connects','59 categories / 127 variants retained','7 physical registry groups retained','native slider previews and cancels without flipping pages'],
                'live_entities':after['entityCount'],'viewport':{'width':root['w'],'height':root['h']},'preview_percent':target,'no_control_command_sent':True}
        (a.output/'board-smoke.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print('RESPONSIVE_BOARD_SMOKE_OK',after['entityCount'],flush=True)
    finally:
        try:usb.command('desktop ui touch:cancel:0:0')
        except Exception:pass
        usb.close()
