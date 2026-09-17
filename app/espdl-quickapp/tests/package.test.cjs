'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '../overlay/apps/system/desktop');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

test('builtin and standalone scripts come from the same current sources', () => {
  const source = Buffer.concat([fs.readFileSync(path.join(root, 'dafeiyu/engine.js')),
    Buffer.from('\n'), fs.readFileSync(path.join(root, 'dafeiyu/app.js'))]);
  assert.deepEqual(fs.readFileSync(path.join(root, 'dafeiyu/qpk/app.js')), source);
  const resource = read('dafeiyu_resource.c');
  const array = resource.match(/g_dafeiyu_app_js\[\]\s*=\s*\{([\s\S]*?)\};/)[1];
  const bytes = Buffer.from([...array.matchAll(/0x([0-9a-f]{2})/g)].map(m => parseInt(m[1],16)));
  assert.deepEqual(bytes, Buffer.concat([source, Buffer.from([0])]));
  assert.match(resource, /sizeof\(g_dafeiyu_app_js\) - 1/);
  const manifest = JSON.parse(read('dafeiyu/manifest.json'));
  assert.equal(manifest.versionName, '1.0.1');
  assert.deepEqual(JSON.parse(read('dafeiyu/qpk/manifest.json')), manifest);
});

test('privileged identity is checked before runtime state is changed', () => {
  const runtime = read('qpk_runtime.c');
  const launch = runtime.slice(runtime.indexOf('int qpk_runtime_launch('));
  assert(launch.indexOf('qpk_launch_identity_valid(') < launch.indexOf('qpk_runtime_stop();'));
  assert.match(read('desktop_main.c'), /!qpk_reserved_package\(entry->package\)/);
  assert.match(runtime, /g_qpk.ha_config_access = qpk_builtin_ha_origin\(package, filename\)/);
  assert.match(runtime, /if\(g_qpk.ha_config_access &&/);
  assert.match(runtime, /if\(!ret&&g_qpk.ha_config_access &&/);
  assert.doesNotMatch(runtime, /strcmp\(g_qpk.package,"com.openvela.homeassistant"\)/);
});

test('runtime releases generations and exposes pet diagnostic commands', () => {
  const runtime = read('qpk_runtime.c');
  assert.equal((runtime.match(/free\(g_qpk.widget_generations\)/g) || []).length, 2);
  const probe = read('glass_ui_probe.inc').split('static int desktop_ui_probe_request')[1];
  assert.match(probe, /"dafeiyu", "pet"/);
});
