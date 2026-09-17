const { test } = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const appRoot = path.join(__dirname, '..');
const publishRoot = path.join(appRoot, '..', '..');
const quickApp = path.join(publishRoot, 'quickapp', 'homeassistant');
const system = path.join(publishRoot, 'app', 'espdl-quickapp', 'overlay',
  'apps', 'system');
const source = fs.readFileSync(path.join(quickApp, 'app.js'), 'utf8');

test('built-in package identity and native authorization stay aligned', () => {
  const manifest = JSON.parse(fs.readFileSync(path.join(quickApp, 'manifest.json'), 'utf8'));
  const auth = fs.readFileSync(path.join(system, 'desktop', 'hass_ui_auth.h'), 'utf8');
  const digest = crypto.createHash('sha256').update(source).digest('hex');
  const encoded = [...Buffer.from(digest, 'hex')]
    .map(value => `0x${value.toString(16).padStart(2, '0')}`).join(', ');

  assert.equal(manifest.package, 'com.openvela.homeassistant');
  assert.equal(manifest.versionName, '0.7.0');
  assert.deepEqual(manifest.features,
    ['homeAssistantService', 'homeAssistant', 'storage']);
  assert.match(auth, /com\.openvela\.homeassistant/);
  assert.ok(auth.includes(encoded));
});

test('UI is bounded and does not expose arbitrary HA service calls', () => {
  assert.match(source, /BUTTON_SLOTS = 16/);
  assert.match(source, /MAX_ENTITIES = 128/);
  assert.match(source, /MAX_FAVORITES = 16/);
  assert.match(source, /RESPONSE_LIMIT = 65536/);
  assert.match(source, /\['turn_on', 'turn_off'\]/);
  assert.match(source, /HTTP · 未加密/);
  assert.match(source, /控制结果未确认，请刷新；不会重试/);
  assert.match(source, /ui\.panel\(/);
  assert.match(source, /ui\.setStyle\(slot\.id, button\.style\)/);
  assert.match(source, /radius: 18/);
  assert.doesNotMatch(source, /system\.homeAssistantService\s*=/);
});

test('native service restricts targets, caches states, and clears bad credentials', () => {
  const service = fs.readFileSync(path.join(system, 'hass', 'hass_service.c'), 'utf8');
  const transport = fs.readFileSync(path.join(system, 'hass', 'hass_transport.c'), 'utf8');

  assert.match(service, /HASS_CACHE_TTL_MS 10000/);
  assert.match(service, /"light".*"switch".*"input_boolean".*"fan"/s);
  assert.match(service, /wire\.status == 401 \|\| wire\.status == 403/);
  assert.match(service, /cache_clear\(\)/);
  assert.match(transport, /HA_RESPONSE_MAX 65536/);
  assert.match(transport, /Content-Length/);
  assert.match(transport, /Transfer-Encoding/);
  assert.match(transport, /PTHREAD_CREATE_DETACHED/);
});

function bootApp(width = 1024, height = 600) {
  let nextHandle = 1;
  let nextRequest = 1;
  let timer = null;
  const requests = [];
  const buttons = [];
  const geometry = new Map();
  const responses = {
    config: { location_name: 'Lab' },
    services: [{ domain: 'light', services: { turn_on: {}, turn_off: {} } }],
    states: [
      { entity_id: 'light.desk', state: 'on',
        attributes: { friendly_name: 'Desk', brightness: 128,
          supported_color_modes: ['brightness'] } },
      { entity_id: 'sensor.room_temp', state: '24.5',
        attributes: { friendly_name: 'Room', unit_of_measurement: 'C' } }
    ]
  };
  const service = {
    apiVersion: 1,
    configure: () => 0,
    status: () => ({ configured: true, busy: false,
      url: 'http://192.168.1.20:8123', canControl: true, canConfigure: true }),
    get(resource) {
      const id = nextRequest++;
      requests.push({ requestId: id, busy: false, done: true,
        cached: resource === 'states', status: 200, error: 0,
        body: JSON.stringify(responses[resource]) });
      return id;
    },
    getState: () => -16,
    control: () => -16,
    poll: () => requests.shift() || { busy: false, done: false },
    close: () => {}
  };
  const ui = {
    primary: 0x252d46,
    getSize: () => ({ width, height }),
    background: () => {},
    button(text, x, y, w, h, callback) {
      const handle = nextHandle++;
      buttons.push({ handle, callback });
      geometry.set(handle, { x, y, w, h });
      return handle;
    },
    panel(x, y, w, h) {
      const handle = nextHandle++;
      geometry.set(handle, { x, y, w, h });
      return handle;
    },
    rect(x, y, w, h) {
      const handle = nextHandle++;
      geometry.set(handle, { x, y, w, h });
      return handle;
    },
    text: (text, x, y) => {
      const handle = nextHandle++;
      geometry.set(handle, { x, y, w: 1, h: 1 });
      return handle;
    },
    setColor: () => {}, setHidden: () => {}, setStyle: () => {},
    setPos(handle, x, y) { Object.assign(geometry.get(handle), { x, y }); },
    setSize(handle, w, h) { Object.assign(geometry.get(handle), { w, h }); },
    setText: () => {}, onSwipe: () => {}
  };
  const context = vm.createContext({
    system: { homeAssistantService: service,
      homeAssistant: {}, storage: { get: () => '', set: () => {} } },
    ui, prompt: { input: () => {} }, console,
    setInterval(callback) { timer = callback; return 1; },
    clearInterval() {}
  });
  vm.runInContext(source, context, { filename: 'homeassistant/app.js' });
  return { context, buttons, geometry, tick: () => timer() };
}

test('all fixed-format controls stay inside supported display sizes', () => {
  for (const [width, height] of [[320, 360], [480, 480], [1024, 600]]) {
    const runtime = bootApp(width, height);
    assert.equal(runtime.buttons.length, 16);
    for (const box of runtime.geometry.values()) {
      assert.ok(box.x >= 0 && box.y >= 0 && box.w > 0 && box.h > 0,
        `${width}x${height} has an invalid rectangle ${JSON.stringify(box)}`);
      assert.ok(box.x + box.w <= width && box.y + box.h <= height,
        `${width}x${height} clips ${JSON.stringify(box)}`);
    }
  }
});

test('shared backend completes config, services, and cached state synchronization', () => {
  const runtime = bootApp();
  assert.equal(runtime.buttons.length, 16);
  assert.equal(runtime.context.ESPHomeHA.snapshot().backend, 'native-service');

  runtime.context.ESPHomeHA.refresh();
  runtime.tick();
  runtime.tick();
  runtime.tick();

  const state = runtime.context.ESPHomeHA.snapshot();
  assert.equal(state.connected, true);
  assert.equal(state.phase, 'ready');
  assert.equal(state.lastCached, true);
  assert.equal(state.entities.length, 2);
  assert.equal(state.entities[0].entity_id, 'light.desk');
  assert.equal(state.entities[0].brightness, 50);
  assert.equal(state.entities[1].domain, 'sensor');
});
