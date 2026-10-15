"""Read HA device relationships using the production fixed template; no secrets logged."""
import http.client,json,re,sys,time
from pathlib import Path
from grok_board_io import Console
from urllib.parse import urlsplit

out=Path(sys.argv[1]);out.mkdir(exist_ok=True,parents=True)
source=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/hass/hass_devices_template.h'
text=source.read_text();strings=re.findall(r'"(?:\\.|[^"\\])*"',text.split('g_hass_devices_template[] =')[1])
body=''.join(json.loads(s) for s in strings)
usb=Console()
try:
    usb.command('');reply=usb.command('desktop portal-pair',private=True);code=re.search(r'PORTAL_PAIR=(\d{6})',reply);assert code;time.sleep(1)
    def portal(method,path,data=None):
        c=http.client.HTTPConnection('192.168.31.33',8080,timeout=20);headers={'Content-Type':'application/json'}
        if 'auth' in globals():headers['Authorization']='Bearer '+auth
        c.request(method,path,None if data is None else json.dumps(data),headers);r=c.getresponse();payload=r.read();c.close();assert r.status==200;return json.loads(payload)
    auth=portal('POST','/api/pair',{'code':code[1]})['token'];del reply,code
    saved=portal('GET','/api/file?path=data/config/@2pportal/khomeassistant/value');url=urlsplit(saved['url'])
    c=http.client.HTTPConnection(url.hostname,url.port or 8123,timeout=20)
    c.request('POST','/api/template',body,{'Authorization':'Bearer '+saved['token'],'Content-Type':'application/json'})
    r=c.getresponse();payload=r.read();c.close();assert r.status==200,'Device query HTTP '+str(r.status)
    rows=json.loads(payload);assert isinstance(rows,list)
    summary={'device_count':len(rows),'registered_entity_count':sum(len(r[3]) for r in rows),'response_bytes':len(payload),
             'devices':[{'device_id':r[0],'name':r[1],'area':r[2],'entities':r[3],
                         'model':r[4] if len(r)>4 else '', 'manufacturer':r[5] if len(r)>5 else ''} for r in rows]}
    (out/'real-device-registry.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print('REAL_HA_DEVICE_REGISTRY_OK',len(rows),'devices',summary['registered_entity_count'],'entities',len(payload),'bytes')
    print('MULTI_ENTITY_DEVICES',sum(len(r[3])>1 for r in rows))
    for r in summary['devices']:print(json.dumps({k:r[k] for k in ['name','model','manufacturer']},ensure_ascii=False))
    c=http.client.HTTPConnection(url.hostname,url.port or 8123,timeout=20)
    c.request('GET','/api/states',headers={'Authorization':'Bearer '+saved['token']})
    r=c.getresponse();live=json.loads(r.read());c.close();saved.clear();assert r.status==200
    keep={'friendly_name','device_class','unit_of_measurement','supported_features','supported_color_modes',
          'brightness','color_temp_kelvin','percentage','preset_mode','preset_modes','temperature','current_temperature',
          'current_humidity','current_position','battery_level','icon','miot_type','device_type','entity_id'}
    safe=[{'entity_id':s['entity_id'],'state':s['state'] if s['entity_id'].split('.')[0] in
           {'light','switch','sensor','binary_sensor','camera','lock','fan','cover','climate'} else 'unknown',
           'attributes':{k:v for k,v in s.get('attributes',{}).items() if k in keep}} for s in live]
    (out/'real-states-ui-fixture.json').write_text(json.dumps(safe,ensure_ascii=False,indent=2),encoding='utf-8')
    for s in live:
        if s['entity_id'].startswith('light.') or re.search(r'camera.*on_p_|lock.*(lock_state|door_state|battery)|v12m.*(lock_state|door_state|battery)',s['entity_id']):
            print(json.dumps({'entity':s['entity_id'],'state':s['state'],'keys':list(s.get('attributes',{})),
                              'safe':{k:v for k,v in s.get('attributes',{}).items() if k in keep}},ensure_ascii=False))
    auth=None
finally:usb.close()
