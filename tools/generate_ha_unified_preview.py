"""Refresh local sample data for the unified, two-view design preview."""
from pathlib import Path
import argparse
import json
import re

repo = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fragment',type=Path,default=repo/'app/homeassistant/preview/unified-device-cards.html')
parser.add_argument('--output',type=Path,default=repo/'out/ha-unified-preview')
args=parser.parse_args()
output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
fragment=args.fragment.resolve()
profiles = json.loads((repo/'app/homeassistant/common-card-profiles.json').read_text(encoding='utf-8'))['profiles']
cases = json.loads((repo/'app/espdl-quickapp/tests/all_device_preview.json').read_text(encoding='utf-8'))['cases']
catalog = json.loads((repo/'app/homeassistant/quick-card-catalog.json').read_text(encoding='utf-8'))
assert len(cases)==len(set(row['type'] for row in cases))==59
assert {row['type'] for row in cases}==set(catalog['types'])
# Keep only metadata consumed by this offline preview. Firmware fixtures retain
# their full capabilities and property variants in the original source file.
profile_keys = {'types','primary','readings','states','readonly','layout'}
compact_profiles = [{key:value for key,value in profile.items() if key in profile_keys}
                    for profile in profiles]
by_type = {kind:profile for profile in profiles for kind in profile['types']}
attribute_keys = {'temperature','min_temp','max_temp','target_temp_step',
                  'hvac_modes','percentage','unit_of_measurement'}
compact_cases = []
for case in cases:
    source = case['states'][0]
    attrs = source['attributes']
    properties = {reading[0] for reading in by_type[case['type']].get('readings',[])}
    compact_attrs = {key:value for key,value in attrs.items() if key in attribute_keys}
    compact_attrs['miot_properties'] = {
        key:value for key,value in attrs.get('miot_properties',{}).items()
        if key.split('.',1)[-1] in properties}
    compact_cases.append({'type':case['type'],'name':case['name'],
        'states':[{'entity_id':source['entity_id'],'state':source['state'],
                   'attributes':compact_attrs}]})
payload = json.dumps({'profiles':compact_profiles,'cases':compact_cases},ensure_ascii=False,separators=(',',':')).replace('<','\\u003c')
text = fragment.read_text(encoding='utf-8').replace('ha-common-devices','ha-unified59-devices')
text = re.sub(r'(<script type="application/json" data-catalog>).*?(</script>)',lambda match:match[1]+payload+match[2],text,flags=re.S)
assert '__UNIFIED_CATALOG__' not in text
assert len(text.encode()) < 1_000_000
assert '宽卡' not in text and 'data-view=' not in text
fragment.write_text(text,encoding='utf-8')
registry = {'version':2,'status':'local interactive design preview','target':'1024x600 landscape',
 'layout':{'reference':'User supplied unified UI','views':['overview','detail','scenes','logs'],
 'navigation':['总览','设备','场景','日志'],
 'overview':'6 small cards per page; only More functions opens detail; picker changes the overview page',
 'detail':'One merged card; primary controls left and auxiliary controls right',
 'wideCards':False,'sharedState':True},
 'performance':{'samePage':'reuse screen without chrome writes',
 'externalState':'compare fields; retain cached nodes and input focus',
 'navigationSaveDelayMs':120,'navigationSaveMaxDelayMs':400,
 'controlCommitSave':'immediate; no save during range input',
 'lifecycleSave':'flush pending state on pagehide or hidden',
 'cachedScreensLimit':2},
 'categories':catalog['types'],'profiles':profiles,'sampleData':True,'firmwareModified':False}
(output/'design-registry.json').write_text(json.dumps(registry,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'categories':59,'views':registry['layout']['views'],'wideCards':False,'bytes':len(text.encode())}))
