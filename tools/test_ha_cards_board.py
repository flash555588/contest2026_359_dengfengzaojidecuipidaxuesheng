"""Check real grouped card bindings and native slider input; never print tokens."""
import argparse,json,re,time
from pathlib import Path
from grok_board_io import Console

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);results=[]
with (a.output/'board-card-console.log').open('w',encoding='utf-8') as log:
    usb=Console(log=log);viewport=None;initial_entity=None;initial_size=None
    def status():
        for retry in range(5):
            reply=usb.command('desktop ui ha-status');m=re.search(r'^HA_STATUS (\{.*\})$',reply,re.M)
            if m:
                try:
                    s=json.loads(m[1]);required={'id','device','entity','model','size','type','variant','members'}
                    expected={'ynsur.lock.v12m','chuangmi.camera.079ac1','yeelink.light.lamp28','yeelink.light.bslamp2'}
                    if '\ufffd' not in m[1] and expected<={c.get('model') for c in s['cards']} and all(required<=set(c) and c['size'] in {'small','large'} and
                          (not c['device'] or c['id']=='device.'+c['device']) for c in s['cards']):return s
                except json.JSONDecodeError:pass  # USB console can drop bytes in a long line.
            time.sleep(.5)
        raise RuntimeError('Missing complete HA status')
    def ready():
        end=time.monotonic()+35
        while time.monotonic()<end:
            s=status()
            if s['connected'] and s['phase']=='ready' and not s['message'].startswith('正在'):return s
            time.sleep(1)
        raise RuntimeError('HA did not become ready')
    def record(name,**data):
        results.append({'test':name,'passed':True,**data});print('BOARD_CARD_OK',name,data,flush=True)
    def click(x,y):
        # HA cached data can be ready before the native button pool is drawn.
        # Retry only a confirmed no-hit; never replay a successful toggle.
        for retry in range(5):
            reply=usb.command(f'desktop ui click-at:{round(x)}:{round(y+64)}')
            if re.search(r'->\s*1\b',reply):time.sleep(.6);return
            assert re.search(r'->\s*0\b',reply),reply
            time.sleep(.3)
        raise AssertionError('Native hit target not ready')
    def touch(kind,rail,percent):
        # The app occupies the panel's inner area (1022 px on this board),
        # not the 1024 px framebuffer. Use the native root's actual geometry.
        x=(rail['x']+rail['w']*percent/100)/viewport['w'];y=(rail['y']+rail['h']/2)/viewport['h']
        reply=usb.command(f'desktop ui touch:{kind}:{x:.6f}:{y:.6f}');assert '-> 1' in reply
    def set_size(entity,size):
        s=status()
        if next(c for c in s['cards'] if c['entity']==entity)['size']!=size:
            if not s['layoutEditing']:click(900,30)
            row=next(r for r in status()['layout'] if r['entity_id']==entity)
            click(row['x']+row['w']/2,row['y']+row['h']-32)
            assert next(c for c in status()['cards'] if c['entity']==entity)['size']==size
        if status()['layoutEditing']:click(900,30)
    def visible_slider(entity):
        end=time.monotonic()+12
        while time.monotonic()<end:
            s=ready();rail=next((r for r in s['sliders'] if r['entity']==entity),None)
            if rail:return s,rail
            time.sleep(.5)
        raise RuntimeError('Native slider did not become visible: '+entity)
    def visible_text(text):
        for retry in range(3):
            if text in usb.command('desktop ui audit'):return
        raise AssertionError('Native text not found: '+text)
    try:
        usb.command('');usb.command('desktop ui ha');s=ready()
        for retry in range(5):
            try:
                audit=usb.command('desktop ui audit');m=re.search(r'UI_LAYOUT_BEGIN\s*(\[.*?\])\s*UI_LAYOUT_END',audit,re.S)
                nodes=json.loads(m[1]);viewport=next(n for n in nodes if n['parent']==1 and n['type']=='container' and n['w']>=640 and 400<=n['h']<600)
                break
            except (TypeError,ValueError,StopIteration,KeyError):pass
        assert viewport,'Native app viewport not found'
        if s['layoutEditing']:click(900,30);s=ready()
        assert s['registeredTypes']==59 and s['registeredVariants']==127
        record('all_registered_styles_retained',categories=59,variants=127)
        assert s['deviceCount']==7 and s['entityCount']>=195
        record('real_registry_groups_all_live_device_functions',devices=s['deviceCount'],live_entities=s['entityCount'])
        camera=next(c for c in s['cards'] if c['model']=='chuangmi.camera.079ac1')
        lock=next(c for c in s['cards'] if c['model']=='ynsur.lock.v12m')
        lamp=next(c for c in s['cards'] if c['model']=='yeelink.light.lamp28')
        original_size=lamp['size']
        initial_entity=lamp['entity'];initial_size=original_size
        assert camera['type']=='camera' and camera['entity'].endswith('_on_p_2_1') and camera['members']==100
        assert lock['type']=='lock' and lock['entity'].endswith('_door_state_p_10_1021') and lock['members']==39
        assert lamp['type']=='light' and lamp['reference']==21 and lamp['members']==37
        record('device_models_select_their_own_styles',camera=camera,lock=lock,lamp=lamp)
        # Native navigation button, not an artificial state assignment.
        click(60,134);s=ready()
        assert any(r['entity_id']==lamp['entity'] for r in s['layout'])
        if lamp['size']!='large':
            click(900,30);time.sleep(.5);assert status()['layoutEditing']
            editor=status();assert not editor['sliders']
            row=next(r for r in editor['layout'] if r['entity_id']==lamp['entity'])
            click(row['x']+row['w']/2,row['y']+row['h']-32);time.sleep(1.5)
            changed=status();assert next(c for c in changed['cards'] if c['id']==lamp['id'])['size']=='large'
            assert changed['layout']==editor['layout'],'editor tiles must stay in place'
            record('explicit_size_button_keeps_editor_grid_stable')
            click(900,30);time.sleep(1)
        s=ready();rail=next(r for r in s['sliders'] if r['entity']==lamp['entity'])
        assert not s['detailOpen'] and next(c for c in s['cards'] if c['id']==lamp['id'])['size']=='large'
        original=rail['value'];page=s['roomPage']
        touch('down',rail,original);touch('move',rail,50)
        # Snapshot plus actual visible native text proves preview happened
        # before release; no HA service request should be in progress.
        s=status();assert s['roomPage']==page and s['phase']=='ready'
        visible_text('亮度 50%')
        touch('cancel',rail,50);s=ready()
        record('native_slider_previews_without_committing_or_changing_page',preview_percent=50,page=page)
        # Long audit output can straddle a periodic refresh. Start the next
        # drag in a fresh idle sync interval and check the native preview.
        click(995,30);s=ready();time.sleep(.6);s=ready()
        # Release the original value: validate real HA service and verification
        # without deliberately changing room brightness or power state.
        rail=next(r for r in s['sliders'] if r['entity']==lamp['entity'])
        touch('down',rail,original);touch('move',rail,original);touch('up',rail,original)
        time.sleep(1.5);s=ready();assert s['roomPage']==page and s['selectedId']==lamp['entity']
        assert s['message'].startswith('状态已达到目标：'),s['message']
        assert next(r for r in s['sliders'] if r['entity']==lamp['entity'])['value']==original
        record('release_controls_and_verifies_the_same_lamp',entity=lamp['entity'],percent=original,phase=s['phase'])
        row=next(r for r in s['layout'] if r['entity_id']==lamp['entity'])
        click(row['x']+row['w']/2,row['y']+row['h']-12);s=status();assert s['detailAdvanced']
        usb.command('desktop ui click:下一功能');time.sleep(.5);s=status()
        assert s['selectedId']!=lamp['entity']
        record('more_opens_separate_member_entity',entity=s['selectedId'],members=lamp['members'])
        click(60,134);s=ready();assert not s['detailOpen']
        usb.command('desktop ui home');usb.command('desktop ui ha');s=ready()
        assert next(c for c in s['cards'] if c['model']=='yeelink.light.lamp28')['size']=='large'
        record('device_large_size_survives_reopening')
        click(60,134);s=ready()
        # Use an online light with a reported brightness. An off bedside light
        # may omit brightness, so do not power it on just to run this test.
        set_size(lamp['entity'],'small');s,small=visible_slider(lamp['entity']);assert small['h']==64
        original_small=small['value'];touch('down',small,original_small);touch('move',small,50)
        visible_text('50%')
        touch('cancel',small,50);ready()
        record('reference_small_tile_previews_the_same_online_lamp',entity=lamp['entity'],track_height=64,preview_percent=50)
        click(995,30);ready();time.sleep(.6);s,small=visible_slider(lamp['entity'])
        touch('down',small,original_small);touch('move',small,original_small);touch('up',small,original_small)
        time.sleep(1.5);s=ready();assert s['message'].startswith('状态已达到目标：') and s['selectedId']==lamp['entity']
        assert next(r for r in s['sliders'] if r['entity']==lamp['entity'])['value']==original_small
        assert not s['detailOpen']
        record('small_slider_release_verifies_the_correct_entity',entity=lamp['entity'],percent=original_small)
        set_size(lamp['entity'],original_size)
        assert next(c for c in status()['cards'] if c['id']==lamp['id'])['size']==original_size
        record('original_layout_preference_restored',size=original_size)
        usb.command('free')
        (a.output/'board-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
        print('HA_CARDS_BOARD_TESTS_PASSED',flush=True)
    finally:
        try:usb.command('desktop ui touch:cancel:0:0')
        except Exception:pass
        if initial_entity:
            try:
                click(60,134);set_size(initial_entity,initial_size)
            except Exception as e:print('LAYOUT_CLEANUP_WARNING',type(e).__name__,flush=True)
        usb.close()
