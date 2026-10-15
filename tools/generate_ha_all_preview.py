"""Generate explicit offline examples for every registered device category."""
from pathlib import Path
import json

repo = Path(__file__).resolve().parents[1]
catalog = json.loads((repo/'app/homeassistant/quick-card-catalog.json').read_text(encoding='utf-8'))
profiles = json.loads((repo/'app/homeassistant/common-card-profiles.json').read_text(encoding='utf-8'))['profiles']
by_type = {kind: profile for profile in profiles for kind in profile['types']}
domains = {
    'diffuser':'humidifier','air-conditioner':'climate','air-fresh':'fan',
    'air-purifier':'fan','airer':'cover','bath-heater':'climate','camera':'camera',
    'ceiling-fan':'fan','curtain':'cover','fan':'fan','heater':'climate',
    'hood':'fan','humidifier':'humidifier','integrated-stove':'fan','light':'light',
    'mirror':'light','outlet':'switch','relay':'switch','switch':'switch',
    'speaker':'media_player','vacuum':'vacuum','video-doorbell':'camera',
    'water-heater':'water_heater','water-dispenser':'water_heater',
    'kettle':'water_heater','health-pot':'water_heater','towel-rack':'climate',
    'pet-drinking-fountain':'switch','massage':'switch','moxibustion':'switch',
    'mosquito-dispeller':'switch','gateway':'binary_sensor'}
binary = {'magnet-sensor':'door','motion-sensor':'motion','smoke-sensor':'smoke',
          'gas-sensor':'gas','submersion-sensor':'moisture'}
values = {'temperature':24.5,'target-temperature':26,'relative-humidity':48,
          'atmospheric-pressure':1013,'pm2.5-density':12,'co2-density':560,
          'hcho-density':.023,'tvoc-density':.12,'illumination':380,'soil-ec':1200,
          'battery-level':86,'working-time':35,'current-distance':2.3,
          'left-time':'00:20','tds-out':12,'mode':'auto','fan-level':40,
          'horizontal-swing':True,'brightness':63,'color-temperature':4000,
          'current-position':72,'motor-control':'open','volume':.39,'mute':False,
          'playing-state':'playing','water-level':75,'filter-life-level':82,
          'freezer-temperature':-18,'progress':42,'speed':6.5,
          'repellent-left-level':76,'target-lid-state':'closed','guard-mode':'home',
          'contact-state':'off','motion-state':'off','submersion-state':'off'}
services = {
    'light':['turn_on','turn_off'], 'switch':['turn_on','turn_off'],
    'fan':['turn_on','turn_off','set_percentage','set_preset_mode','oscillate'],
    'climate':['turn_on','turn_off','set_temperature','set_hvac_mode','set_fan_mode','set_swing_mode','set_preset_mode'],
    'cover':['set_cover_position','open_cover','close_cover','stop_cover'],
    'humidifier':['turn_on','turn_off','set_humidity','set_mode'],
    'media_player':['turn_on','turn_off','volume_set','volume_mute','select_source','media_play','media_pause','media_stop','media_next_track','media_previous_track'],
    'vacuum':['start','pause','stop','return_to_base','set_fan_speed'],
    'water_heater':['set_temperature','set_operation_mode','turn_on','turn_off']}
groups = [{'domain':domain,'services':{service:{} for service in names}} for domain,names in services.items()]
cases = []
for kind in catalog['types']:
    profile = by_type[kind]
    name = next(row['name'] for row in catalog['variants'] if row['type']==kind)
    domain = 'binary_sensor' if kind in binary else domains.get(kind,'sensor')
    state = {'climate':'cool','water_heater':'on','cover':'open',
             'media_player':'playing','camera':'idle','vacuum':'idle',
             'binary_sensor':'off','sensor':'idle'}.get(domain,'on')
    if kind in ['heater','bath-heater','towel-rack']: state='heat'
    if kind in ['washer','dishwasher']: state='washing'
    if kind in ['air-fryer','cooker','induction-cooker','juicer','oven','pressure-cooker']:state='cooking'
    if kind=='printer':state='printing'
    attrs = {'friendly_name':name,'device_type':kind,'device_id':'preview_device',
             'device_name':name,'miot_properties':{}}
    if domain=='light':attrs.update(brightness=160,supported_color_modes=['color_temp'],
        color_temp_kelvin=4000,min_color_temp_kelvin=2700,max_color_temp_kelvin=6500,
        effect_list=['阅读','柔光','夜灯'],effect='阅读',supported_features=4)
    if domain=='fan':attrs.update(supported_features=59,percentage=40,percentage_step=10,
        preset_mode='auto',preset_modes=['auto','quiet','sleep'],oscillating=False)
    if domain=='climate':attrs.update(supported_features=425,temperature=26,
        current_temperature=24.5,min_temp=16,max_temp=30,target_temp_step=.5,
        hvac_modes=['heat','off'] if state=='heat' else ['cool','heat','off'],
        fan_mode='auto',fan_modes=['auto','low','high'],swing_mode='off',swing_modes=['off','vertical'])
    if domain=='water_heater':attrs.update(supported_features=3,temperature=50,
        current_temperature=42,min_temp=35,max_temp=75,target_temp_step=1,
        operation_mode='electric',operation_list=['electric','eco','off'])
    if domain=='cover':attrs.update(supported_features=15,current_position=72)
    if domain=='humidifier':attrs.update(supported_features=1,humidity=55,min_humidity=30,
        max_humidity=80,current_humidity=48,available_modes=['auto','sleep'],mode='auto')
    if domain=='media_player':attrs.update(supported_features=4|8|16|32|128|256|512|2048|4096|1,
        volume_level=.39,source_list=['Bluetooth','TV'],source='Bluetooth',media_title='午后的音乐')
    if domain=='vacuum':attrs.update(supported_features=8192|4|8|16|32|64,battery_level=86,
        fan_speed='standard',fan_speed_list=['quiet','standard','max'])
    if kind in binary:attrs.update(device_class=binary[kind],battery_level=86)
    sensor = {'air-monitor':('pm25','12','ug/m3'),
              'temperature-humidity-sensor':('temperature','24.5','°C'),
              'illumination-sensor':('illuminance','380','lx'),
              'plant-monitor':('moisture','42','%'),'flowerpot':('moisture','42','%'),
              'fridge':('temperature','4','°C'),
              'gas-concentration':('gas','0.02','% LEL'),
              'water-purifier':('','12','ppm'),
              'treadmill':('distance','2.3','km'),'walking-pad':('distance','2.3','km')}.get(kind)
    if sensor:
        attrs.update(device_class=sensor[0],unit_of_measurement=sensor[2]);state=sensor[1]
    if kind=='temperature-humidity-sensor':attrs.update(current_temperature=24.5,current_humidity=48)
    if kind=='gateway':attrs['device_class']='connectivity';state='on'
    local = dict(values, status=state, on=state not in ['off','closed'])
    if kind in ['plant-monitor','flowerpot']:local['relative-humidity']=42
    if kind=='fridge':local['temperature']=4
    if domain=='water_heater':local.update(temperature=42,**{'target-temperature':50})
    # Use an ordinary tunable lamp; the first lamp variant is a fan/light combo.
    first = next(row for row in catalog['variants'] if row['type']==kind
                 and (kind != 'light' or row['variant']==2))
    for key in first['requires']:
        prop=key.split('.',1)[1]
        value=local.get(prop,True if prop.startswith('start-') or prop in ['play','pause','package'] else state)
        existing=attrs['miot_properties'].get(key)
        if existing is None:attrs['miot_properties'][key]=value
        elif isinstance(existing,list):existing.append(value)
        else:attrs['miot_properties'][key]=[existing,value]
    for key,_,_ in profile.get('readings',[]):
        if not key.startswith('$') and key in local:attrs['miot_properties'][kind+'.'+key]=local[key]
    attrs['miot_properties'][kind+'.status']=state
    attrs['miot_properties'][kind+'.on']=local['on']
    states=[{'entity_id':domain+'.preview','state':state,'attributes':attrs}]
    cases.append({'type':kind,'name':name,'states':states})
assert len(cases)==len({row['type'] for row in cases})==59
target=repo/'app/espdl-quickapp/tests'
(target/'all_device_preview.json').write_text(json.dumps({'cases':cases,'services':groups},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def literal(value):return json.dumps(value,ensure_ascii=False)
lines=['/* Generated offline samples; never included by a firmware target. */',
       'struct all_device_case_s {const char *type,*name,*states;};',
       'static const struct all_device_case_s all_device_cases[] = {']
for case in cases:lines.append('  {'+','.join([literal(case['type']),literal(case['name']),literal(json.dumps(case['states'],ensure_ascii=False,separators=(',',':')))])+'},')
lines+=['};','static const char all_device_services[] = '+literal(json.dumps(groups,separators=(',',':')))+';']
(target/'all_device_preview.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('ALL_DEVICE_PREVIEW_FIXTURES',len(cases))
