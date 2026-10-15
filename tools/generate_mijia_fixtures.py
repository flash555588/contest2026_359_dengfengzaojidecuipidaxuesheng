from pathlib import Path
import json
root=Path(__file__).resolve().parents[2];catalog=json.loads((root/'project/app/homeassistant/quick-card-catalog.json').read_text(encoding='utf-8'))
rows=[]
for spec in catalog['variants']:
    kind=spec['type'];domain={'light':'light','switch':'switch','relay':'switch','outlet':'switch','fan':'fan','air-purifier':'fan','air-fresh':'fan','ceiling-fan':'fan','air-conditioner':'climate','curtain':'cover','airer':'cover','speaker':'media_player','camera':'camera','video-doorbell':'camera','vacuum':'vacuum','humidifier':'humidifier'}.get(kind,'sensor')
    state={'sensor':'24.5','climate':'cool','cover':'open','media_player':'playing','camera':'idle','vacuum':'cleaning'}.get(domain,'on')
    attrs={'friendly_name':spec['name'],'device_type':kind,'miot_properties':{}}
    values={'on':True,'temperature':24.5,'relative-humidity':48,'atmospheric-pressure':1013,'pm2.5-density':12,'hcho-density':.02,'tvoc-density':.1,'target-temperature':26,'mode':'auto','fan-level':40,'horizontal-swing':True,'brightness':63,'color-temperature':4000,'current-position':72,'motor-control':'open','battery-level':68,'illumination':300,'soil-ec':1200,'working-time':40,'current-distance':2.3,'left-time':'00:20','tds-out':12,'volume':.39,'mute':False,'playing-state':'playing','status':state,'submersion-state':'off'}
    counts={}
    for key in spec['requires']:counts[key]=counts.get(key,0)+1
    for key,n in counts.items():
        prop=key.split('.',1)[1];value=values.get(prop,True if prop.startswith('start-') or prop in ['play','pause','package'] else state)
        attrs['miot_properties'][key]=[value]*n if n>1 else value
    if domain=='light':
        if 'light.brightness' in spec['requires']:attrs.update(brightness=160,supported_color_modes=['brightness'])
        if 'light.color-temperature' in spec['requires']:attrs['color_temp_kelvin']=4000
    if any(k.endswith('.fan-level') for k in spec['requires']):attrs['percentage']=40
    if domain=='climate':attrs.update(current_temperature=24.5,temperature=26,hvac_action='cooling')
    if any(k.endswith('.mode') for k in spec['requires']):attrs['hvac_modes' if domain=='climate' else 'preset_modes']=['auto','cool','heat'] if domain=='climate' else ['auto','turbo','sleep']
    if domain=='media_player':attrs.update(volume_level=.39,media_title='午后的音乐')
    if domain=='vacuum':attrs['battery_level']=68
    if domain=='humidifier':attrs['current_humidity']=48
    if domain=='cover':attrs['current_position']=72
    rows.append({'entity_id':domain+'.example','state':state,'attributes':attrs})
prelude=root/'project/app/espdl-quickapp/tests/mijia_card_fixture.js'
prelude.write_text('// Offline display fixtures for all public property combinations.\nglobalThis.__miExamples='+json.dumps(rows,ensure_ascii=False,separators=(',',':'))+';\nglobalThis.__miSpecs='+json.dumps([{'type':r['type'],'variant':r['variant'],'layout':r['layout'],'size':r['size']} for r in catalog['variants']],separators=(',',':'))+';\n',encoding='utf-8')
(root/'project/app/espdl-quickapp/tests/mijia_card_fixture.h').write_text('/* Offline quick-card reference dimensions, generated with the JS fixtures. */\nstatic const unsigned mi_card_sizes[127][2] = {\n'+',\n'.join('  {'+str(r['size'][0])+','+str(r['size'][1])+'}' for r in catalog['variants'])+'\n};\n',encoding='utf-8')
print('MI_REVIEW_FIXTURES',len(rows))
