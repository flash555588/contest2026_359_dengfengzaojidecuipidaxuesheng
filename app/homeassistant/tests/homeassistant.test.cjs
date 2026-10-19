const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const appRoot = path.join(__dirname, '..');
const publishRoot = path.join(appRoot, '..', '..');
const system = path.join(publishRoot, 'app', 'espdl-quickapp', 'overlay',
  'apps', 'system');
const read = (...parts) => fs.readFileSync(path.join(system, ...parts), 'utf8');

test('native service accepts HTTP hosts and replaces active configuration', () => {
  const service = read('hass', 'hass_service.c');
  const transport = read('hass', 'hass_transport.c');
  const validation = read('hass', 'hass_config_validate.h');

  assert.match(service, /hass_config_url_valid\(url\)/);
  assert.match(validation, /static inline bool hass_config_url_valid/);
  assert.doesNotMatch(validation, /static bool local_url|inet_pton/);
  assert.match(service, /HASS_CACHE_TTL_MS 10000/);
  assert.match(service, /"light".*"switch".*"input_boolean".*"fan"/s);
  assert.match(service, /wire\.status == 401 \|\| wire\.status == 403/);
  assert.match(service, /if \(owner\) hass_transport_stop\(\)/);
  assert.match(service, /-ECANCELED/);
  assert.match(transport, /HA_RESPONSE_MAX \(256 \* 1024 \+ 4096\)/);
  assert.match(transport, /PTHREAD_CREATE_DETACHED/);
});

test('portal keeps an omitted token and applies Home Assistant saves', () => {
  const portal = read('desktop', 'portal', 'app.js');
  const http = read('desktop', 'glass_portal_http.c');
  const configuration = read('desktop', 'glass_portal_config.c');

  assert.match(portal, /if\(data\[key\]===''\)delete data\[key\]/);
  assert.match(portal, /智能家居配置已保存并应用/);
  assert.ok(http.includes('portal_config_save(arg,o)'));
  const save = configuration.indexOf('qpk_storage_write(PORTAL_CONFIG_ROOT');
  const apply = configuration.indexOf('hass_configure(ha_client,url,token,true)', save);
  assert.ok(save >= 0 && apply > save);
});

test('desktop opens the reviewed app.js UI on the shared native service', () => {
  const makefile = read('desktop', 'Makefile');
  const desktop = read('desktop', 'desktop_main.c');
  const glass = read('desktop', 'glass_ui.inc');
  const runtime = read('desktop', 'qpk_runtime.c');
  const ui = fs.readFileSync(path.join(publishRoot, 'quickapp',
    'homeassistant', 'app.js'), 'utf8');
  const resource = fs.readFileSync(path.join(system, 'desktop',
    'homeassistant_resource.c'));
  const generated = fs.readFileSync(path.join(appRoot,
    'homeassistant_resource.c'));

  assert.match(makefile, /camera_resource\.c/);
  assert.match(makefile, /recorder_resource\.c/);
  assert.match(makefile, /homeassistant_resource\.c/);
  assert.deepEqual(resource, generated);
  assert.match(desktop, /static const struct builtin_qpk_s g_builtin_homeassistant_qpk/);
  assert.match(desktop, /qpk_runtime_launch\(card, manifest->name, manifest->package,/);
  assert.match(glass, /static void glass_homeassistant[\s\S]*launch_builtin_homeassistant_qapp\(e\)/);
  assert.doesNotMatch(glass, /#include "glass_hass_ui\.inc"/);

  assert.match(runtime, /hass_qjs_install\(context, system, g_qpk\.hass_grants\)/);
  assert.match(ui, /const nativeService = system\.homeAssistantService/);
  assert.match(ui, /function sharedAdapter\(\)/);
  assert.match(ui, /VERIFY_ATTEMPTS = 4/);
  assert.match(ui, /VERIFY_WINDOW = 8000/);
  assert.match(ui, /nativeService\.getState\(entity\)/);
  assert.match(ui, /控制已受理，正在核对状态/);
});
