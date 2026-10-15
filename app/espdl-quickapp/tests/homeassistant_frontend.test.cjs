'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname,
  '../overlay/apps/system/desktop/homeassistant/app.js'), 'utf8');

assert.equal(source.includes('\\u2212'), false,
  'frontend text must not use the unsupported Unicode minus sign');

function boot(width = 1024, height = 600, options = {}) {
  let serviceUrl = options.url || 'http://192.168.1.20:8123';
  let configured = true, configurationRevision = 1;
  let handle = 1;
  let requestId = 1;
  let timer;
  let touch;
  let swipe;
  let nextTimeout = 2;
  const deferred = [];
  const pending = [];
  const buttons = [];
  const controls = [];
  const requests = [];
  const geometry = new Map();
  const hidden = new Map();
  const textValues = new Map();
  const styles = new Map();
  const nativeWrites=[];
  const saves = new Map();
  const responses = {
    devices: options.devices || [],
    config: { location_name: 'Lab' },
    services: options.services || [{ domain: 'light', services: { turn_on: {}, turn_off: {} } }],
    states: options.states || [
      { entity_id: 'light.desk', state: 'on', attributes: {
        friendly_name: 'Desk', brightness: 128,
        supported_color_modes: ['brightness'] } },
      { entity_id: 'light.spotlights', state: 'on', attributes: {
        friendly_name: 'Spotlights', brightness: 200,
        supported_color_modes: ['brightness'] } },
      { entity_id: 'light.a', state: 'off', attributes: {
        friendly_name: 'Light A', supported_color_modes: ['brightness'] } },
      { entity_id: 'light.b', state: 'off', attributes: {
        friendly_name: 'Light B', supported_color_modes: ['brightness'] } },
      { entity_id: 'light.c', state: 'on', attributes: {
        friendly_name: 'Light C', brightness: 102,
        supported_color_modes: ['brightness'] } },
      { entity_id: 'light.d', state: 'off', attributes: {
        friendly_name: 'Light D', supported_color_modes: ['brightness'] } },
      { entity_id: 'sensor.temperature', state: '24.5', attributes: {
        friendly_name: 'Temperature', unit_of_measurement: 'C' } }
    ]
  };
  const service = {
    apiVersion: 1,
    configure: () => 0,
    status: () => ({ configured, busy: false, configurationRevision,
      deviceGroups: options.grouping !== false,sceneSync:options.sceneSync===true,
      url: serviceUrl, canControl: options.canControl !== false, canConfigure: true }),
    get(resource) {
      requests.push(resource);
      const id = requestId++;
      const body=resource==='scenes'?{items:(options.sceneStates||[]).slice(0,128),total:(options.sceneStates||[]).length}:responses[resource];
      pending.push({ requestId: id, busy: false, done: true,
        cached: resource === 'states', status: resource==='scenes'&&options.sceneStatus||resource==='states'&&options.stateStatus||resource==='devices'&&options.deviceStatus||200, error: 0,
        body: JSON.stringify(body) });
      return id;
    },
    getState(entity) {
      if(!options.acceptControls)return -22;
      const id=requestId++,state=responses.states.find(row=>row.entity_id===entity);
      pending.push({requestId:id,busy:false,done:true,status:state?200:404,error:0,body:JSON.stringify(state||{})});return id;
    },
    control(entity, on, brightness) {
      controls.push({ entity, on, brightness });
      if(options.busyAdmissions){options.busyAdmissions--;return -16;}
      if(options.acceptControls){
        const state=responses.states.find(row=>row.entity_id===entity);state.state=on?'on':'off';
        if(brightness>=0)state.attributes.brightness=Math.round(brightness*255/100);
        const id=requestId++;pending.push({requestId:id,busy:false,done:true,status:options.controlStatus||200,error:0,body:'[]'});return id;
      }
      return -22;
    },
    poll: () => options.holdRequests||options.sharedBusy ? { busy: true, done: false } :
      pending.shift() || { busy: false, done: false },
    close: () => { pending.length = 0; }
  };
  if(options.nativeActions)service.callService=(entity,action,data)=>{
    const body=JSON.parse(data);controls.push({entity,action,body});
    if(options.busyAdmissions){options.busyAdmissions--;return -16;}
    if(!options.acceptControls)return -22;
    const state=responses.states.find(row=>row.entity_id===entity)||(options.sceneStates||[]).find(row=>row.entity_id===entity);
    if((action==='turn_on'||action==='turn_off')&&!/^(scene|script)\./.test(entity))state.state=action==='turn_on'?'on':'off';
    if(body.brightness_pct!==undefined)state.attributes.brightness=Math.round(body.brightness_pct*255/100);
    if(body.color_temp_kelvin!==undefined)state.attributes.color_temp_kelvin=body.color_temp_kelvin;
    if(body.value!==undefined||body.option!==undefined)state.state=String(body.value===undefined?body.option:body.value);
    if(body.position!==undefined)state.attributes.current_position=body.position;
    if(body.hvac_mode!==undefined)state.state=body.hvac_mode;
    for(const key of ['percentage','preset_mode','oscillating','temperature','humidity','volume_level','is_volume_muted','effect','fan_mode','swing_mode','source','fan_speed','operation_mode'])
      if(body[key]!==undefined)state.attributes[key]=body[key];
    const id=requestId++;pending.push({requestId:id,busy:false,done:true,status:options.controlStatus||200,error:0,body:'[]'});return id;
  };
  function create(x, y, w, h) {
    const id = handle++;
    geometry.set(id, { x, y, w, h });
    return id;
  }
  function requireHandle(id, api) {
    if (!geometry.has(id)) {
      throw new TypeError(`${api} received a non-native handle: ${String(id)}`);
    }
  }
  const ui = {
    primary: options.dark ? 0xffffff : 0x252d46,
    nowMs: options.nowMs,
    getSize: () => ({ width, height }),
    background: () => {},
    button(text, x, y, w, h, callback) {
      const id = create(x, y, w, h);
      buttons.push({ id, callback });
      return id;
    },
    panel: (x, y, w, h) => create(x, y, w, h),
    rect: (x, y, w, h) => create(x, y, w, h),
    arc: (x, y, w, h) => create(x, y, w, h),
    arcSet: id => requireHandle(id, 'arcSet'),
    line: (points, x, y, w, h) => create(x, y, w, h),
    lineSet: id => requireHandle(id, 'lineSet'),
    text: (text, x, y, w = 1, h = 1) => {const id=create(x,y,w,h);textValues.set(id,text);return id;},
    setColor: (id,value) => {requireHandle(id, 'setColor');nativeWrites.push({method:'color',id,value});},
    setHidden: (id,value) => {requireHandle(id, 'setHidden');hidden.set(id,!!value);nativeWrites.push({method:'hidden',id,value:!!value});},
    setStyle: (id,style) => {requireHandle(id,'setStyle');if(style.fontSize!==undefined)assert(style.fontSize>=0&&style.fontSize<=48,'native style fontSize contract');styles.set(id,{...styles.get(id),...style});nativeWrites.push({method:'style',id,style});},
    setText(id, value) {
      requireHandle(id, 'setText');
      textValues.set(id, value);
      nativeWrites.push({method:'text',id,value});
    },
    setOpacity: id => requireHandle(id, 'setOpacity'),
    onSwipe: handler => { swipe = handler; },
    onTouch: handler => { touch = handler; },
    setPos(id, x, y) {
      requireHandle(id, 'setPos');
      Object.assign(geometry.get(id), { x, y });
      nativeWrites.push({method:'position',id,x,y});
    },
    setSize(id, w, h) {
      requireHandle(id, 'setSize');
      Object.assign(geometry.get(id), { w, h });
      nativeWrites.push({method:'size',id,w,h});
    }
  };
  const context = vm.createContext({
    system: { homeAssistantService: service, homeAssistant: {},
      yield: () => ({ then: resolve => { deferred.push(resolve); } }),
      storage: { get: key => key==='eh_favs'?JSON.stringify(options.favorites||[]):key==='eh_scope'?(options.savedScope||''):key==='eh_card_sizes'?(options.savedLayout||''):key==='eh_card_sizes_2'?(options.savedLayout2||''):key==='eh_card_usage'?(options.savedUsage||''):'', set: (key,value) => saves.set(key,value) } },
    ui, prompt: { input: () => {} }, console,
    setInterval(callback) { timer = callback; return 1; },
    clearInterval: () => {},
    setTimeout(callback) { deferred.push(callback); return nextTimeout++; },
    clearTimeout: () => {}
  });
  vm.runInContext(options.source||source, context, { filename: 'homeassistant/app.js' });
  // Creation deliberately shares several native event-loop turns.
  for(let i=0;i<30&&deferred.length;i++)deferred.shift()();
  return { context, buttons, controls, requests, geometry, hidden, textValues, styles, saves,nativeWrites,
    remoteConfigure(endpoint) {
      serviceUrl = endpoint;
      configured = !!endpoint;
      configurationRevision++;
    }, tick: () => {
    timer();
    const callback = deferred.shift();
    if (callback) callback();
  },
    touch: (state, nx, ny) => touch(state, nx, ny),
    swipe: direction => swipe(direction) };
}

test('frontend uses the reviewed native service and completes a state sync', () => {
  const runtime = boot();
  assert.equal(runtime.context.ESPHomeHA.snapshot().backend, 'native-service');
  runtime.context.ESPHomeHA.refresh();
  for (let i = 0; i < 12; i++) runtime.tick();
  const state = runtime.context.ESPHomeHA.snapshot();
  assert.equal(state.connected, true);
  assert.equal(state.phase, 'ready');
  assert.equal(state.lastCached, true);
  assert.equal(state.entities.length, 7);
  assert.equal(state.entities[0].brightness, 50);
});

test('a real-home sized state response exceeding 64 KiB still synchronizes', () => {
  const states=Array.from({length:100},(_,i)=>({entity_id:'sensor.large_'+i,state:String(i),
    attributes:{friendly_name:'Sensor '+i,unit_of_measurement:'C',extra:'x'.repeat(900)}}));
  assert.ok(JSON.stringify(states).length>65536);
  const r=boot(1024,600,{url:'http://124.223.30.246:8123',states});
  r.context.ESPHomeHA.refresh();for(let i=0;i<30;i++)r.tick();
  const result=r.context.ESPHomeHA.snapshot();
  assert.equal(result.connected,true);assert.equal(result.entities.length,100);
});

test('HA scheduling follows monotonic time across wall-clock changes', () => {
  let monotonic=1000;
  const runtime=boot(1024,536,{nowMs:()=>monotonic});
  runtime.context.ESPHomeHA.refresh();for(let i=0;i<20;i++)runtime.tick();
  const before=runtime.requests.length,deadline=runtime.context.ESPHomeHA.snapshot().nextRefresh;
  assert.equal(deadline,11000);
  vm.runInContext('Date.now=()=>9999999999999',runtime.context);
  for(let i=0;i<20;i++)runtime.tick();assert.equal(runtime.requests.length,before,'NTP forward jump must not expire HA requests');
  vm.runInContext('Date.now=()=>0',runtime.context);monotonic=deadline+1;
  for(let i=0;i<20;i++)runtime.tick();assert(runtime.requests.length>before,'wall-clock rollback must not stall refresh');
});

test('frontend keeps its fixed controls inside supported displays', () => {
  for (const [width, height] of [[1024, 536], [1024, 600]]) {
    const runtime = boot(width, height);
    assert.equal(runtime.buttons.length, 32);
    assert.ok(runtime.geometry.size <= 140,
      `${width}x${height}: ${runtime.geometry.size} native widgets exceeds the UI budget`);
    for (const box of runtime.geometry.values()) {
      assert.ok(box.x >= 0 && box.y >= 0 && box.w > 0 && box.h > 0);
      assert.ok(box.x + box.w <= width, `${width} clips ${JSON.stringify(box)}`);
      assert.ok(box.y + box.h <= height, `${height} clips ${JSON.stringify(box)}`);
    }
  }
});

test('More opens details while only the separate power icon toggles the entity', () => {
  for (const [width, height] of [[1024, 536], [1024, 600]]) {
    const runtime = boot(width, height);
    runtime.context.ESPHomeHA.refresh();
    for (let i = 0; i < 12; i++) runtime.tick();
    const card = runtime.buttons.find(entry => {
      const box = runtime.geometry.get(entry.id);
      return !runtime.hidden.get(entry.id) && box && box.x>=164 && box.w >= 120 && box.h === 38;
    });
    assert.ok(card, `${width}x${height}: a card sized button exists`);
    card.callback();
    assert.equal(runtime.context.ESPHomeHA.snapshot().view, 'detail');
    assert.equal(runtime.context.ESPHomeHA.snapshot().detailOpen, true);
    assert.equal(runtime.controls.length, 0, `${width}x${height}: card does not toggle`);
    for (let i = 0; i < 12; i++) runtime.tick();
    assert.equal(runtime.buttons.length, 64, `${width}x${height}: detail pool expands lazily`);
    for (const box of runtime.geometry.values()) {
      assert.ok(box.x + box.w <= width, `${width} detail clips ${JSON.stringify(box)}`);
      assert.ok(box.y + box.h <= height, `${height} detail clips ${JSON.stringify(box)}`);
    }

    const iconRuntime = boot(width, height);
    iconRuntime.context.ESPHomeHA.refresh();
    for (let i = 0; i < 12; i++) iconRuntime.tick();
    const icon = iconRuntime.buttons.find(entry => {
      const box = iconRuntime.geometry.get(entry.id);
      return box && box.w === 48 && box.h === 48;
    });
    assert.ok(icon, `${width}x${height}: an icon toggle exists`);
    icon.callback();
    assert.equal(iconRuntime.controls.length, 1, `${width}x${height}: one control call`);
    assert.equal(iconRuntime.controls[0].entity, 'light.desk');
    assert.equal(iconRuntime.controls[0].on, false);
  }
});

test('detail power control toggles while read-only details never send control', () => {
  const runtime = boot();
  runtime.context.ESPHomeHA.refresh();
  for (let i = 0; i < 12; i++) runtime.tick();
  const card = runtime.buttons.find(entry => {
    const box = runtime.geometry.get(entry.id);
    return !runtime.hidden.get(entry.id) && box && box.x>=164 && box.w >= 120 && box.h === 38;
  });
  card.callback();
  for (let i = 0; i < 12; i++) runtime.tick();
  assertUnifiedDetail(runtime);
  const power=runtime.buttons.find(entry=>{const g=runtime.geometry.get(entry.id);return !runtime.hidden.get(entry.id)&&g.w===48&&g.h===48&&g.x>700&&g.y<150;});
  assert(power,'More exposes a power action');
  power.callback();
  assert.equal(runtime.controls.length, 1, 'detail power sends one control call');
  assert.equal(runtime.controls[0].entity, 'light.desk');
  assert.equal(runtime.controls[0].on, false);

  const sensorRuntime = boot(1024, 600, { states: [{
    entity_id: 'sensor.temperature', state: '24.5', attributes: {
      friendly_name: 'Temperature', unit_of_measurement: 'C', device_class: 'temperature'
    }
  }] });
  sensorRuntime.context.ESPHomeHA.refresh();
  for (let i = 0; i < 12; i++) sensorRuntime.tick();
  const sensorCard = sensorRuntime.buttons.find(entry => {
    const box = sensorRuntime.geometry.get(entry.id);
    return !sensorRuntime.hidden.get(entry.id) && box && box.x>=164 && box.w >= 120 && box.h === 38;
  });
  sensorCard.callback();
  for (let i = 0; i < 12; i++) sensorRuntime.tick();
  assert.equal(sensorRuntime.context.ESPHomeHA.snapshot().view, 'detail');
  assert.equal(sensorRuntime.controls.length, 0);
});

test('detail brightness controls keep using brightness_pct through the native service', () => {
  const runtime = boot();
  runtime.context.ESPHomeHA.refresh();
  for (let i = 0; i < 12; i++) runtime.tick();
  const card = runtime.buttons.find(entry => {
    const box = runtime.geometry.get(entry.id);
    return box && box.x>=164 && box.w >= 120 && box.h === 38;
  });
  card.callback();
  for (let i = 0; i < 12; i++) runtime.tick();
  assertUnifiedDetail(runtime);
  const rail=runtime.context.ESPHomeHA.snapshot().parameterControls.find(r=>r.key==='brightness_pct');
  assert(rail);const nx=(rail.x+rail.w*(rail.vertical?.5:.6))/1024,ny=(rail.y+rail.h*(rail.vertical?.4:.5))/600;
  runtime.touch('down',nx,ny);runtime.touch('up',nx,ny);
  assert.equal(runtime.controls.length, 1);
  assert.equal(runtime.controls[0].entity, 'light.desk');
  assert.equal(runtime.controls[0].on, true);
  assert.equal(runtime.controls[0].brightness, 60);
});

test('a configured native session is adopted even on a public endpoint', () => {
  const runtime = boot(1024, 600, { url: 'http://124.223.30.246:8123' });
  const state = runtime.context.ESPHomeHA.snapshot();
  assert.equal(state.nativeManaged, true);
  assert.equal(state.backend, 'native-service');
  assert.equal(state.settingsOpen, false);
  assert.equal(state.url, 'http://124.223.30.246:8123');
  runtime.context.ESPHomeHA.refresh();
  for (let i = 0; i < 12; i++) runtime.tick();
  assert.equal(runtime.context.ESPHomeHA.snapshot().connected, true);
});

test('remote configuration updates the open home address and restores controls', () => {
  const runtime = boot(1024, 536);
  runtime.context.ESPHomeHA.refresh();
  for (let i = 0; i < 20; i++) runtime.tick();
  const before = runtime.requests.length;
  runtime.remoteConfigure('http://ha.example.com:8123');
  for (let i = 0; i < 20; i++) runtime.tick();
  const state = runtime.context.ESPHomeHA.snapshot();
  assert.equal(state.url, 'http://ha.example.com:8123');
  assert.equal(state.nativeManaged, true);
  assert.equal(state.connected, true);
  assert.deepEqual(runtime.requests.slice(before), ['config', 'services', 'devices', 'states']);
  const power = runtime.buttons.find(b => {
    const box = runtime.geometry.get(b.id);
    return box.w === 48 && box.h === 48;
  });
  power.callback();
  assert.equal(runtime.controls.length, 1);
});

test('remote token replacement discards an in-flight request even at the same address', () => {
  const options = { holdRequests: true };
  const runtime = boot(1024, 536, options);
  runtime.context.ESPHomeHA.refresh();
  runtime.tick();
  runtime.remoteConfigure('http://192.168.1.20:8123');
  runtime.tick();
  options.holdRequests = false;
  for (let i = 0; i < 20; i++) runtime.tick();
  assert.equal(runtime.context.ESPHomeHA.snapshot().connected, true);
  assert.deepEqual(runtime.requests, ['config', 'config', 'services', 'devices', 'states']);
});

test('clearing remote configuration removes the old home and opens settings', () => {
  const runtime = boot();
  runtime.context.ESPHomeHA.refresh();
  for (let i = 0; i < 20; i++) runtime.tick();
  const before = runtime.requests.length;
  runtime.remoteConfigure('');
  for (let i = 0; i < 20; i++) runtime.tick();
  const state = runtime.context.ESPHomeHA.snapshot();
  assert.equal(state.nativeManaged, false);
  assert.equal(state.connected, false);
  assert.equal(state.settingsOpen, true);
  assert.equal(state.entities.length, 0);
  assert.equal(runtime.requests.length, before);
});

test('dashboard preserves domain metadata and pages through every synced entity', () => {
  const states = [
    { entity_id: 'cover.study', state: 'open', attributes: {
      friendly_name: 'Study blind', device_class: 'blind', current_position: 72 } },
    { entity_id: 'climate.downstairs', state: 'heat', attributes: {
      friendly_name: 'Downstairs', current_temperature: 20.5, temperature: 22,
      hvac_action: 'heating' } },
    { entity_id: 'media_player.living_room', state: 'playing', attributes: {
      friendly_name: 'Living room speaker', media_title: 'Evening radio', volume_level: 0.35 } },
    { entity_id: 'binary_sensor.front_door', state: 'on', attributes: {
      friendly_name: 'Front door', device_class: 'door' } },
    { entity_id: 'sensor.phone_battery', state: '68', attributes: {
      friendly_name: 'Phone battery', device_class: 'battery', unit_of_measurement: '%' } }
  ];
  for (let index = 0; index < 10; index++) states.push({
    entity_id: `sensor.metric_${index}`, state: String(index), attributes: {
      friendly_name: `Metric ${index}`, unit_of_measurement: 'W', device_class: 'power'
    }
  });
  const runtime = boot(1024, 600, { states });
  runtime.context.ESPHomeHA.refresh();
  for (let index = 0; index < 12; index++) runtime.tick();
  let state = runtime.context.ESPHomeHA.snapshot();
  assert.equal(state.entities.length, 15);
  assert.equal(state.entities[0].current_position, 72);
  assert.equal(state.entities[1].current_temperature, 20.5);
  assert.equal(state.entities[1].temperature, 22);
  assert.equal(state.entities[2].media_title, 'Evening radio');
  assert.equal(state.entities[2].volume_level, 0.35);
  assert.equal(state.entities[3].device_class, 'door');
  assert.ok([...runtime.textValues.values()].some(value => String(value).startsWith('Evening')));
  runtime.swipe('left');
  state = runtime.context.ESPHomeHA.snapshot();
  assert.equal(state.roomPage, 1);
  runtime.swipe('right');
  assert.equal(runtime.context.ESPHomeHA.snapshot().roomPage, 0);
});

test('Mi quick cards keep the More and power targets separate in both themes', () => {
  for(const dark of [false,true]) {
    const runtime=boot(1024,536,{dark});runtime.context.ESPHomeHA.refresh();
    for(let i=0;i<20;i++)runtime.tick();
    const targets=runtime.buttons.map(button=>({button,box:runtime.geometry.get(button.id)}));
    const card=targets.find(t=>t.box.w>200&&t.box.h===38);
    const power=targets.find(t=>t.box.w===48&&t.box.h===48);
    assert(card&&power);
    assert(power.box.x>card.box.x+card.box.w/2,'the independent power switch sits at the upper right');
    assert(power.box.y<card.box.y);
    power.button.callback();
    assert.equal(runtime.controls.length,1);
    assert.deepEqual(runtime.controls[0],{entity:'light.desk',on:false,brightness:-1});
    assert(!runtime.context.ESPHomeHA.snapshot().detailOpen);
  }
});

test('offline, unknown and read-only quick cards never send control requests', () => {
  for(const options of [
    {canControl:false},
    {states:[{entity_id:'light.offline',state:'unavailable',attributes:{friendly_name:'Offline'}}]},
    {states:[{entity_id:'light.unknown',state:'unknown',attributes:{friendly_name:'Unknown'}}]}
  ]) {
    const runtime=boot(1024,536,options);runtime.context.ESPHomeHA.refresh();
    for(let i=0;i<20;i++)runtime.tick();
    const power=runtime.buttons.find(b=>{const box=runtime.geometry.get(b.id);return box.w===48&&box.h===48;});
    if(power)power.callback();assert.equal(runtime.controls.length,0);
    if(options.states)assert([...runtime.textValues.values()].includes(options.states[0].state==='unknown'?'状态未知':'设备离线'));
  }
});

test('quick-card registry covers the published category catalog', () => {
  const catalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../../homeassistant/quick-card-catalog.json'),'utf8'));
  assert.deepEqual([...boot().context.ESPHomeHA.snapshot().registeredCardTypes].sort(),catalog.types.slice().sort());
  assert.equal(boot().context.ESPHomeHA.snapshot().registeredCardVariants,127);
});

test('every published property combination resolves to its registered reference layout', () => {
  const catalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../../homeassistant/quick-card-catalog.json'),'utf8'));
  const fixtureContext=vm.createContext({});vm.runInContext(fs.readFileSync(path.join(__dirname,'mijia_card_fixture.js'),'utf8'),fixtureContext);
  catalog.variants.forEach((spec,i)=>{
    const runtime=boot(1024,536,{states:[fixtureContext.__miExamples[i]]});runtime.context.ESPHomeHA.refresh();for(let n=0;n<20;n++)runtime.tick();
    const match=runtime.context.ESPHomeHA.snapshot().entities[0].quickReference;
    assert.equal(match.layout,spec.layout,spec.type+' variant '+spec.variant);
    assert(match.variant>=1,spec.type+' resolves through the property registry');
    const card=runtime.buttons.find(b=>{const box=runtime.geometry.get(b.id);return box.w>200&&box.h===38;});
    card.callback();for(let n=0;n<25;n++)runtime.tick();
    for(const [id,box] of runtime.geometry)if(!runtime.hidden.get(id)) {
      assert(box.x>=0&&box.y>=0&&box.x+box.w<=1024&&box.y+box.h<=536,spec.type+' clips '+JSON.stringify(box));
    }
  });
});

test('quick-card variants depend on real properties and retain read-only domains', () => {
  const examples=[
    ['light','on',{brightness:160,color_temp_kelvin:4000},'light-color-temperature','63%'],
    ['light','off',{},'light-status',null],
    ['fan','on',{percentage:40,oscillating:true},'fan-speed','风速 40%'],
    ['fan','on',{device_type:'air-purifier',pm25:12},'air-quality','PM2.5 12.0'],
    ['fan','on',{percentage:NaN},'fan-status',null],
    ['cover','open',{current_position:72},'curtain-position','开合 72%'],
    ['media_player','playing',{volume_level:.35,media_title:'Evening radio'},'speaker-volume','音量 35%'],
    ['vacuum','cleaning',{battery_level:68},'vacuum-battery','68%'],
    ['humidifier','on',{current_humidity:48},'humidifier-humidity','48%'],
    ['climate','heat',{current_temperature:20.5,temperature:22},'temperature-reading','20.5 °C'],
    ['sensor','24.5',{device_class:'temperature',unit_of_measurement:'C'},'sensor-reading','24.5 °C'],
    ['camera','idle',{miot_type:'urn:miot-spec-v2:device:camera:0000A01C:example:1'},'profile-status','空闲'],
    ['switch','unavailable',{device_type:'outlet'},'switch-status',null],
    ['sensor','idle',{device_type:'not-a-registered-category'},'sensor-reading','空闲']
  ];
  for(const [domain,state,attributes,style,value] of examples) {
    const runtime=boot(1024,536,{states:[{entity_id:domain+'.example',state,attributes:{friendly_name:'Example',...attributes}}]});
    runtime.context.ESPHomeHA.refresh();for(let i=0;i<20;i++)runtime.tick();
    assert.equal(runtime.context.ESPHomeHA.snapshot().entities[0].cardStyle,style,domain);
    if(value){const display=style==='air-quality'?'12.0':/^(?:sensor-reading|temperature-reading|humidifier-humidity|vacuum-battery)$/.test(style)?value.replace(/^(\d+(?:\.\d+)?).*$/, '$1'):value;assert([...runtime.textValues.values()].some(text=>String(text).includes(display)),value);}
    if(!['light','switch','input_boolean','fan'].includes(domain)) {
      const card=runtime.buttons.find(b=>{const box=runtime.geometry.get(b.id);return box.w>200&&box.h===38;});
      card.callback();for(let i=0;i<20;i++)runtime.tick();
      runtime.buttons.forEach(b=>{const box=runtime.geometry.get(b.id);if(box.w===116&&box.h===116)b.callback();});
      assert.equal(runtime.controls.length,0,domain+' retains the existing read-only service scope');
    }
  }
  const runtime=boot(1024,536);runtime.context.ESPHomeHA.refresh();for(let i=0;i<20;i++)runtime.tick();
  assert([...runtime.geometry.values()].some(box=>box.w>200&&box.h===48),'small lamp brightness track follows the thick reference style');
});

test('unified small readings retain their values, units and readable text boxes',()=>{
  for(const height of [536,600]){
    const r=boot(1024,height,{states:[
      {entity_id:'fan.purifier',state:'on',attributes:{friendly_name:'Air purifier',device_type:'air-purifier',pm25:12}},
      {entity_id:'sensor.temperature',state:'24.5',attributes:{friendly_name:'Temperature',device_class:'temperature',unit_of_measurement:'°C'}},
      {entity_id:'sensor.humidity',state:'48.0',attributes:{friendly_name:'Humidity',device_class:'humidity',unit_of_measurement:'%'}}]});
    r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
    for(const part of ['12.0','24.5','48.0','°C','%'])assert([...r.textValues].some(([id,text])=>!r.hidden.get(id)&&String(text).includes(part)),part);
    for(const [id,text] of r.textValues)if(!r.hidden.get(id)&&/24.5|48.0/.test(String(text)))assert(r.geometry.get(id).h>=32);
    assert.equal(r.controls.length,0);
  }
});

function assertUnifiedDetail(runtime) {
  assert(runtime.context.ESPHomeHA.snapshot().detailOpen);
  assert(runtime.context.ESPHomeHA.snapshot().detailAdvanced,'small card opens the full detailed card in one step');
}

test('small lamp directly opens the detailed card and commits brightness through the original service', () => {
  const runtime=boot(1024,536);runtime.context.ESPHomeHA.refresh();for(let i=0;i<20;i++)runtime.tick();
  const card=runtime.buttons.find(b=>{const box=runtime.geometry.get(b.id);return box.w>200&&box.h===38;});
  card.callback();for(let i=0;i<20;i++)runtime.tick();
  assert.equal(runtime.context.ESPHomeHA.snapshot().entities[0].quickReference.layout,18);
  assert(runtime.context.ESPHomeHA.snapshot().detailAdvanced);
  assert(![...runtime.textValues].some(([id,text])=>!runtime.hidden.get(id)&&text==='更多操作'),'no intermediate card footer');
  const rect=runtime.context.ESPHomeHA.snapshot().parameterControls.find(r=>r.entity==='light.desk'&&r.key==='brightness_pct');
  assert(rect);const nx=(rect.x+rect.w*.75)/1024,ny=(rect.y+rect.h/2)/536;
  runtime.touch('down',nx,ny);assert.equal(runtime.controls.length,0);
  runtime.touch('up',nx,ny);assert.deepEqual(runtime.controls[0],{entity:'light.desk',on:true,brightness:75});
});

function layoutButton(runtime) {
  return runtime.buttons.find(b=>!runtime.hidden.get(b.id)&&['编辑卡片','完成编辑'].includes(runtime.textValues.get(b.id)));
}
function deviceBody(runtime,entityId) {
  let snapshot=runtime.context.ESPHomeHA.snapshot();const page=snapshot.layoutPages.findIndex(p=>p.some(r=>r.entity_id===entityId));
  while(snapshot.roomPage!==page){runtime.swipe(snapshot.roomPage<page?'left':'right');for(let i=0;i<20;i++)runtime.tick();snapshot=runtime.context.ESPHomeHA.snapshot();}
  const row=snapshot.layoutPages[page].find(r=>r.entity_id===entityId);
  const button=runtime.buttons.find(b=>{const box=runtime.geometry.get(b.id);return !runtime.hidden.get(b.id)&&(snapshot.layoutEditing?
    box.x===row.x+16&&box.y===row.y+row.h-50&&box.w===row.w-32&&box.h===36:
    box.x===row.x&&box.y===row.y+row.h-42&&box.w===row.w&&box.h===38);});
  assert(button,entityId+' is editable');return button;
}

function largeBrightnessRail(runtime,entityId) {
  const state=runtime.context.ESPHomeHA.snapshot();
  const row=state.layoutPages[state.roomPage].find(r=>r.entity_id===entityId);
  assert(row&&row.size==='small');
  const label=[...runtime.textValues].find(([id,value])=>!runtime.hidden.get(id)&&
    String(value).includes('%')&&runtime.geometry.get(id).y>=row.y&&runtime.geometry.get(id).y<row.y+row.h);
  assert(label,'brightness label');const box=runtime.geometry.get(label[0]);
  const rail=state.brightnessControls.find(r=>r.entity===entityId);
  assert(rail,'brightness track');return {rect:rail,labelId:label[0]};
}

test('mixed-board brightness drag owns the gesture and previews before one release commit', () => {
  const savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'light.desk':'large'}});
  const r=boot(1024,536,{savedLayout});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const rail=largeBrightnessRail(r,'light.desk'),nx=v=>(rail.rect.x+rail.rect.w*v)/1024,ny=(rail.rect.y+rail.rect.h/2)/536;
  r.touch('down',nx(.5),ny);r.touch('move',nx(.3),ny);r.swipe('left');for(let i=0;i<20;i++)r.tick();
  assert.equal(r.context.ESPHomeHA.snapshot().roomPage,0,'a brightness drag must not turn the board page');
  assert.equal(r.textValues.get(rail.labelId),'30%');assert.equal(r.controls.length,0);
  r.touch('up',nx(.3),ny);r.swipe('left');
  assert.deepEqual(r.controls,[{entity:'light.desk',on:true,brightness:30}]);
  assert.equal(r.context.ESPHomeHA.snapshot().roomPage,0,'release gesture belongs to the same slider');
  r.touch('down',.98,.85);r.swipe('left');assert.equal(r.context.ESPHomeHA.snapshot().roomPage,1,'ordinary board swiping still works');
});

test('switching from More to overview does not leave mixed brightness input blocked', () => {
  const savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'light.desk':'large'}});
  const r=boot(1024,536,{savedLayout});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const state=r.context.ESPHomeHA.snapshot(),row=state.layoutPages[0].find(v=>v.entity_id==='light.desk');
  const more=r.buttons.find(b=>{const v=r.geometry.get(b.id);return !r.hidden.get(b.id)&&v.x===row.x&&v.w===row.w&&v.h<80;});
  assert(more);more.callback();for(let i=0;i<30;i++)r.tick();assert(r.context.ESPHomeHA.snapshot().detailAdvanced);
  const overview=r.buttons.find(b=>{const v=r.geometry.get(b.id);return !r.hidden.get(b.id)&&v.x===8&&v.y===70;});
  assert(overview);overview.callback();for(let i=0;i<30;i++)r.tick();
  const rail=largeBrightnessRail(r,'light.desk'),nx=(rail.rect.x+rail.rect.w*.6)/1024,ny=(rail.rect.y+rail.rect.h/2)/536;
  r.touch('down',nx,ny);r.touch('up',nx,ny);
  assert.deepEqual(r.controls,[{entity:'light.desk',on:true,brightness:60}]);
});

test('cancelled, editing and invalidated brightness gestures never submit', () => {
  const savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'light.desk':'large'}});
  for(const mode of ['cancel','remote']) {
    const r=boot(1024,536,{savedLayout});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
    const rail=largeBrightnessRail(r,'light.desk'),nx=(rail.rect.x+rail.rect.w*.75)/1024,ny=(rail.rect.y+rail.rect.h/2)/536;
    if(mode==='editing'){layoutButton(r).callback();for(let i=0;i<20;i++)r.tick();}
    r.touch('down',nx,ny);r.touch('move',nx,ny);
    if(mode==='cancel')r.touch('cancel',nx,ny);
    if(mode==='remote'){r.remoteConfigure('http://192.168.1.21:8123');for(let i=0;i<20;i++)r.tick();}
    r.touch('up',nx,ny);assert.equal(r.controls.length,0,mode+' must not submit');
  }
});

test('registry device IDs group functions while identical names do not merge devices', () => {
  const states=[
    {entity_id:'light.main',state:'on',attributes:{friendly_name:'灯',brightness:128,supported_color_modes:['brightness']}},
    {entity_id:'sensor.temp_a',state:'24.5',attributes:{friendly_name:'温度',device_class:'temperature',unit_of_measurement:'°C'}},
    {entity_id:'sensor.hum_a',state:'48',attributes:{friendly_name:'湿度',device_class:'humidity',unit_of_measurement:'%'}},
    {entity_id:'sensor.temp_b',state:'22',attributes:{friendly_name:'温度',device_class:'temperature',unit_of_measurement:'°C'}},
    {entity_id:'sensor.hum_b',state:'60',attributes:{friendly_name:'湿度',device_class:'humidity',unit_of_measurement:'%'}},
    {entity_id:'sensor.temp_unrelated',state:'10',attributes:{friendly_name:'温度',device_class:'temperature',unit_of_measurement:'°C'}}
  ];
  const devices=[['dev_a','设备 A','客厅',['light.main','sensor.temp_a','sensor.hum_a']],
    ['dev_b','设备 B','客厅',['sensor.temp_b','sensor.hum_b']]];
  const r=boot(1024,536,{states,devices,savedLayout:JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'device.dev_a':'large'}})});
  r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();const s=r.context.ESPHomeHA.snapshot();
  assert.equal(s.entities.length,6);assert.equal(s.deviceCards.length,3);assert.equal(s.layoutPages.flat().length,3);
  const a=s.deviceCards.find(c=>c.device_id==='dev_a'),b=s.deviceCards.find(c=>c.device_id==='dev_b');
  assert.equal(a.entity_id,'light.main');assert.equal(a.cardSize,'small');assert.equal(a.members.length,3);
  assert.equal(b.quickReference.layout,7,'temperature and humidity share their registered two-metric card');
  assert.deepEqual(Array.from(b.members),['sensor.temp_b','sensor.hum_b']);
  const rail=largeBrightnessRail(r,'light.main'),nx=(rail.rect.x+rail.rect.w*.75)/1024,ny=(rail.rect.y+rail.rect.h/2)/536;
  r.touch('down',nx,ny);r.touch('up',nx,ny);assert.deepEqual(r.controls,[{entity:'light.main',on:true,brightness:75}]);
});

test('grouped device More exposes every member and keeps read-only sensors separate from controls', () => {
  const states=[{entity_id:'light.main',state:'on',attributes:{friendly_name:'主灯',brightness:128,supported_color_modes:['brightness']}},
    {entity_id:'sensor.temperature',state:'24.5',attributes:{friendly_name:'温度',device_class:'temperature',unit_of_measurement:'°C'}}];
  const r=boot(1024,536,{states,devices:[['dev_a','组合设备','',['light.main','sensor.temperature']]],
    savedLayout:JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'device.dev_a':'large'}})});
  r.context.ESPHomeHA.refresh();for(let i=0;i<45;i++)r.tick();
  const row=r.context.ESPHomeHA.snapshot().layoutPages[0][0];
  const more=r.buttons.find(b=>{const v=r.geometry.get(b.id);return !r.hidden.get(b.id)&&v.x===row.x&&v.w===row.w&&v.h<80;});
  assert(more);more.callback();for(let i=0;i<30;i++)r.tick();
  assert(r.context.ESPHomeHA.snapshot().functionControls.some(f=>f.entity==='sensor.temperature'&&f.kind==='read'));
  assert(!r.context.ESPHomeHA.snapshot().parameterControls.some(f=>f.entity==='sensor.temperature'));
  assert(![...r.textValues].some(([id,text])=>!r.hidden.get(id)&&String(text).includes('项功能')),'no separate function navigation');
  assert([...r.textValues].some(([id,text])=>!r.hidden.get(id)&&String(text).includes('24.5')));assert.equal(r.controls.length,0);
  const rail=r.context.ESPHomeHA.snapshot().parameterControls.find(v=>v.key==='brightness_pct');
  assert(rail);const nx=(rail.x+rail.w*(rail.vertical?.5:.6))/1024,ny=(rail.y+rail.h*(rail.vertical?.4:.5))/536;
  r.touch('down',nx,ny);r.touch('move',nx,ny);assert.equal(r.controls.length,0);
  r.touch('up',nx,ny);assert.deepEqual(r.controls,[{entity:'light.main',on:true,brightness:60}]);
});

test('device association failure falls back to state sync without guessed grouping', () => {
  const r=boot(1024,536,{deviceStatus:404});r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  const s=r.context.ESPHomeHA.snapshot();assert.equal(s.connected,true);assert.equal(s.deviceCards.length,s.entities.length);
  assert(s.deviceCards.every(c=>!c.device_id));
});

test('registered product model prevents camera indicators and lock settings from replacing the main device', () => {
  const states=[
    {entity_id:'light.cam_indicator',state:'on',attributes:{friendly_name:'摄像机 指示灯',supported_color_modes:['onoff']}},
    {entity_id:'switch.cam_motion_detection_p_5_1',state:'off',attributes:{friendly_name:'摄像机 移动侦测'}},
    {entity_id:'switch.cam_on_p_2_1',state:'on',attributes:{friendly_name:'摄像机控制 开关'}},
    {entity_id:'switch.lock_motion_detection_p_8_1',state:'on',attributes:{friendly_name:'门锁 移动侦测'}},
    {entity_id:'sensor.lock_door_state_p_10_1021',state:'正常上锁',attributes:{friendly_name:'门状态',device_class:'enum'}},
    {entity_id:'sensor.lock_battery',state:'22',attributes:{device_class:'battery',unit_of_measurement:'%'}}];
  const devices=[['camera_id','摄像机','',states.slice(0,3).map(s=>s.entity_id),'chuangmi.camera.079ac1','小白'],
    ['lock_id','门锁','',states.slice(3).map(s=>s.entity_id),'ynsur.lock.v12m','因硕']];
  const r=boot(1024,536,{states,devices});r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();
  const cards=r.context.ESPHomeHA.snapshot().deviceCards;
  assert.equal(cards.length,2);
  assert.equal(cards[0].quickReference.type,'camera');assert.equal(cards[0].quickReference.layout,4);
  assert.equal(cards[0].entity_id,'switch.cam_on_p_2_1');assert.equal(cards[0].members.length,3);
  assert.equal(cards[1].quickReference.type,'lock');assert.equal(cards[1].quickReference.layout,22);
  assert.equal(cards[1].entity_id,'sensor.lock_door_state_p_10_1021');assert.equal(cards[1].members.length,3);
  assert.equal(r.controls.length,0);
});

test('camera without its primary power entity cannot toggle its indicator as device power', () => {
  const states=[{entity_id:'light.cam_indicator',state:'on',attributes:{friendly_name:'指示灯'}}];
  const r=boot(1024,536,{states,devices:[['camera_id','摄像机','',['light.cam_indicator'],'chuangmi.camera.079ac1']]});
  r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const card=r.context.ESPHomeHA.snapshot().deviceCards[0];assert.equal(card.quickReference.type,'camera');assert(card.readonly);
  for(const b of r.buttons) {const v=r.geometry.get(b.id);if(!r.hidden.get(b.id)&&v.w===48&&v.h===48)b.callback();}
  assert.equal(r.controls.length,0);
});

test('all 127 registered variants survive grouping instead of collapsing into a generic card', () => {
  const context=vm.createContext({});vm.runInContext(fs.readFileSync(path.join(__dirname,'mijia_card_fixture.js'),'utf8'),context);
  for(const sample of context.__miExamples) {
    const expected=sample.attributes.review_layout;
    const auxiliary={entity_id:'switch.auxiliary',state:'on',attributes:{friendly_name:'辅助开关'}};
    const r=boot(1024,536,{states:[auxiliary,sample],devices:[['product','设备','',[auxiliary.entity_id,sample.entity_id]]]});
    r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
    const card=r.context.ESPHomeHA.snapshot().deviceCards[0];
    assert.equal(card.quickReference.type,sample.attributes.device_type);
    assert.equal(card.entity_id,sample.entity_id,'declared product capability wins over auxiliary switch');
    if(expected!==undefined)assert.equal(card.quickReference.layout,expected);
    const single=boot(1024,536,{states:[sample]});single.context.ESPHomeHA.refresh();for(let i=0;i<30;i++)single.tick();
    assert.equal(card.quickReference.layout,single.context.ESPHomeHA.snapshot().deviceCards[0].quickReference.layout);
    assert.equal(card.quickReference.variant,single.context.ESPHomeHA.snapshot().deviceCards[0].quickReference.variant);
  }
});

test('real HA device models bind to their registered card categories', () => {
  const evidence=path.resolve(__dirname,'../../../../output/mijia-mixed-fix-20261014');
  if(!fs.existsSync(path.join(evidence,'real-states-ui-fixture.json')))return;
  const states=JSON.parse(fs.readFileSync(path.join(evidence,'real-states-ui-fixture.json'),'utf8'));
  const registry=JSON.parse(fs.readFileSync(path.join(evidence,'real-device-registry.json'),'utf8'));
  const devices=registry.devices.map(d=>[d.device_id,d.name,d.area,d.entities,d.model,d.manufacturer]);
  const r=boot(1024,536,{states,devices});r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();
  const cards=r.context.ESPHomeHA.snapshot().deviceCards;
  const camera=cards.find(c=>c.model==='chuangmi.camera.079ac1');assert(camera);assert.equal(camera.quickReference.type,'camera');
  assert.equal(camera.entity_id,'switch.chuangmi_cn_1184483380_079ac1_on_p_2_1');assert.equal(camera.members.length,100);
  const lock=cards.find(c=>c.model==='ynsur.lock.v12m');assert(lock);assert.equal(lock.quickReference.type,'lock');assert.equal(lock.members.length,39);
  assert.equal(lock.entity_id,'sensor.ynsur_cn_1212516643_v12m_door_state_p_10_1021');
  const lamp=cards.find(c=>c.model==='yeelink.light.lamp28');assert(lamp);assert.equal(lamp.quickReference.layout,21);assert.equal(lamp.members.length,37);
  assert.equal(r.controls.length,0);
});

test('a device with more than 128 entities keeps its full function list and stable size', () => {
  const states=Array.from({length:180},(_,i)=>({entity_id:'sensor.feature_'+i,state:String(i),
    attributes:{friendly_name:'功能 '+i}}));
  const devices=[['dev_many','多功能设备','',states.map(s=>s.entity_id)]];
  const savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'device.dev_many':'large'}});
  const r=boot(1024,536,{states,devices,savedLayout});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const s=r.context.ESPHomeHA.snapshot();assert.equal(s.entities.length,180);
  assert.equal(s.deviceCards.length,1);assert.equal(s.deviceCards[0].members.length,180);assert.equal(s.limited,false);
  const changed=boot(1024,536,{states:states.slice(1),devices,savedLayout});changed.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)changed.tick();
  assert.equal(changed.context.ESPHomeHA.snapshot().deviceCards[0].cardSize,'small','size follows device ID even if its representative entity changes');
});







test('reference lamp small tile tracks every pointer move without repainting the rest of the page', () => {
  for(const dark of [false,true]) {
    const r=boot(1024,536,{dark});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
    const rail=r.context.ESPHomeHA.snapshot().brightnessControls.find(v=>v.entity==='light.desk');assert(rail);
    assert.equal(rail.h,48);const nx=v=>(rail.x+rail.w*v)/1024,ny=(rail.y+rail.h/2)/536;
    r.touch('down',nx(.2),ny);
    for(let i=0;i<60;i++) {
      const percent=20+i,from=r.nativeWrites.length;r.touch('move',nx(percent/100),ny);r.tick();
      assert.equal(r.context.ESPHomeHA.snapshot().brightnessInput.percent,percent);
      assert([...r.textValues].some(([id,v])=>!r.hidden.get(id)&&v===percent+'%'));
      const writes=r.nativeWrites.slice(from);assert(writes.length<=3,'only fill, thumb and percentage change per pointer move');
      assert(!writes.some(v=>v.method==='position'&&v.y!==Math.round(rail.y+10)),'other cards do not move or reflow while dragging');
    }
    assert.equal(r.controls.length,0);r.touch('up',nx(.79),ny);
    assert.deepEqual(r.controls,[{entity:'light.desk',on:true,brightness:79}]);
    deviceBody(r,'light.desk').callback();assert.equal(r.context.ESPHomeHA.snapshot().detailOpen,false,'release click belongs to the slider');
    r.touch('down',.98,.8);deviceBody(r,'light.desk').callback();assert(r.context.ESPHomeHA.snapshot().detailOpen,'ordinary card click still opens details');
  }
});

test('brightness preview stays responsive during background sync and commits once after it finishes', () => {
  const options={};const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  options.holdRequests=true;r.context.ESPHomeHA.refresh();for(let i=0;i<12;i++)r.tick();
  const rail=r.context.ESPHomeHA.snapshot().brightnessControls.find(v=>v.entity==='light.desk');assert(rail,'read-only state sync cannot disable the slider');
  const nx=(rail.x+rail.w*.68)/1024,ny=(rail.y+rail.h/2)/536;
  r.touch('down',nx,ny);r.touch('move',nx,ny);r.touch('up',nx,ny);
  assert.equal(r.controls.length,0,'keep the single native request lane');
  assert.equal(r.context.ESPHomeHA.snapshot().brightnessInput.percent,68);
  options.holdRequests=false;for(let i=0;i<30;i++)r.tick();
  assert.deepEqual(r.controls,[{entity:'light.desk',on:true,brightness:68}]);
});

test('an unsubmitted brightness intent never survives a remote home change', () => {
  const options={};const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  options.holdRequests=true;r.context.ESPHomeHA.refresh();for(let i=0;i<12;i++)r.tick();
  const rail=r.context.ESPHomeHA.snapshot().brightnessControls.find(v=>v.entity==='light.desk');assert(rail);
  const nx=(rail.x+rail.w*.68)/1024,ny=(rail.y+rail.h/2)/536;
  r.touch('down',nx,ny);r.touch('up',nx,ny);r.remoteConfigure('http://192.168.1.21:8123');r.tick();
  options.holdRequests=false;for(let i=0;i<30;i++)r.tick();assert.equal(r.controls.length,0);
});

test('low brightness retains a rounded cap and the three-pixel inset handle agrees with the touch axis', () => {
  const r=boot(1024,536,{dark:true,states:[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'台灯',brightness:41,supported_color_modes:['brightness']}}]});
  r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const rail=r.context.ESPHomeHA.snapshot().brightnessControls[0];
  const handle=[...r.geometry].find(([id,v])=>!r.hidden.get(id)&&v.w===6&&v.h===28);assert(handle);
  assert.equal(handle[1].x,Math.round(rail.x+rail.w*.16-3),'white bar center is the finger coordinate');
  const nx=(rail.x+rail.w*.76)/1024,ny=(rail.y+rail.h/2)/536;r.touch('down',nx,ny);
  assert.equal(r.geometry.get(handle[0]).x,Math.round(rail.x+rail.w*.76-3));
  r.touch('cancel',nx,ny);assert.equal(r.controls.length,0);
});

test('a failed refresh or removed lamp during a drag cannot leave a stuck unsubmitted target', () => {
  for(const mode of ['failed','removed']) {
    const states=[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'台灯',brightness:128,supported_color_modes:['brightness']}}];
    const options={states};const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
    const rail=r.context.ESPHomeHA.snapshot().brightnessControls[0],nx=(rail.x+rail.w*.6)/1024,ny=(rail.y+rail.h/2)/536;
    if(mode==='failed')options.stateStatus=503;else states.length=0;
    options.holdRequests=true;r.context.ESPHomeHA.refresh();for(let i=0;i<12;i++)r.tick();
    r.touch('down',nx,ny);r.touch('move',nx,ny);options.holdRequests=false;for(let i=0;i<10;i++)r.tick();
    r.touch('up',nx,ny);assert.equal(r.controls.length,0);assert.equal(r.context.ESPHomeHA.snapshot().brightnessInput,null);
  }
});

test('slider gestures never page the board when disabled or on read-only color temperature', () => {
  for(const mode of ['disabled']) {
    const states=[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'灯',brightness:160,color_temp_kelvin:2800,supported_color_modes:['color_temp']}}];
    for(let i=0;i<8;i++)states.push({entity_id:'sensor.extra_'+i,state:'1',attributes:{friendly_name:'读数 '+i}});
    const options={states,canControl:mode!=='disabled'};
    if(mode==='color')options.savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'light.desk':'large'}});
    const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();
    if(mode==='editing'){layoutButton(r).callback();for(let i=0;i<25;i++)r.tick();}
    let track;
    if(mode==='color') {
      const caption=[...r.textValues].find(([id,v])=>!r.hidden.get(id)&&String(v).startsWith('色温 '));assert(caption);
      const box=r.geometry.get(caption[0]);track={x:box.x,y:box.y+30,w:box.w,h:64};
    } else track=[...r.geometry].find(([id,b])=>!r.hidden.get(id)&&b.h===48&&b.w>200&&b.y>100)[1];
    const nx=(track.x+track.w*.5)/1024,ny=(track.y+track.h/2)/536;
    r.touch('down',nx,ny);r.touch('move',nx-.1,ny);r.swipe('left');r.touch('up',nx-.1,ny);r.swipe('left');
    assert.equal(r.context.ESPHomeHA.snapshot().roomPage,0,mode);assert.equal(r.controls.length,0);
    r.touch('down',.98,.88);r.swipe('left');assert.equal(r.context.ESPHomeHA.snapshot().roomPage,1,'ordinary swipes outside controls remain available');
  }
});

test('left bulb is a passive indicator while the separate right switch controls power', () => {
  const r=boot(1024,536);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const row=r.context.ESPHomeHA.snapshot().layoutPages[0].find(v=>v.entity_id==='light.desk');
  r.touch('down',(row.x+40)/1024,(row.y+44)/536);r.touch('up',(row.x+40)/1024,(row.y+44)/536);
  deviceBody(r,'light.desk').callback();assert(!r.context.ESPHomeHA.snapshot().detailOpen);assert.equal(r.controls.length,0);
  r.touch('down',(row.x+row.w-36)/1024,(row.y+38)/536);
  const power=r.buttons.find(b=>{const box=r.geometry.get(b.id);return !r.hidden.get(b.id)&&box.w===48&&box.h===48&&box.x===row.x+row.w-60;});
  assert(power);power.callback();assert.deepEqual(r.controls,[{entity:'light.desk',on:false,brightness:-1}]);
});



test('an unchanged refresh reuses grouped cards but changed readings still reach the UI', () => {
  const states=[{entity_id:'sensor.room',state:'24.5',attributes:{friendly_name:'温度',device_class:'temperature',unit_of_measurement:'°C'}}];
  const r=boot(1024,536,{states});r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  const visible=()=>[...r.textValues].filter(([id])=>!r.hidden.get(id)).map(([,text])=>text);
  r.nativeWrites.length=0;r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  assert(!r.nativeWrites.some(w=>w.method==='text'&&w.value==='24.5'),'same reading should not be rewritten');
  states[0].state='26.7';r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  assert(visible().some(text=>String(text).includes('26.7')));
  r.context.ESPHomeHA.disconnect();for(let i=0;i<35;i++)r.tick();
  assert(visible().includes('在线 0'),'cached statistics must invalidate on disconnect');
});



test('an in-flight control keeps navigation and other sliders interactive and serializes device commands', () => {
  const options={acceptControls:true},r=boot(1024,536,options);
  r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  function drag(entity,percent){
    const rail=r.context.ESPHomeHA.snapshot().brightnessControls.find(v=>v.entity===entity);assert(rail);
    const nx=(rail.x+rail.w*percent/100)/1024,ny=(rail.y+rail.h/2)/536;
    r.touch('down',nx,ny);r.touch('move',nx,ny);r.touch('up',nx,ny);
  }
  options.holdRequests=true;drag('light.desk',60);for(let i=0;i<15;i++)r.tick();
  assert.equal(r.controls.length,1);
  drag('light.spotlights',30);drag('light.spotlights',80);for(let i=0;i<15;i++)r.tick();
  assert.equal(r.controls.length,1,'the native lane receives only one request at a time');
  const waiting=r.context.ESPHomeHA.snapshot();assert.equal(waiting.controlQueue.length,1);
  assert.equal(waiting.controlQueue[0].brightness,80,'latest unsent level replaces the previous level for this device');
  assert(!layoutButton(r),'wide-card editor is removed');
  options.holdRequests=false;for(let i=0;i<60;i++)r.tick();
  assert.deepEqual(r.controls,[{entity:'light.desk',on:true,brightness:60},{entity:'light.spotlights',on:true,brightness:80}]);
  assert.equal(r.context.ESPHomeHA.snapshot().controlQueue.length,0);
  assert.equal(r.context.ESPHomeHA.snapshot().selectedId,'light.spotlights','verification must not steal the selection');
});

test('switching the home discards pending device intents without replaying them', () => {
  const options={acceptControls:true},r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const drag=(entity,value)=>{const rail=r.context.ESPHomeHA.snapshot().brightnessControls.find(v=>v.entity===entity);
    const x=(rail.x+rail.w*value/100)/1024,y=(rail.y+rail.h/2)/536;r.touch('down',x,y);r.touch('up',x,y);};
  options.holdRequests=true;drag('light.desk',55);for(let i=0;i<15;i++)r.tick();drag('light.spotlights',75);
  assert.equal(r.context.ESPHomeHA.snapshot().controlQueue.length,1);
  r.remoteConfigure('http://192.168.1.21:8123');for(let i=0;i<15;i++)r.tick();
  options.holdRequests=false;for(let i=0;i<60;i++)r.tick();
  assert.equal(r.controls.length,1);assert.equal(r.context.ESPHomeHA.snapshot().controlQueue.length,0);
});

test('frequently used small devices precede unused large cards on the home page', () => {
  const savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'light.desk':'large','light.spotlights':'large'}});
  const savedUsage=JSON.stringify({server:'http://192.168.1.20:8123',counts:{'sensor.temperature':8,'light.c':3}});
  const r=boot(1024,536,{savedLayout,savedUsage});r.context.ESPHomeHA.refresh();for(let i=0;i<45;i++)r.tick();
  const home=r.context.ESPHomeHA.snapshot().layoutPages;
  assert.equal(home[0][0].entity_id,'sensor.temperature');
  assert(home[0].some(row=>row.entity_id==='light.c'),'a common small card must not be pushed behind unused large cards');
});

test('usage is recorded per device, batched in storage and applied only on a home visit', () => {
  let now=1000;
  const states=[{entity_id:'sensor.a',state:'10',attributes:{friendly_name:'A'}},{entity_id:'sensor.b',state:'20',attributes:{friendly_name:'B'}}];
  const devices=[['dev_a','设备 A','',['sensor.a']],['dev_b','设备 B','',['sensor.b']]];
  const r=boot(1024,536,{states,devices,nowMs:()=>now});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const before=r.context.ESPHomeHA.snapshot().layoutPages;
  deviceBody(r,'sensor.b').callback();for(let i=0;i<35;i++)r.tick();
  assert(!r.saves.has('eh_card_usage'),'opening a card must not immediately write flash');
  assert.deepEqual(r.context.ESPHomeHA.snapshot().layoutPages,before,'recording use must not move the current cards');
  const overview=r.buttons.find(b=>{const box=r.geometry.get(b.id);return !r.hidden.get(b.id)&&box.x===8&&box.y===70&&box.h===38;});
  overview.callback();for(let i=0;i<30;i++)r.tick();
  assert.equal(r.context.ESPHomeHA.snapshot().layoutPages[0][0].entity_id,'sensor.b');
  now=12000;for(let i=0;i<25;i++)r.tick();
  const saved=JSON.parse(r.saves.get('eh_card_usage'));assert.equal(saved.counts['device.dev_b'],1);
  const other=boot(1024,536,{states,devices,url:'http://192.168.1.21:8123',savedUsage:JSON.stringify(saved)});
  other.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)other.tick();assert.equal(other.context.ESPHomeHA.snapshot().layoutPages[0][0].entity_id,'sensor.a');
});

test('an unconfirmed write drops queued controls and still allows card editing', () => {
  const options={acceptControls:true,controlStatus:503},r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const drag=(entity,value)=>{const rail=r.context.ESPHomeHA.snapshot().brightnessControls.find(v=>v.entity===entity);
    const x=(rail.x+rail.w*value/100)/1024,y=(rail.y+rail.h/2)/536;r.touch('down',x,y);r.touch('up',x,y);};
  options.holdRequests=true;drag('light.desk',60);for(let i=0;i<15;i++)r.tick();drag('light.spotlights',80);
  options.holdRequests=false;for(let i=0;i<45;i++)r.tick();
  assert.equal(r.controls.length,1,'uncertain writes and queued follow-ups must not be replayed');
  assert.equal(r.context.ESPHomeHA.snapshot().controlQueue.length,0);
  assert(!layoutButton(r),'wide editor remains absent after a failed command');
});

test('registry changes update grouped cards even when all state values are unchanged', () => {
  let now=1000;
  const states=[{entity_id:'sensor.room',state:'24.5',attributes:{friendly_name:'温度',device_class:'temperature'}}];
  const devices=[['dev_room','原房间','',['sensor.room']]];
  const r=boot(1024,536,{states,devices,nowMs:()=>now});r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  devices[0][1]='新房间';now=310000;r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  assert.equal(r.context.ESPHomeHA.snapshot().deviceCards[0].name,'新房间');
});

test('native busy admission keeps the latest unsent level and does not invalidate the whole home', () => {
  const options={acceptControls:true},r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const drag=value=>{const rail=r.context.ESPHomeHA.snapshot().brightnessControls.find(v=>v.entity==='light.desk');assert(rail);
    const x=(rail.x+rail.w*value/100)/1024,y=(rail.y+rail.h/2)/536;r.touch('down',x,y);r.touch('up',x,y);};
  options.busyAdmissions=1;options.sharedBusy=true;drag(30);for(let i=0;i<20;i++)r.tick();
  assert.equal(r.controls.length,1);assert(r.context.ESPHomeHA.snapshot().connected);
  assert(r.context.ESPHomeHA.snapshot().brightnessInput.pending,'not accepted means waiting, not sent');
  drag(85);for(let i=0;i<15;i++)r.tick();assert.equal(r.controls.length,1);
  options.sharedBusy=false;for(let i=0;i<60;i++)r.tick();
  assert.deepEqual(r.controls.map(v=>v.brightness),[30,85],'only the newest admitted target is sent');
  assert(r.context.ESPHomeHA.snapshot().connected);assert.equal(r.context.ESPHomeHA.snapshot().brightnessInput,null);
});

function parameterDrag(r,key,fraction,height=536){
  if(!r.context.ESPHomeHA.snapshot().parameterControls.some(v=>v.key===key)){deviceBody(r,r.context.ESPHomeHA.snapshot().deviceCards[0].entity_id).callback();for(let i=0;i<30;i++)r.tick();}
  const rail=r.context.ESPHomeHA.snapshot().parameterControls.find(v=>v.key===key);assert(rail,key);
  const nx=(rail.x+rail.w*(rail.vertical?.5:fraction))/1024,ny=(rail.y+rail.h*(rail.vertical?1-fraction:.5))/height;
  r.touch('down',nx,ny);for(let i=0;i<30;i++)r.touch('move',nx,ny);
  assert.equal(r.controls.length,0,'no service while dragging');r.swipe('left');r.touch('up',nx,ny);
}
test('color temperature uses device bounds, follows gestures, and verifies its own attribute',()=>{
  for(const dark of [false,true]){
    const states=[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'台灯',brightness:128,color_temp_kelvin:2700,min_color_temp_kelvin:2700,max_color_temp_kelvin:6500,supported_color_modes:['color_temp']}}];
    const r=boot(1024,536,{dark,states,nativeActions:true,acceptControls:true,savedLayout:JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'light.desk':'large'}})});
    r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();
    if(!r.context.ESPHomeHA.snapshot().detailOpen){deviceBody(r,'light.desk').callback();for(let i=0;i<30;i++)r.tick();}
    assert([...r.styles.values()].some(style=>style.gradientHorizontal===1&&style.gradientEnd===0xffffff),'Kelvin rail is a full warm-to-white gradient');
    parameterDrag(r,'color_temp_kelvin',.5);assert.equal(r.controls.length,1);
    assert.deepEqual(r.controls[0],{entity:'light.desk',action:'turn_on',body:{entity_id:'light.desk',color_temp_kelvin:4600}});
    for(let i=0;i<30;i++)r.tick();assert.equal(r.context.ESPHomeHA.snapshot().phase,'ready');
    assert.equal(r.context.ESPHomeHA.snapshot().entities[0].color_temp_kelvin,4600);
    assert.equal(r.context.ESPHomeHA.snapshot().roomPage,0);
    const row=r.context.ESPHomeHA.snapshot().layoutPages[0][0];
    assert(r.context.ESPHomeHA.snapshot().detailOpen);
    assert(r.context.ESPHomeHA.snapshot().functionControls.some(c=>c.key==='color_temp_kelvin'));
    assert(r.context.ESPHomeHA.snapshot().parameterControls.filter(c=>c.entity==='light.desk').length===2,'brightness and Kelvin are both visible without switching tabs');
    assert(r.context.ESPHomeHA.snapshot().parameterControls.every(c=>!c.vertical),'reference layout keeps horizontal controls');
    assert([...r.styles.values()].some(style=>style.gradientHorizontal===1),'More draws warm left to white right');
  }
});
test('More groups typed number, option and button actions while sensors stay read-only',()=>{
  const states=[{entity_id:'light.main',state:'on',attributes:{friendly_name:'主灯',brightness:128,supported_color_modes:['brightness']}},
    {entity_id:'number.limit',state:'2',attributes:{friendly_name:'限值',min:0,max:10,step:.5}},
    {entity_id:'select.mode',state:'auto',attributes:{friendly_name:'模式',options:['auto','quiet','boost']}},
    {entity_id:'button.test',state:'unknown',attributes:{friendly_name:'测试按钮'}},
    {entity_id:'sensor.temp',state:'24',attributes:{friendly_name:'温度'}}];
  const services=[{domain:'light',services:{turn_on:{},turn_off:{}}},{domain:'number',services:{set_value:{}}},{domain:'select',services:{select_option:{}}},{domain:'button',services:{press:{}}}];
  const r=boot(1024,536,{nativeActions:true,acceptControls:true,states,services,devices:[['dev','灯具','',states.map(s=>s.entity_id)]]});
  r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();deviceBody(r,'light.main').callback();for(let i=0;i<20;i++)r.tick();assertUnifiedDetail(r);
  assert.equal(r.context.ESPHomeHA.snapshot().selectedId,'light.main');
  parameterDrag(r,'value',.75);assert.deepEqual(r.controls[0].body,{entity_id:'number.limit',value:7.5});
  for(let i=0;i<20;i++)r.tick();assert.equal(r.context.ESPHomeHA.snapshot().phase,'ready');
  assert.equal(r.context.ESPHomeHA.snapshot().selectedId,'light.main','auxiliary controls retain the physical device view');
  function clickValue(text){
    const label=[...r.textValues].find(([id,t])=>!r.hidden.get(id)&&t===text);assert(label,text+' '+JSON.stringify([...r.textValues].filter(([id])=>!r.hidden.get(id)).map(([,value])=>value)));
    const g=r.geometry.get(label[0]),x=g.x+g.w/2,y=g.y+g.h/2;
    const b=r.buttons.find(b=>{const q=r.geometry.get(b.id);return !r.hidden.get(b.id)&&x>=q.x&&x<=q.x+q.w&&y>=q.y&&y<=q.y+q.h;});assert(b);b.callback();
  }
  clickValue('安静');assert.deepEqual(r.controls.at(-1).body,{entity_id:'select.mode',option:'quiet'});
  for(let i=0;i<20;i++)r.tick();assert.equal(r.context.ESPHomeHA.snapshot().phase,'ready');
  clickValue('执行');assert.equal(r.controls.at(-1).action,'press');
  for(let i=0;i<20;i++)r.tick();assert.equal(r.context.ESPHomeHA.snapshot().phase,'ready','stateless action accepts unchanged state');
  const page=r.buttons.find(b=>!r.hidden.get(b.id)&&r.textValues.get(b.id)==='>');assert(page);page.callback();for(let i=0;i<20;i++)r.tick();
  assert(r.context.ESPHomeHA.snapshot().functionControls.some(f=>f.entity==='sensor.temp'&&f.kind==='read'));
  assert(!r.context.ESPHomeHA.snapshot().parameterControls.some(f=>f.entity==='sensor.temp'));
  assert.equal(r.context.ESPHomeHA.snapshot().selectedId,'light.main');

});
test('brightness and temperature intents on one lamp do not replace each other',()=>{
  const states=[{entity_id:'light.desk',state:'on',attributes:{friendly_name:'灯',brightness:128,color_temp_kelvin:2700,supported_color_modes:['color_temp']}}];
  const opts={nativeActions:true,acceptControls:true,states,savedLayout:JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'light.desk':'large'}})};
  const r=boot(1024,536,opts);r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();opts.sharedBusy=true;opts.busyAdmissions=1;
  const b=r.context.ESPHomeHA.snapshot().brightnessControls[0],ny=(b.y+b.h/2)/536,nx=(b.x+b.w*.7)/1024;
  r.touch('down',nx,ny);r.touch('up',nx,ny);assert.equal(r.controls.length,1);
  r.touch('down',.98,.8);deviceBody(r,'light.desk').callback();for(let i=0;i<30;i++)r.tick();
  const t=r.context.ESPHomeHA.snapshot().parameterControls.find(row=>row.key==='color_temp_kelvin'),ty=(t.y+t.h/2)/536,tx=(t.x+t.w*.6)/1024;
  r.touch('down',tx,ty);r.touch('up',tx,ty);assert.equal(r.context.ESPHomeHA.snapshot().controlQueue.length,1);
  opts.sharedBusy=false;for(let i=0;i<60;i++)r.tick();assert.equal(r.controls.length,3);
  assert.equal(r.controls[1].body.brightness_pct,70);assert(r.controls[2].body.color_temp_kelvin);
  assert.equal(r.context.ESPHomeHA.snapshot().phase,'ready');
});

function commonProfileSamples(){
  return [
    {entity_id:'light.bedside',state:'on',attributes:{friendly_name:'床头灯',brightness:200,supported_color_modes:['color_temp'],color_temp_kelvin:2700,min_color_temp_kelvin:2700,max_color_temp_kelvin:6500}},
    {entity_id:'switch.plug',state:'on',attributes:{friendly_name:'插座'}},
    {entity_id:'fan.standard',state:'on',attributes:{friendly_name:'风扇',supported_features:59,percentage:40,percentage_step:10,preset_mode:'normal',preset_modes:['normal','quiet','sleep'],oscillating:false}},
    {entity_id:'climate.room',state:'cool',attributes:{friendly_name:'空调',supported_features:425,temperature:24,min_temp:16,max_temp:30,target_temp_step:.5,hvac_modes:['cool','heat','off'],fan_mode:'auto',fan_modes:['auto','low','high'],swing_modes:['off','vertical']}},
    {entity_id:'cover.curtain',state:'open',attributes:{friendly_name:'窗帘',supported_features:15,current_position:60}},
    {entity_id:'fan.purifier',state:'on',attributes:{friendly_name:'净化器',device_type:'air-purifier',supported_features:57,percentage:40,preset_modes:['auto','quiet'],pm25:12}},
    {entity_id:'humidifier.room',state:'on',attributes:{friendly_name:'加湿器',supported_features:1,humidity:55,min_humidity:30,max_humidity:80,current_humidity:48,available_modes:['auto','sleep']}},
    {entity_id:'media_player.speaker',state:'playing',attributes:{friendly_name:'音箱',supported_features:4|8|16|32|128|256|512|2048|4096|1,volume_level:.35,source_list:['Bluetooth','TV'],source:'Bluetooth'}},
    {entity_id:'vacuum.robot',state:'idle',attributes:{friendly_name:'扫地机',supported_features:8192|4|8|16|32|64,battery_level:86,fan_speed:'standard',fan_speed_list:['quiet','standard','max']}},
    {entity_id:'water_heater.bath',state:'on',attributes:{friendly_name:'热水器',supported_features:3,temperature:50,min_temp:35,max_temp:75,target_temp_step:1,operation_mode:'electric',operation_list:['electric','eco','off']}},
    {entity_id:'sensor.temp',state:'24.5',attributes:{friendly_name:'温度',device_class:'temperature',unit_of_measurement:'°C'}},
    {entity_id:'sensor.humidity',state:'48',attributes:{friendly_name:'湿度',device_class:'humidity',unit_of_measurement:'%'}}
  ];
}
function commonServices(){
  const catalog={light:['turn_on','turn_off'],switch:['turn_on','turn_off'],fan:['turn_on','turn_off','set_percentage','set_preset_mode','oscillate'],climate:['turn_on','turn_off','set_temperature','set_hvac_mode','set_fan_mode','set_swing_mode'],cover:['set_cover_position','open_cover','close_cover','stop_cover'],humidifier:['turn_on','turn_off','set_humidity','set_mode'],media_player:['turn_on','turn_off','volume_set','volume_mute','select_source','media_play','media_pause','media_stop','media_next_track','media_previous_track'],vacuum:['start','pause','stop','return_to_base','set_fan_speed'],water_heater:['set_temperature','set_operation_mode','turn_on','turn_off']};
  return Object.entries(catalog).map(([domain,services])=>({domain,services:Object.fromEntries(services.map(service=>[service,{}]))}));
}
test('all eleven common profiles are registered and match grouped physical devices without replacing the 127 references',()=>{
  const states=commonProfileSamples(),devices=states.slice(0,11).map((row,i)=>['dev'+i,row.attributes.friendly_name,'',i===10?['sensor.temp','sensor.humidity']:[row.entity_id]]);
  const r=boot(1024,536,{states,devices,services:commonServices(),nativeActions:true});r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();
  const snapshot=r.context.ESPHomeHA.snapshot(),catalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../../homeassistant/common-card-profiles.json'),'utf8'));
  assert.deepEqual(Array.from(snapshot.registeredCommonCardProfiles,p=>p.id),catalog.profiles.map(p=>p.id));
  assert.equal(snapshot.deviceCards.length,11);assert.equal(new Set(snapshot.deviceCards.map(card=>card.commonProfile)).size,11);
  assert.equal(snapshot.registeredCardVariants,127);assert.equal(snapshot.registeredCardTypes.length,59);
  const sensor=snapshot.deviceCards.find(card=>card.commonProfile==='temperature-humidity');assert.equal(sensor.members.length,2);
  assert.equal(r.controls.length,0,'registering and drawing profiles never issues control commands');
});
test('common small and large ranges own gestures and send exact typed parameters through the existing service',()=>{
  const cases=[['fan.standard','percentage','set_percentage',50],['climate.room','temperature','set_temperature',23],['cover.curtain','position','set_cover_position',50],['humidifier.room','humidity','set_humidity',55],['media_player.speaker','volume_level','volume_set',.5],['water_heater.bath','temperature','set_temperature',55]];
  for(const size of ['small'])for(const [id,key,action,value] of cases){
    const states=commonProfileSamples().filter(row=>row.entity_id===id),savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{[id]:size}});
    const r=boot(1024,536,{states,services:commonServices(),nativeActions:true,acceptControls:true,savedLayout});r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();
    if(key==='temperature'){deviceBody(r,id).callback();for(let i=0;i<30;i++)r.tick();}
    const rect=r.context.ESPHomeHA.snapshot().parameterControls.find(row=>row.entity===id&&row.key===key);assert(rect,id+' '+size);
    const nx=(rect.x+rect.w*.5)/1024,ny=(rect.y+rect.h/2)/536;
    r.touch('down',nx,ny);r.touch('move',nx,ny);r.swipe('left');assert.equal(r.controls.length,0);assert.equal(r.context.ESPHomeHA.snapshot().roomPage,0);
    r.touch('up',nx,ny);assert.equal(r.controls.length,1);assert.equal(r.controls[0].action,action);assert.deepEqual(r.controls[0].body,{entity_id:id,[key]:value});
    for(let i=0;i<20;i++)r.tick();assert.equal(r.context.ESPHomeHA.snapshot().phase,'ready',id+' confirms the right attribute');
  }
});
test('advertised feature limits hide unsupported media actions and cover controls',()=>{
  const states=[{entity_id:'media_player.volume_only',state:'on',attributes:{friendly_name:'只调音量',supported_features:4,volume_level:.2,source_list:['TV']}},
    {entity_id:'cover.position_only',state:'open',attributes:{friendly_name:'只调位置',supported_features:4,current_position:30}}];
  for(const state of states){const r=boot(1024,536,{states:[state],services:commonServices(),nativeActions:true});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();deviceBody(r,state.entity_id).callback();for(let i=0;i<20;i++)r.tick();assertUnifiedDetail(r);
    const specs=r.context.ESPHomeHA.snapshot().functionControls;assert.equal(specs.length,1);assert.equal(specs[0].key,state.entity_id.startsWith('cover')?'position':'volume_level');assert.equal(r.controls.length,0);
  }
});
test('long option lists choose an exact raw value without cycling or leaking input to the page',()=>{
  const states=[{entity_id:'light.main',state:'on',attributes:{friendly_name:'主灯',brightness:128,supported_color_modes:['brightness']}},{entity_id:'select.long_mode',state:'m0',attributes:{friendly_name:'模式',options:['m0','m1','m2','m3','m4','m5']}}];
  const r=boot(1024,536,{nativeActions:true,acceptControls:true,states,services:[...commonServices(),{domain:'select',services:{select_option:{}}}],devices:[['dev','灯具','',states.map(row=>row.entity_id)]]});r.context.ESPHomeHA.refresh();for(let i=0;i<50;i++)r.tick();deviceBody(r,'light.main').callback();for(let i=0;i<20;i++)r.tick();assertUnifiedDetail(r);
  function click(text){const button=r.buttons.find(button=>!r.hidden.get(button.id)&&r.textValues.get(button.id)===text);if(button){button.callback();return;}const label=[...r.textValues].find(([id,value])=>!r.hidden.get(id)&&value===text);assert(label,text);const g=r.geometry.get(label[0]);const target=r.buttons.find(button=>{const b=r.geometry.get(button.id);return !r.hidden.get(button.id)&&g.x+g.w/2>=b.x&&g.x+g.w/2<=b.x+b.w&&g.y+g.h/2>=b.y&&g.y+g.h/2<=b.y+b.h;});assert(target);target.callback();}
  click('m0  >');for(let i=0;i<10;i++)r.tick();assert(r.context.ESPHomeHA.snapshot().optionPicker);assert.equal(r.controls.length,0);
  r.swipe('left');r.touch('down',.5,.5);r.touch('up',.8,.5);assert.equal(r.controls.length,0);
  click('>');for(let i=0;i<10;i++)r.tick();click('m5');assert.deepEqual(r.controls[0].body,{entity_id:'select.long_mode',option:'m5'});for(let i=0;i<20;i++)r.tick();assert.equal(r.context.ESPHomeHA.snapshot().optionPicker,null);assert.equal(r.context.ESPHomeHA.snapshot().selectedId,'light.main');
  assert.equal(r.buttons.length,64,'profile options reuse the native pool rather than adding native controls per device');
});

test('climate power turns off an active HVAC mode while a temperature intent is pending',()=>{
  const state=commonProfileSamples().find(row=>row.entity_id==='climate.room');
  const r=boot(1024,536,{states:[state],services:commonServices(),nativeActions:true,acceptControls:true});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const plus=r.buttons.find(button=>!r.hidden.get(button.id)&&r.textValues.get(button.id)==='+');assert(plus);plus.callback();assert.equal(r.controls[0].action,'set_temperature');
  const glyph=[...r.textValues].find(([id,text])=>!r.hidden.get(id)&&text==='\uf011');assert(glyph);
  const g=r.geometry.get(glyph[0]),x=g.x+g.w/2,y=g.y+g.h/2;
  const power=r.buttons.find(button=>{const b=r.geometry.get(button.id);return !r.hidden.get(button.id)&&b.w===48&&b.h===48&&x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;});assert(power);r.touch('down',x/1024,y/536);r.touch('up',x/1024,y/536);power.callback();
  for(let i=0;i<40;i++)r.tick();assert.equal(r.controls.at(-1).action,'turn_off');assert.equal(r.controls.at(-1).entity,'climate.room');
});

test('every common small card opens the unified detailed page without the intermediate quick card',()=>{
  for(const state of commonProfileSamples().slice(0,11)){
    const r=boot(1024,536,{states:[state],services:commonServices(),nativeActions:true});r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
    deviceBody(r,state.entity_id).callback();for(let i=0;i<25;i++)r.tick();const snapshot=r.context.ESPHomeHA.snapshot();
    assert.equal(snapshot.view,'detail');assert(snapshot.detailAdvanced,state.entity_id);assert.equal(snapshot.selectedId,state.entity_id);
    assert(![...r.textValues].some(([id,text])=>!r.hidden.get(id)&&text==='更多操作'),state.entity_id+' has no duplicated middle page');
    assert.equal(r.controls.length,0,'opening details does not issue commands');
  }
});

test('one back action from direct details restores the same board page and saved card combination',()=>{
  const r=boot(1024,536);r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();r.swipe('left');for(let i=0;i<20;i++)r.tick();
  const before=r.context.ESPHomeHA.snapshot();assert.equal(before.roomPage,1);const entry=before.layoutPages[1][0];
  deviceBody(r,entry.entity_id).callback();for(let i=0;i<25;i++)r.tick();assert(r.context.ESPHomeHA.snapshot().detailAdvanced);
  const back=r.buttons.find(button=>{const box=r.geometry.get(button.id);return !r.hidden.get(button.id)&&box.x===176&&box.w===32&&box.h===30;});assert(back);back.callback();for(let i=0;i<25;i++)r.tick();
  const after=r.context.ESPHomeHA.snapshot();assert.equal(after.view,'list');assert.equal(after.roomPage,before.roomPage);assert.equal(after.nav,before.nav);
  assert.equal(JSON.stringify(after.layoutPages),JSON.stringify(before.layoutPages));assert.equal(r.controls.length,0);
});

test('all 59 device types have a unique profile and the additional 42 render both card sizes and direct details',()=>{
  const catalog=JSON.parse(fs.readFileSync(path.join(__dirname,'../../homeassistant/quick-card-catalog.json'),'utf8'));
  const registry=JSON.parse(fs.readFileSync(path.join(__dirname,'../../homeassistant/common-card-profiles.json'),'utf8'));
  const covered=registry.profiles.flatMap(profile=>profile.types);
  assert.equal(new Set(covered).size,covered.length);assert.deepEqual([...covered].sort(),catalog.types.slice().sort());
  const fixtures=vm.createContext({});vm.runInContext(fs.readFileSync(path.join(__dirname,'mijia_card_fixture.js'),'utf8'),fixtures);
  const additional=registry.profiles.filter(profile=>profile.layout);assert.equal(additional.length,42);
  for(const profile of additional)for(const size of ['small'])for(const dark of [false,true]){
    const sample=fixtures.__miExamples.find(row=>profile.types.includes(row.attributes.device_type));assert(sample,profile.id);
    const savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{[sample.entity_id]:size}});
    const r=boot(1024,536,{states:[sample],nativeActions:true,services:commonServices(),savedLayout,dark});
    r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
    const card=r.context.ESPHomeHA.snapshot().deviceCards[0];assert.equal(card.commonProfile,profile.id);
    for(const [id,box] of r.geometry)if(!r.hidden.get(id))assert(box.x>=0&&box.y>=0&&box.x+box.w<=1024&&box.y+box.h<=536,profile.id+' '+size+' clips '+JSON.stringify(box));
    if(size==='small'){
      deviceBody(r,sample.entity_id).callback();for(let i=0;i<25;i++)r.tick();assertUnifiedDetail(r);
      const details=r.context.ESPHomeHA.snapshot();assert.equal(details.view,'detail');
      if(profile.readonly)assert(details.functionControls.every(spec=>spec.kind==='read'),profile.id+' is read-only');
      for(const [id,box] of r.geometry)if(!r.hidden.get(id))assert(box.x>=0&&box.y>=0&&box.x+box.w<=1024&&box.y+box.h<=536,profile.id+' detail clips '+JSON.stringify(box));
      assert.equal(r.buttons.length,64,'new profiles share the existing button pool');
    }
    assert.equal(r.controls.length,0,'rendering never operates a device');
  }
});

test('air monitor preserves zero, small decimals, source units and grouped sensor readings without duplicates',()=>{
  const states=[
    {entity_id:'sensor.pm',state:'0',attributes:{friendly_name:'PM2.5',device_type:'air-monitor',device_class:'pm25',unit_of_measurement:'µg/m³',hcho:.023}},
    {entity_id:'sensor.temp_f',state:'75.2',attributes:{friendly_name:'温度',device_class:'temperature',unit_of_measurement:'°F'}},
    {entity_id:'sensor.hum',state:'0',attributes:{friendly_name:'湿度',device_class:'humidity',unit_of_measurement:'%'}}];
  const r=boot(1024,536,{states,devices:[['air','空气监测','',states.map(row=>row.entity_id)]]});
  r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const card=r.context.ESPHomeHA.snapshot().deviceCards[0],readings=Array.from(card.readings);
  assert.equal(readings.find(row=>row.key==='pm2.5-density').display,'0.0 ug/m3');
  assert.equal(readings.find(row=>row.key==='hcho-density').display,'0.023 mg/m3');
  assert.equal(readings.find(row=>row.key==='temperature').display,'75.2 °F');
  assert.equal(readings.find(row=>row.key==='relative-humidity').display,'0.0%');
  deviceBody(r,card.entity_id).callback();for(let i=0;i<25;i++)r.tick();
  const fields=r.context.ESPHomeHA.snapshot().functionControls;assert.equal(fields.length,4);
  assert.equal(new Set(fields.map(spec=>spec.title)).size,4);assert(fields.every(spec=>spec.kind==='read'));
});

test('alarms distinguish triggered, untriggered and unavailable without fabricating normal readings',()=>{
  for(const [type,active] of [['smoke-sensor','检测到烟雾'],['gas-sensor','检测到燃气'],['submersion-sensor','检测到漏水']]){
    for(const state of ['on','off','unknown','unavailable']){
      const sample={entity_id:'binary_sensor.alarm',state,attributes:{device_type:type,friendly_name:'报警传感器'}};
      const r=boot(1024,536,{states:[sample]});r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
      const reading=r.context.ESPHomeHA.snapshot().deviceCards[0].readings[0];
      assert.equal(reading.alert,state==='on');
      if(state==='on')assert.equal(reading.display,active);
      if(['unknown','unavailable'].includes(state))assert.equal(reading.display,'--');
      assert.equal(r.controls.length,0);
    }
  }
});

test('heater binds existing temperature controls and camera details remain explicit about unavailable video',()=>{
  const heater={entity_id:'climate.heater',state:'heat',attributes:{device_type:'heater',friendly_name:'取暖器',supported_features:385,temperature:23,current_temperature:20,min_temp:16,max_temp:30,hvac_modes:['heat','off']}};
  const r=boot(1024,536,{states:[heater],services:commonServices(),nativeActions:true,acceptControls:true});
  r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();parameterDrag(r,'temperature',.5);
  assert.deepEqual(r.controls[0].body,{entity_id:'climate.heater',temperature:23});
  const camera=boot(1024,536,{states:[{entity_id:'camera.entry',state:'idle',attributes:{friendly_name:'门口摄像头',device_type:'camera'}}]});
  camera.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)camera.tick();deviceBody(camera,'camera.entry').callback();for(let i=0;i<25;i++)camera.tick();
  assert([...camera.textValues].some(([id,text])=>!camera.hidden.get(id)&&text.includes('暂无预览画面')));
  assert(camera.context.ESPHomeHA.snapshot().functionControls.every(spec=>spec.kind==='read'));
  assert.equal(camera.controls.length,0);
});

test('a declared read-only sensor never acquires a power button from a switch-shaped entity',()=>{
  const r=boot(1024,536,{states:[{entity_id:'switch.smoke',state:'on',attributes:{friendly_name:'烟雾状态',device_type:'smoke-sensor'}}],
    nativeActions:true,services:[{domain:'switch',services:{turn_on:{},turn_off:{}}}]});
  r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  assert(![...r.textValues].some(([id,text])=>!r.hidden.get(id)&&text==='\uf011'));
  deviceBody(r,'switch.smoke').callback();for(let i=0;i<25;i++)r.tick();
  assert(r.context.ESPHomeHA.snapshot().functionControls.every(spec=>spec.kind==='read'));
  for(const button of r.buttons)if(!r.hidden.get(button.id))button.callback();
  assert.equal(r.controls.length,0);
});

test('unified firmware ignores historical wide sizes and only More opens all 59 categories',()=>{
  const fixture=JSON.parse(fs.readFileSync(path.join(__dirname,'all_device_preview.json'),'utf8'));
  for(const sample of fixture.cases){
    const entity=sample.states[0].entity_id;
    const r=boot(1024,536,{states:sample.states,services:fixture.services,nativeActions:true,
      savedLayout:JSON.stringify({server:'http://192.168.1.20:8123',sizes:{[entity]:'large'}})});
    r.context.ESPHomeHA.refresh();for(let i=0;i<45;i++)r.tick();
    const state=r.context.ESPHomeHA.snapshot(),row=state.layoutPages[0][0];
    assert.equal(row.size,'small',sample.type);assert(!state.layoutEditing);assert(!layoutButton(r));
    const x=row.x+row.w/2,y=row.y+22;
    assert(!r.buttons.some(button=>{const b=r.geometry.get(button.id);return !r.hidden.get(button.id)&&x>=b.x&&x<=b.x+b.w&&y>=b.y&&y<=b.y+b.h;}),'body has no action: '+sample.type);
    deviceBody(r,entity).callback();for(let i=0;i<30;i++)r.tick();assertUnifiedDetail(r);
    assert.equal(r.controls.length,0,sample.type+' opens without sending a device command');
    for(const [id,b] of r.geometry)if(!r.hidden.get(id))assert(b.x>=0&&b.y>=0&&b.x+b.w<=1024&&b.y+b.h<=536,sample.type+' visible geometry');
  }
});

test('temperature small-card stepper uses advertised steps and never opens More',()=>{
  for(const domain of ['climate','water_heater']){
    const state={entity_id:domain+'.target',state:domain==='climate'?'heat':'on',attributes:{friendly_name:'Temperature',temperature:24,min_temp:16,max_temp:30,target_temp_step:.5,
      supported_features:domain==='climate'?425:3,hvac_modes:['heat','off'],operation_list:['electric','off']}};
    const r=boot(1024,536,{states:[state],services:commonServices(),nativeActions:true,acceptControls:true});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
    const plus=r.buttons.find(button=>!r.hidden.get(button.id)&&r.textValues.get(button.id)==='+');assert(plus);plus.callback();
    assert.equal(r.controls[0].action,'set_temperature');assert.equal(r.controls[0].body.temperature,24.5);
    assert(!r.context.ESPHomeHA.snapshot().detailOpen);
  }
});

test('scene and log navigation replace environment/system and retain sensors under devices',()=>{
  const states=[{entity_id:'scene.evening',state:'2026-10-15T00:00:00',attributes:{friendly_name:'晚间场景'}},
    {entity_id:'script.sleep',state:'off',attributes:{friendly_name:'睡眠脚本'}},
    {entity_id:'sensor.room',state:'24.5',attributes:{friendly_name:'室温',device_class:'temperature',unit_of_measurement:'°C'}}];
  const r=boot(1024,536,{states});r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  const select=y=>{const button=r.buttons.find(button=>{const box=r.geometry.get(button.id);return !r.hidden.get(button.id)&&box.x===8&&box.y===y;});assert(button);button.callback();for(let i=0;i<25;i++)r.tick();};
  assert([...r.textValues.values()].includes('场景'));assert([...r.textValues.values()].includes('日志'));
  assert(![...r.textValues.values()].includes('环境'));assert(![...r.textValues.values()].includes('系统'));
  select(162);let snapshot=r.context.ESPHomeHA.snapshot();assert.equal(snapshot.nav,'scenes');
  assert.deepEqual([...snapshot.layoutPages.flat().map(row=>row.entity_id)].sort(),['scene.evening','script.sleep']);
  select(116);snapshot=r.context.ESPHomeHA.snapshot();assert(snapshot.layoutPages.flat().some(row=>row.entity_id==='sensor.room'));
  select(208);snapshot=r.context.ESPHomeHA.snapshot();assert.equal(snapshot.nav,'logs');assert(snapshot.activityLog.length>0);
  assert(snapshot.layoutPages.flat().every(row=>row.entity_id.startsWith('local.log_')));
  for(const button of r.buttons){const box=r.geometry.get(button.id);if(!r.hidden.get(button.id)&&box.x>=180&&box.y>=76&&box.y<450)button.callback();}
  assert.equal(r.controls.length,0,'log entries are never device controls');
});

test('session logs stay bounded and do not repeat unchanged periodic synchronization',()=>{
  const states=[{entity_id:'sensor.room',state:'0',attributes:{friendly_name:'Room'}}];
  const r=boot(1024,536,{states});r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  const count=r.context.ESPHomeHA.snapshot().activityLog.length;
  r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();assert.equal(r.context.ESPHomeHA.snapshot().activityLog.length,count);
  for(let i=1;i<=40;i++){states[0].state=String(i);r.context.ESPHomeHA.refresh();for(let j=0;j<30;j++)r.tick();}
  assert.equal(r.context.ESPHomeHA.snapshot().activityLog.length,32);
});

function chooseSection(r,y){
  const button=r.buttons.find(button=>{const box=r.geometry.get(button.id);return !r.hidden.get(button.id)&&box.x===8&&box.y===y;});
  assert(button);button.callback();for(let i=0;i<45;i++)r.tick();
}
function sceneExecuteButton(r,id){
  const row=r.context.ESPHomeHA.snapshot().layoutPages.flat().find(row=>row.entity_id===id);assert(row);
  return r.buttons.find(button=>{const box=r.geometry.get(button.id);return !r.hidden.get(button.id)&&box.x===row.x+16&&box.y===row.y+82&&box.w===row.w-32;});
}
const sceneServices=[{domain:'scene',services:{turn_on:{}}},{domain:'script',services:{turn_on:{}}}];
test('HA scene sync independently discovers scenes beyond 512 devices and refreshes renamed/removed presets',()=>{
  const sceneStates=[{entity_id:'scene.evening',state:'unknown',attributes:{friendly_name:'晚间回家'}},
    {entity_id:'script.sleep',state:'off',attributes:{friendly_name:'一键睡眠',last_triggered:'2026-10-15T12:00:00Z'}}];
  const options={sceneSync:true,sceneStates,nativeActions:true,services:sceneServices,
    states:Array.from({length:600},(_,i)=>({entity_id:'sensor.room_'+i,state:'0',attributes:{}}))};
  const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<45;i++)r.tick();
  assert.equal(r.context.ESPHomeHA.snapshot().entities.length,512);assert(!r.requests.includes('scenes'));
  chooseSection(r,162);let snapshot=r.context.ESPHomeHA.snapshot();
  assert.equal(snapshot.sceneSyncReady,true);assert.equal(snapshot.sceneTotal,2);
  assert.deepEqual([...snapshot.layoutPages.flat().map(row=>row.entity_id)].sort(),['scene.evening','script.sleep']);
  assert.equal(snapshot.entities.length,512,'independent scenes never replace device state');
  assert([...r.textValues.values()].includes('尚未触发'));assert(sceneExecuteButton(r,'scene.evening'));
  const count=snapshot.activityLog.length;r.context.ESPHomeHA.refresh();for(let i=0;i<45;i++)r.tick();
  assert.equal(r.context.ESPHomeHA.snapshot().activityLog.length,count,'unchanged scene sync does not add logs');
  sceneStates[0].attributes.friendly_name='回家';sceneStates.splice(1,1);
  r.context.ESPHomeHA.refresh();for(let i=0;i<45;i++)r.tick();snapshot=r.context.ESPHomeHA.snapshot();
  assert.equal(snapshot.scenes[0].name,'回家');assert.equal(snapshot.sceneTotal,1);
  assert(!snapshot.layoutPages.flat().some(row=>row.entity_id==='script.sleep'));
});
test('HA scenes remain discoverable with favorite-only device sync and bounded to 128 entries',()=>{
  const options={sceneSync:true,acceptControls:true,savedScope:'favorites',favorites:['sensor.room'],
    states:[{entity_id:'sensor.room',state:'24',attributes:{}}],
    sceneStates:Array.from({length:140},(_,i)=>({entity_id:'scene.preset_'+i,state:'unknown',attributes:{friendly_name:'场景 '+i}}))};
  const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<45;i++)r.tick();chooseSection(r,162);
  const snapshot=r.context.ESPHomeHA.snapshot();assert.equal(snapshot.scope,'favorites');
  assert.equal(snapshot.entities.length,1);assert.equal(snapshot.scenes.length,128);assert.equal(snapshot.sceneTotal,140);
});
test('HA scene/script execution targets one preset, coalesces repeat taps and acknowledges acceptance without on-state verification',()=>{
  for(const domain of ['scene','script']){
    const entity=domain+'.preset',options={sceneSync:true,nativeActions:true,acceptControls:true,services:sceneServices,
      sceneStates:[{entity_id:entity,state:domain==='scene'?'unknown':'off',attributes:{friendly_name:'一键联动'}}]};
    const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();chooseSection(r,162);
    const button=sceneExecuteButton(r,entity);assert(button);options.holdRequests=true;button.callback();button.callback();
    assert.equal(r.controls.length,1);assert.equal(r.controls[0].entity,entity);assert.equal(r.controls[0].action,'turn_on');
    assert.deepEqual(r.controls[0].body,{entity_id:entity});assert.equal(r.context.ESPHomeHA.snapshot().pending.kind,'service');
    options.holdRequests=false;for(let i=0;i<45;i++)r.tick();const snapshot=r.context.ESPHomeHA.snapshot();
    assert.equal(r.controls.length,1);assert.equal(snapshot.phase,'ready');assert.equal(snapshot.controlQueue.length,0);
    assert(snapshot.activityLog.some(row=>row.name==='HA 已接受场景'));
    assert(!snapshot.activityLog.some(row=>row.name==='操作已确认'),'HTTP success does not claim all grouped devices reached their target');
    assert.equal(snapshot.scenes[0].state,domain==='scene'?'unknown':'off','a preset is never forced into an on state');
    assert(!snapshot.functionControls.some(spec=>spec.service==='turn_off'));
  }
});
test('scene sync failure retains stale rows, recovers and clears the independent catalog on a home change',()=>{
  const options={sceneSync:true,sceneStates:[{entity_id:'scene.old_home',state:'unknown',attributes:{friendly_name:'旧家庭'}}]};
  const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();chooseSection(r,162);
  options.sceneStatus=500;r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  let snapshot=r.context.ESPHomeHA.snapshot();assert(snapshot.connected);assert(snapshot.sceneSyncError);assert(snapshot.scenes[0].stale);
  options.sceneStatus=200;options.sceneStates=[];r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();
  snapshot=r.context.ESPHomeHA.snapshot();assert.equal(snapshot.sceneTotal,0);assert.equal(snapshot.scenes.length,0);assert(!snapshot.sceneSyncError);
  r.remoteConfigure('http://other-home.local:8123');r.tick();snapshot=r.context.ESPHomeHA.snapshot();assert(!snapshot.sceneSyncReady);assert.equal(snapshot.scenes.length,0);
});
test('scene action is absent without HA control grants or the advertised turn_on service',()=>{
  for(const controlAllowed of [false,true]){
    const options={sceneSync:true,nativeActions:true,canControl:controlAllowed,services:controlAllowed?[]:sceneServices,
      sceneStates:[{entity_id:'scene.preset',state:'unknown',attributes:{}}]};
    const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();chooseSection(r,162);
    assert(!sceneExecuteButton(r,'scene.preset'));assert.equal(r.controls.length,0);
  }
});

test('a failed scene submission is never replayed automatically',()=>{
  const options={sceneSync:true,nativeActions:true,acceptControls:true,controlStatus:500,services:sceneServices,
    sceneStates:[{entity_id:'scene.preset',state:'unknown',attributes:{friendly_name:'回家场景'}}]};
  const r=boot(1024,536,options);r.context.ESPHomeHA.refresh();for(let i=0;i<40;i++)r.tick();chooseSection(r,162);
  sceneExecuteButton(r,'scene.preset').callback();for(let i=0;i<100;i++)r.tick();
  const snapshot=r.context.ESPHomeHA.snapshot();assert.equal(r.controls.length,1);assert.equal(snapshot.controlQueue.length,0);
  assert.equal(snapshot.nextRefresh,Infinity);assert(snapshot.activityLog.some(row=>row.name==='操作失败'));
});


