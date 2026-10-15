'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname,
  '../overlay/apps/system/desktop/portal/app.js'), 'utf8');
const bearer = 'a'.repeat(32);

function boot() {
  const nodes = new Map(), intervals = [], calls = [];
  const documentEvents = {}, windowEvents = {};
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, {
      value: '', hidden: false, textContent: '', disabled: false,
      classList: { toggle() {}, add() {} }, addEventListener() {},
      elements: { namedItem: key => node(id + ':' + key) },
      querySelector: selector => node(id + ':' + selector),
      querySelectorAll: () => []
    });
    return nodes.get(id);
  };
  let handler = () => new Response('{}');
  const context = vm.createContext({
    document: { hidden: false, getElementById: node, querySelectorAll: () => [],
      addEventListener: (name, callback) => { documentEvents[name] = callback; } },
    window: { addEventListener: (name, callback) => { windowEvents[name] = callback; } },
    location: { hash: '', pathname: '/', search: '' },
    history: { replaceState() {} },
    sessionStorage: { getItem: () => '', setItem() {}, removeItem() {} },
    Blob, Uint8Array, AbortController, Response, TypeError, TextEncoder,
    FormData: class {
      constructor(form) { return new Map([['url', form.elements.namedItem('url').value],
        ['token', form.elements.namedItem('token').value]]); }
    },
    fetch: async (url, options) => { calls.push({ url, options }); return handler(url, options); },
    setTimeout: (callback, ms) => {
      if (ms < 1000) queueMicrotask(callback);
      return 1;
    },
    clearTimeout() {},
    setInterval: (callback, ms) => { intervals.push({ callback, ms }); return intervals.length; },
    clearInterval() {}
  });
  vm.runInContext(source, context);
  return { context, nodes, node, calls, intervals, documentEvents, windowEvents,
    handle: callback => { handler = callback; },
    evaluate: code => vm.runInContext(code, context),
    connected: async () => {
      vm.runInContext(`token='${bearer}';`, context);
      await vm.runInContext('connect()', context);
      assert.equal(node('workspace').hidden, false);
      calls.length = 0;
    }
  };
}

test('visible configuration form stays paired through more than 15 minutes of editing', async () => {
  const r = boot();
  await r.connected();
  let now = 0, lastAuth = 0;
  r.node('homeassistant:ha_url').value = 'http://draft.local:8123';
  r.handle((url, options) => {
    assert.equal(url, '/api/session');
    assert.equal(options.headers.Authorization, 'Bearer ' + bearer);
    if (now - lastAuth >= 900000) return new Response('{}', { status: 401 });
    lastAuth = now;
    return new Response('{"ok":true}');
  });
  const heartbeat = r.intervals.find(entry => entry.ms === 60000);
  assert.ok(heartbeat);
  for (let i = 0; i < 20; i++) { now += 60000; await heartbeat.callback(); }
  assert.equal(r.evaluate('token'), bearer);
  assert.equal(r.node('homeassistant:ha_url').value, 'http://draft.local:8123');
  assert.equal(r.calls.length, 20);
});

test('transient read failures recover with bounded retries and keep drafts', async () => {
  const r = boot(); await r.connected();
  r.node('homeassistant:ha_url').value = 'http://draft.local:8123';
  let failures = 2;
  r.handle(() => { if (failures-- > 0) throw new TypeError('network'); return new Response('{}'); });
  await r.evaluate("api('session')");
  assert.equal(r.calls.length, 3);
  assert.equal(r.node('workspace').hidden, false);
  assert.equal(r.node('homeassistant:ha_url').value, 'http://draft.local:8123');
});

test('reconnect verifies the existing session without overwriting edited forms', async () => {
  const r = boot(); await r.connected();
  r.node('homeassistant:ha_url').value = 'http://draft.local:8123';
  r.handle(() => { throw new TypeError('offline'); });
  await assert.rejects(r.evaluate("api('session')"), /已保留填写内容/);
  assert.equal(r.calls.length, 3);
  assert.equal(r.evaluate('token'), bearer);
  r.calls.length = 0;
  r.handle(() => new Response('{}'));
  await r.evaluate('connect()');
  assert.deepEqual(r.calls.map(call => call.url), ['/api/session']);
  assert.equal(r.node('workspace').hidden, false);
  assert.equal(r.node('homeassistant:ha_url').value, 'http://draft.local:8123');
});

test('a lost save reply is reported as uncertain and the POST is sent only once', async () => {
  const r = boot(); await r.connected();
  r.node('homeassistant:ha_token').value = 'draft-secret';
  r.handle(() => { throw new TypeError('reply lost'); });
  await assert.rejects(r.evaluate("api('config?section=homeassistant','POST',{ha_url:'http://new.local:8123'})"), /配置可能已保存/);
  assert.equal(r.calls.length, 1);
  assert.equal(r.calls[0].options.method, 'POST');
  assert.equal(r.node('homeassistant:ha_token').value, 'draft-secret');
  r.handle(() => new Response('{}'));
  await r.evaluate('keepSession()');
  assert.equal(r.node('connection-badge').textContent, '已连接设备');
});

test('expired or revoked authorization is not retried and clears the local session', async () => {
  const r = boot(); await r.connected();
  r.handle(() => new Response('{"error":"连接已失效"}', { status: 401 }));
  await r.evaluate('keepSession()');
  assert.equal(r.calls.length, 1);
  assert.equal(r.evaluate('token'), '');
  assert.equal(r.node('workspace').hidden, true);
});

test('heartbeat skips hidden pages and active requests, then resumes without reloading forms', async () => {
  const r = boot(); await r.connected();
  r.context.document.hidden = true;
  await r.evaluate('keepSession()');
  assert.equal(r.calls.length, 0);
  r.context.document.hidden = false;
  let release;
  r.handle(() => new Promise(resolve => { release = resolve; }));
  const read = r.evaluate("api('config?section=homeassistant')");
  await r.evaluate('keepSession()');
  assert.equal(r.calls.length, 1);
  release(new Response('{}')); await read;
  r.handle(() => new Response('{}'));
  await r.documentEvents.visibilitychange();
  // Event callback schedules an async check; wait for it to finish.
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(r.calls.at(-1).url, '/api/session');
});

test('late responses cannot reconnect a page after an explicit disconnect', async () => {
  const r = boot(); await r.connected();
  r.evaluate('connectionInterrupted=true');
  let release;
  r.handle(() => new Promise(resolve => { release = resolve; }));
  const read = r.evaluate("api('session')");
  r.node('disconnect').onclick();
  release(new Response('{}')); await read;
  assert.equal(r.evaluate('token'), '');
  assert.equal(r.node('workspace').hidden, true);
});

test('a lost code-exchange reply is retried using the same pairing code', async () => {
  const r = boot();
  let first = true;
  r.handle(url => {
    if (url === '/api/pair') {
      if (first) { first = false; throw new TypeError('reply lost'); }
      return new Response(JSON.stringify({ token: bearer }));
    }
    return new Response('{}');
  });
  await r.evaluate("connect('123456')");
  assert.equal(r.node('workspace').hidden, false);
  const exchanges = r.calls.filter(call => call.url === '/api/pair');
  assert.equal(exchanges.length, 2);
  assert.equal(exchanges[0].options.body, exchanges[1].options.body);
});

test('a saved configuration remains reported as saved if its follow-up read fails', async () => {
  const r = boot(); await r.connected();
  r.node('homeassistant:url').value = 'http://new.local:8123';
  r.handle((url, options) => {
    if(options.method === 'POST') return new Response('{"ok":true}');
    throw new TypeError('read failed');
  });
  r.node('homeassistant').onsubmit({ preventDefault() {}, target: r.node('homeassistant') });
  await new Promise(resolve => setImmediate(resolve));
  assert.match(r.node('status').textContent, /^配置已保存/);
  assert.equal(r.calls.filter(call => call.options.method === 'POST').length, 1);
  assert.equal(r.node('homeassistant:button').disabled, false);
});

test('a failed save keeps password drafts and re-enables the save button', async () => {
  const r = boot(); await r.connected();
  r.node('homeassistant:url').value = 'http://new.local:8123';
  r.node('homeassistant:token').value = 'draft-secret';
  r.handle(() => { throw new TypeError('save reply lost'); });
  r.node('homeassistant').onsubmit({ preventDefault() {}, target: r.node('homeassistant') });
  await new Promise(resolve => setImmediate(resolve));
  assert.match(r.node('status').textContent, /配置可能已保存/);
  assert.equal(r.calls.length, 1);
  assert.equal(r.node('homeassistant:token').value, 'draft-secret');
  assert.equal(r.node('homeassistant:button').disabled, false);
});

test('HA slash-port input is rejected before POST and preserves the token draft', async () => {
  const r=boot();await r.connected();
  r.node('homeassistant:url').value='http://124.223.30.246/8123';
  r.node('homeassistant:token').value='draft-secret';
  r.node('homeassistant').onsubmit({preventDefault(){},target:r.node('homeassistant')});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(r.calls.length,0);
  assert.match(r.node('status').textContent,/端口前用冒号/);
  assert.equal(r.node('homeassistant:token').value,'draft-secret');
  assert.equal(r.node('homeassistant:button').disabled,false);
});

test('HA colon-port save trims copied credentials and a blank token preserves the stored one', async () => {
  const r=boot();await r.connected();r.node('homeassistant:url').value=' http://124.223.30.246:8123 ';
  r.node('homeassistant:token').value='  copied-token\n';
  r.handle(()=>new Response('{}'));
  r.node('homeassistant').onsubmit({preventDefault(){},target:r.node('homeassistant')});
  await new Promise(resolve=>setImmediate(resolve));
  let post=r.calls.find(c=>c.options.method==='POST');
  assert.deepEqual(JSON.parse(post.options.body),{url:'http://124.223.30.246:8123',token:'copied-token'});
  r.calls.length=0;r.node('homeassistant:token').value='';
  r.node('homeassistant').onsubmit({preventDefault(){},target:r.node('homeassistant')});
  await new Promise(resolve=>setImmediate(resolve));
  post=r.calls.find(c=>c.options.method==='POST');assert.equal('token' in JSON.parse(post.options.body),false);
});
