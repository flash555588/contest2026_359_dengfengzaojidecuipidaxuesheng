const {test} = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..', 'overlay', 'apps', 'system', 'desktop');
const engine = fs.readFileSync(path.join(root, 'dafeiyu/engine.js'), 'utf8');
const ui = fs.readFileSync(path.join(root, 'dafeiyu/app.js'), 'utf8');
const code = engine + '\n' + ui;

function mockUi(labels, nextRef) {
  return {
    getSize: () => ({width: 960, height: 470}),
    background() {},
    panel() { return nextRef.n++; },
    rect() { return nextRef.n++; },
    text: (text) => {
      const id = nextRef.n++;
      labels.set(id, {text, color: 0});
      return id;
    },
    number: (text) => {
      const id = nextRef.n++;
      labels.set(id, {text, color: 0});
      return id;
    },
    button: (text) => {
      const id = nextRef.n++;
      labels.set(id, {text, color: 0});
      return id;
    },
    setText: (id, text) => { if (labels.get(id)) labels.get(id).text = text; },
    setColor: (id, color) => { if (labels.get(id)) labels.get(id).color = color; },
    setPos() {},
    setSize() {},
    setOpacity() {}
  };
}

function setup(store = {}, extra = {}) {
  const labels = new Map();
  const toasts = [];
  const inputs = [];
  const nextRef = { n: 1 };
  let tick;
  const context = {
    ui: mockUi(labels, nextRef),
    system: {
      storage: {
        get: (key) => store[key] == null ? null : store[key],
        set: (key, value) => { store[key] = String(value); },
        delete: (key) => { delete store[key]; }
      },
      pet: extra.pet
    },
    prompt: {
      showToast: (text) => toasts.push(text),
      dialog: () => {},
      input: (opts, cb) => {
        inputs.push(opts);
        if (extra.inputValue) cb(extra.inputValue);
      }
    },
    setInterval: (callback) => { tick = callback; return 1; },
    globalThis: {}
  };
  context.globalThis = context;
  vm.runInNewContext(code, context);
  return {
    api: context.Dafeiyu || context.globalThis.Dafeiyu,
    store, toasts, inputs, tick, labels
  };
}

test('starts in wander with a visible pet', () => {
  const s = setup();
  const state = s.api.getState();
  assert.equal(state.mode, 'wander');
  assert.equal(state.visible, true);
  assert.equal(state.mood, 72);
});

test('poke and feed change bubble and meters', () => {
  const s = setup();
  s.api.poke();
  assert.ok(s.api.getState().bubble.length > 0);
  s.api.feed('fish');
  assert.match(s.api.getState().bubble, /小鱼干/);
  assert.ok(s.api.getState().hunger < 28);
});

test('chat uses local lexicon', () => {
  const s = setup();
  s.api.chat('你好');
  assert.match(s.api.getState().reply, /碳基/);
});

test('uses native system.pet when hosted', () => {
  const calls = [];
  const hosted = {
    getState: () => ({
      mode: 'still', dir: 'down', size: 'medium', facing: 1,
      x: 10, y: 10, w: 96, h: 88, mood: 80, hunger: 10,
      visible: true, dragging: false, jump: 0, eventSeq: 2,
      bubble: '本地鱼', inner: false, event: 'poke', reply: '', toast: 'hi'
    }),
    setMode: (mode) => { calls.push(['setMode', mode]); return hosted.getState(); },
    poke: () => { calls.push(['poke']); return hosted.getState(); },
    feed: (food) => { calls.push(['feed', food]); return hosted.getState(); },
    say: () => hosted.getState(),
    chat: () => hosted.getState(),
    toggle: () => hosted.getState(),
    setSize: () => hosted.getState()
  };
  const s = setup({}, {pet: hosted});
  s.api.setMode('follow');
  s.api.poke();
  s.api.feed('cake');
  assert.deepEqual(calls[0], ['setMode', 'follow']);
  assert.deepEqual(calls[1], ['poke']);
  assert.deepEqual(calls[2], ['feed', 'cake']);
  assert.equal(s.toasts.includes('hi'), true);
});
