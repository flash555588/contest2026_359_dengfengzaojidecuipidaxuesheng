"""Headless browser checks for the embedded portal and browser-side ZIP decoding."""
from pathlib import Path
from http.server import SimpleHTTPRequestHandler,ThreadingHTTPServer
from functools import partial
from threading import Thread
from playwright.sync_api import sync_playwright
import json,zipfile,io,argparse
ws=Path(__file__).resolve().parent.parent
src=ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop/portal'
out=ws/'04-v3-20260913/espdl-quickapp/evidence/portal-browser'
out.mkdir(exist_ok=True)
class Quiet(SimpleHTTPRequestHandler):
    def log_message(self,*args): pass
server=ThreadingHTTPServer(('127.0.0.1',8765),partial(Quiet,directory=str(src)))
Thread(target=server.serve_forever,daemon=True).start()
configs={'ai':{'backend':'openai_compatible','model':'','base_url':'https://api.openai.com/v1','system_prompt':'请简洁回答','timeout_ms':30000,'max_tokens':512,'api_key_set':False},'homeassistant':{'url':'http://homeassistant.local:8123','token_set':False},'music':{'api_key_set':False},'desktop':{'light':1,'palette':1,'widget':0,'reduced_motion':0}}
calls=[]
state={'code':'000042','token':'a'*32,'valid':True,'offline':False,'limited':False,'pairs':0}
def route_api(route):
    from urllib.parse import urlsplit,parse_qs
    req=route.request; parts=urlsplit(req.url);query=parse_qs(parts.query)
    if state['offline']:
        route.abort('connectionrefused');return
    if parts.path=='/api/pair':
        if state['limited']:
            route.fulfill(status=429,content_type='application/json',body=json.dumps({'error':'尝试次数较多，请稍后再试','retry_after':1}));return
        if json.loads(req.post_data)['code']!=state['code']:
            route.fulfill(status=401,content_type='application/json',body=json.dumps({'error':'令牌不正确，请核对设备屏幕上的 6 位数字'}));return
        state['pairs']+=1
        route.fulfill(content_type='application/json',body=json.dumps({'token':state['token'],'idle_seconds':900}));return
    if not state['valid'] or req.headers.get('authorization')!='Bearer '+state['token']:
        route.fulfill(status=401,content_type='application/json',body=json.dumps({'error':'连接已失效，请输入设备上的令牌或重新扫码'}));return
    if req.method=='GET':
        data=configs[query['section'][0]] if parts.path=='/api/config' else {'items':[]}
    else:
        calls.append((parts.path,req.post_data_buffer));data={'ok':True}
    route.fulfill(content_type='application/json',body=json.dumps(data))
try:
    with sync_playwright() as p:
        browser=p.chromium.launch(channel='msedge',headless=True)
        page=browser.new_page(viewport={'width':1440,'height':1000},device_scale_factor=1)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        page.route('**/api/**',route_api)
        base='http://127.0.0.1:8765/'
        page.goto(base)
        page.locator('#connect').wait_for(state='visible')
        assert page.locator('#workspace').is_hidden()
        page.screenshot(path=str(out/'connect-desktop.png'),full_page=True)
        page.set_viewport_size({'width':390,'height':844})
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        page.screenshot(path=str(out/'connect-mobile.png'),full_page=True)
        page.locator('#pair-code').fill('123456');page.locator('#pair-button').click()
        page.wait_for_function("document.querySelector('#status').textContent.includes('不正确')")
        assert page.locator('#workspace').is_hidden()
        state['limited']=True
        page.locator('#pair-button').click()
        page.wait_for_function("document.querySelector('#pair-button').textContent.includes('秒后重试')")
        assert page.locator('#pair-button').is_disabled()
        state['limited']=False
        page.wait_for_function("!document.querySelector('#pair-button').disabled")
        # Leading zeroes and a pasted grouping space must survive normalization.
        page.locator('#pair-code').fill('000 042')
        assert page.locator('#pair-code').input_value()=='000042'
        page.locator('#pair-button').click()
        page.wait_for_function("document.querySelector('#ai [name=max_tokens]').value==='512'")
        assert page.locator('#ai [name=backend]').input_value()=='openai_compatible'
        page.locator('#ai [name=model]').fill('fixture-model')
        page.locator('#ai [name=backend]').select_option('anthropic_compatible')
        page.locator('#ai button[type=submit]').click()
        page.wait_for_function("document.querySelector('#status').textContent.includes('已保存')")
        submitted=[json.loads(data) for path,data in calls if path=='/api/config']
        assert submitted[-1]['backend']=='anthropic_compatible'
        assert 'api_key' not in submitted[-1]
        page.locator('#workspace').wait_for(state='visible')
        pairs=state['pairs']
        page.reload();page.locator('#workspace').wait_for(state='visible')
        assert state['pairs']==pairs,'Reload must restore the session without pairing again'
        assert page.evaluate("sessionStorage.getItem('device-space-session-v1')") == state['token']
        desktop=page
        page=browser.new_page(viewport={'width':390,'height':844},device_scale_factor=1)
        page.on('pageerror',lambda e:errors.append(str(e)));page.route('**/api/**',route_api)
        page.goto(base+'#'+state['code']);page.locator('#workspace').wait_for(state='visible')
        assert '#' not in page.url
        for _ in range(12):
            pairs=state['pairs']
            page.goto(base+'#'+state['code'])
            page.wait_for_function("document.querySelector('#workspace').hidden===false && location.hash===''")
            assert state['pairs']==pairs+1
        desktop.reload();desktop.locator('#workspace').wait_for(state='visible')
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        page.screenshot(path=str(out/'config-mobile.png'),full_page=True)
        page.locator('[data-tab=files]').click();page.screenshot(path=str(out/'files-mobile.png'),full_page=True)
        page.locator('[data-tab=install]').click()
        # Use the downloaded, unmodified CDN response for repeatable offline QA.
        # The device does not embed or serve this dependency.
        cached=ws/'diagnostics/downloads/fflate-0.8.2.js'
        page.route('https://cdn.jsdelivr.net/npm/fflate@0.8.2/umd/index.js',lambda r:r.fulfill(path=str(cached),content_type='text/javascript'))
        archive=io.BytesIO()
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('manifest.json',json.dumps({'name':'浏览器测试','package':'org.test.browser','entry':'app.js'}))
            z.writestr('app.js','console.log("Hello");')
        page.locator('#package').set_input_files({'name':'fixture.qpk','mimeType':'application/zip','buffer':archive.getvalue()})
        page.locator('#preview').wait_for(state='visible')
        page.screenshot(path=str(out/'install-mobile.png'),full_page=True)
        page.locator('#install-button').click()
        page.wait_for_function("document.querySelector('#install-status').textContent.includes('安装完成')")
        assert any(path=='/api/install/file' and data==b'console.log("Hello");' for path,data in calls)
        large=io.BytesIO()
        with zipfile.ZipFile(large,'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('manifest.json',json.dumps({'name':'Large fixture','package':'test.large','entry':'app.js'}))
            z.writestr('app.js','/*'+'x'*(2*1024*1024)+'*/')
            for i in range(170): z.writestr(f'asset{i}.txt','fixture')
        page.locator('#package').set_input_files({'name':'large.zip','mimeType':'application/zip','buffer':large.getvalue()})
        page.wait_for_function("document.querySelector('#app-name').textContent==='Large fixture'")
        assert page.locator('#preview').is_visible()
        assert '172 个文件' in page.locator('#app-detail').inner_text()
        bad=io.BytesIO()
        with zipfile.ZipFile(bad,'w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('manifest.json','{}');z.writestr('../escape.js','bad')
        page.locator('#package').set_input_files({'name':'bad.zip','mimeType':'application/zip','buffer':bad.getvalue()})
        page.wait_for_function("document.querySelector('#status').textContent.includes('路径')")
        page.set_viewport_size({'width':1440,'height':1000});page.locator('[data-tab=config]').click()
        page.screenshot(path=str(out/'config-desktop.png'),full_page=True)
        state['valid']=False
        page.locator('[data-tab=files]').click()
        page.locator('#connect').wait_for(state='visible')
        page.wait_for_function("document.querySelector('#status').textContent.includes('已失效')")
        assert page.evaluate("sessionStorage.getItem('device-space-session-v1')") is None
        state.update(code='006789',token='b'*32,valid=True)
        page.locator('#pair-code').fill(state['code']);page.locator('#pair-button').click()
        page.locator('#workspace').wait_for(state='visible')
        state['offline']=True
        page.locator('[data-tab=files]').click()
        page.locator('#retry-connection').wait_for(state='visible')
        state['offline']=False;pairs=state['pairs']
        page.locator('#retry-connection').click();page.locator('#workspace').wait_for(state='visible')
        assert state['pairs']==pairs,'Network retry should retain valid authentication'
        page.locator('#disconnect').click();page.reload()
        page.locator('#connect').wait_for(state='visible')
        assert page.evaluate("sessionStorage.getItem('device-space-session-v1')") is None
        assert not errors,errors
        browser.close()
    report={'manual_pairing':True,'leading_zero_and_grouped_input':True,'reload_restores_session':True,
            'repeated_qr_entries':12,'old_browser_preserved':True,'expiry_and_network_recovery':True,
            'rate_limit_recovers':True,'real_fflate_install':True,'large_source_and_172_files':True,'page_errors':errors,'api':'deterministic local fixture'}
    (out/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS: manual/QR pairing, reload/session recovery, 12 rescans, expiry/reconnect, mobile/desktop layouts, real ZIP install and path validation')
finally:server.shutdown()
