"""Verify paired HA configuration and live sync; never log stored credentials."""
import argparse,http.client,json,re,time
from pathlib import Path
from grok_board_io import Console

p=argparse.ArgumentParser();p.add_argument('--board',default='192.168.31.33:8080');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);results=[]
with (a.output/'board-console.log').open('w',encoding='utf-8') as log:
    usb=Console(log=log)
    def request(method,data=None):
        c=http.client.HTTPConnection(a.board,timeout=20)
        c.request(method,'/api/config?section=homeassistant',None if data is None else json.dumps(data),
                  {'Authorization':'Bearer '+auth,'Content-Type':'application/json'})
        r=c.getresponse();status=r.status;payload=json.loads(r.read());c.close();return status,payload
    def record(name,**data):
        results.append({'test':name,'passed':True,**data});print('BOARD_HA_OK',name,data,flush=True)
    def audit():
        raw=usb.command('desktop ui audit');match=re.search(r'UI_LAYOUT_BEGIN\s*(\[.*?\])\s*UI_LAYOUT_END',raw,re.S)
        assert match,'Missing UI audit';return json.loads(match[1])
    def synced(timeout=25):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            nodes=audit();labels=[v['text'] for v in nodes if v['type']=='label']
            if '已同步' in labels:
                assert not any('超过 64 KiB' in v for v in labels)
                assert any(re.search(r'实体\s+[1-9]',v) for v in labels),labels
                return labels
            time.sleep(1)
        raise RuntimeError('HA did not synchronize: '+repr(labels))
    try:
        usb.command('');reply=usb.command('desktop portal-pair',private=True)
        code=re.search(r'PORTAL_PAIR=(\d{6})',reply);assert code
        c=http.client.HTTPConnection(a.board,timeout=20);c.request('POST','/api/pair',json.dumps({'code':code[1]}),{'Content-Type':'application/json'})
        r=c.getresponse();assert r.status==200;auth=json.loads(r.read())['token'];c.close();del reply,code
        status,original=request('GET');assert status==200 and original['url']=='http://124.223.30.246:8123' and original['token_set']
        record('saved_endpoint_and_token_after_flash',url=original['url'],token_set=True)
        usb.command('desktop ui ha');labels=synced();record('real_server_device_list_loaded',counts=[v for v in labels if re.match(r'(实体|在线|运行)\s+\d+',v)])
        assert request('POST',{'url':'http://124.223.30.246/8123'})[0]==400
        assert request('GET')[1]==original;record('invalid_slash_port_preserves_saved_config')
        assert request('POST',{'token':'x'*512})[0]==400
        assert request('GET')[1]==original;record('oversize_token_rejected_without_change')
        assert request('POST',{'url':'http://127.0.0.1:9'})[0]==200
        time.sleep(2);labels=[v['text'] for v in audit() if v['type']=='label']
        assert '已同步' not in labels;record('open_page_invalidates_old_home_on_remote_change')
        assert request('POST',{'url':original['url']})[0]==200
        labels=synced();assert request('GET')[1]==original
        record('restore_url_without_token_reconnects_open_page',token_preserved=True)
        usb.command('desktop ui qpk-capacity-status');usb.command('free')
        (a.output/'board-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
        print('HA_CONFIG_BOARD_TESTS_PASSED',flush=True)
    finally:
        if 'auth' in locals() and 'original' in locals():
            try:request('POST',{'url':original['url']})
            except Exception:pass
        usb.close()
