"""Read-only board check: open More and preview/cancel Kelvin. No service writes."""
import argparse,json,re,time
from pathlib import Path
from grok_board_io import Console
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
with (a.output/'board-more-console.log').open('w',encoding='utf-8') as log:
    usb=Console(log=log)
    def status():
        for _ in range(5):
            m=re.search(r'^HA_STATUS (\{.*\})$',usb.command('desktop ui ha-status'),re.M)
            if m:
                try:
                    s=json.loads(m[1]);assert 'parameters' in s and 'functions' in s;return s
                except (ValueError,AssertionError):pass
        raise RuntimeError('Incomplete new HA status')
    def audit():
        for _ in range(4):
            m=re.search(r'UI_LAYOUT_BEGIN\s*(\[.*?\])\s*UI_LAYOUT_END',usb.command('desktop ui audit'),re.S)
            if m:
                try:return json.loads(m[1])
                except ValueError:pass
        raise RuntimeError('Incomplete native audit')
    def click(x,y):
        for _ in range(4):
            reply=usb.command(f'desktop ui click-at:{round(x)}:{round(y)}')
            if re.search(r'->\s*1\b',reply):time.sleep(.5);return
            assert re.search(r'->\s*0\b',reply),'Ambiguous click; will not replay'
        raise RuntimeError('Native hit target not ready')
    def touch(kind,r,percent,root):
        x=(r['x']+r['w']*(.5 if r.get('vertical') else percent/100))/root['w'];y=(r['y']+r['h']*(1-percent/100 if r.get('vertical') else .5))/root['h']
        assert re.search(r'->\s*1\b',usb.command(f'desktop ui touch:{kind}:{x:.6f}:{y:.6f}'))
    try:
        usb.command('');usb.command('desktop ui ha');end=time.monotonic()+60
        while time.monotonic()<end:
            s=status()
            if s['connected'] and s['phase']=='ready' and s['layout']:break
            time.sleep(1)
        else:raise RuntimeError('HA startup not ready')
        assert s['registeredTypes']==59 and s['registeredVariants']==127
        nodes=audit();root=next(n for n in nodes if n['parent']==1 and n['type']=='container' and n['w']>=640 and 400<=n['h']<600)
        # Device navigation restricts the page without changing saved sizes.
        click(60,64+134);time.sleep(1);s=status()
        lamp=next(c for c in s['cards'] if c['model']=='yeelink.light.lamp28')
        row=next(r for r in s['layout'] if r['entity_id']==lamp['entity'])
        if lamp['size']=='large':click(row['x']+row['w']/2,64+row['y']+row['h']-20)
        else:
            click(row['x']+90,64+row['y']+30);time.sleep(1)
            nodes=audit();more=next(n for n in nodes if n.get('text')=='更多操作')
            click(more['x']+more['w']/2,more['y']+more['h']/2)
        time.sleep(1);s=status();assert s['detailAdvanced']
        functions=s['functions'];assert any(f.get('key')=='color_temp_kelvin' for f in functions)
        rail=next(r for r in s['parameters'] if r['key']=='color_temp_kelvin')
        assert rail['max']>rail['min'];touch('down',rail,50,root);touch('move',rail,60,root)
        expected=round(rail['min']+(rail['max']-rail['min'])*.6)
        nodes=audit();assert any(n.get('text') in (str(expected)+'K','色温 '+str(expected)+'K') for n in nodes)
        touch('cancel',rail,60,root);s=status();assert s['phase']=='ready' and s['controlQueue']==0
        # All functions share the card; paging changes its content, never the device.
        count=0
        for _ in range(12):
            if any(r.get('key')=='value' for r in s['parameters']):break
            nodes=audit()
            nxt=next((n for n in nodes if n.get('text')=='>' and n['y']>480),None)
            if not nxt:break
            click(nxt['x']+nxt['w']/2,nxt['y']+nxt['h']/2);s=status();count+=1
            assert s['selectedId']==lamp['entity'],'paging changed the physical device'
        assert any(f.get('service')=='set_value' for f in s['functions']),'No numeric write control in grouped device'
        result={'passed':True,'checks':['live HA startup','59 categories / 127 variants','physical lamp More includes Kelvin range','native Kelvin preview and cancel','same device number set_value exposed'],
                'entity_count':s['entityCount'],'device_count':s['deviceCount'],'function_steps':count,'no_control_command_sent':True}
        (a.output/'board-more-smoke.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print('HA_MORE_BOARD_SMOKE_OK',s['entityCount'],flush=True)
    finally:
        try:usb.command('desktop ui touch:cancel:0:0');usb.command('desktop ui ha')
        except Exception:pass
        usb.close()
