// SPDX-License-Identifier: Apache-2.0
// Business behavior and layout bounds; UI is an explicit fixture, not LVGL.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

function launch(source, options = {}) {
  const W = options.width || 1024, H = options.height || 536;
  const widgets = new Map(), timers = new Map(), values = {...options.values};
  let id = 0, timerId = 0, failWrite = false, swipe;
  const notices = [];
  function add(type, text, x, y, width = 0, height = 0, callback) {
    assert(widgets.size < 64, 'widget limit');
    assert(Number.isFinite(x) && Number.isFinite(y));
    assert(x >= 0 && y >= 0 && x < W && y < H, `position outside ${W}x${H}: ${x},${y}`);
    if (width) assert(x + width <= W && width > 0, 'width outside viewport');
    if (height) assert(y + height <= H && height > 0, 'height outside viewport');
    const handle = ++id;
    widgets.set(handle, {type, text: String(text), x, y, width, height, callback});
    return handle;
  }
  const get = handle => { assert(widgets.has(handle), 'stale widget'); return widgets.get(handle); };
  const palette = options.dark ? {primary: 0xf4f2ff, secondary: 0xabb3cd, surface: 0x303752, card: 0x202641, accent: 0xb4a6ff} :
    {primary: 0x252d46, secondary: 0x616981, surface: 0xe9eafa, card: 0xf5f6fd, accent: 0x5860bf};
  const ui = new Proxy({
    ...palette, getSize: () => ({width: W, height: H}), background() {},
    text: (text, x, y) => add('text', text, x, y),
    number: (text, x, y, w, h) => add('number', text, x, y, w, h),
    panel: (x, y, w, h) => add('panel', '', x, y, w, h),
    button(text, x, y, w, h, callback) {
      assert.equal(typeof callback, 'function', 'button callback is argument six');
      return add('button', text, x, y, w, h, callback);
    },
    setText(handle, text) { get(handle).text = String(text); },
    setSize(handle, w, h) {
      const item = get(handle); assert(w > 0 && h > 0);
      assert(item.x + w <= W && item.y + h <= H); item.width = w; item.height = h;
    },
    setPos(handle, x, y) { Object.assign(get(handle), {x, y}); },
    setColor(handle, color) { get(handle).color = color; },
    setStyle(handle, style) {
      get(handle);
      for (const key of Object.keys(style)) assert(['radius', 'borderColor', 'borderWidth', 'borderOpacity', 'textColor', 'fontSize', 'center', 'ellipsis', 'enabled'].includes(key));
    },
    setHidden(handle, hidden) { get(handle).hidden = hidden; },
    onSwipe(fn) { assert.equal(typeof fn, 'function'); swipe = fn; },
    onTouch(fn) { assert.equal(typeof fn, 'function'); },
    remove(handle) { get(handle); widgets.delete(handle); }
  }, {get(target, key) { assert(key in target, `unknown API ui.${String(key)}`); return target[key]; }});
  const context = vm.createContext({ui,
    app: {getInfo: () => ({name: '参考项目', packageName: 'fixture.example', versionName: '1.0'})},
    system: {storage: {
      get(key) { return Object.hasOwn(values, key) ? values[key] : null; },
      set(key, value) {
        assert(/^[A-Za-z0-9_-]{1,64}$/.test(key)); assert.equal(typeof value, 'string');
        if (failWrite) throw Error('storage fixture failure'); values[key] = value;
      },
      delete(key) { delete values[key]; }
    }},
    prompt: {showToast: value => notices.push(value), dialog: value => notices.push(value)},
    console: {log() {}},
    setInterval(fn, ms) { assert.equal(typeof fn, 'function'); assert(ms >= 20 && timers.size < 8); timers.set(++timerId, fn); return timerId; },
    clearInterval(timer) { timers.delete(timer); }
  });
  const evaluate = text => vm.runInContext(text, context, {timeout: 1000});
  evaluate('Math.random = function () { return 0; };');
  evaluate(source);
  return {widgets, values, notices, timers, evaluate,
    failWrites: () => { failWrite = true; },
    click(text) {
      const button = [...widgets.values()].find(item => item.type === 'button' && item.text === text);
      assert(button, `missing button ${text}`); button.callback();
    },
    swipe: direction => { assert(swipe); swipe(direction); }
  };
}

const root = process.argv[2];
const hello = launch(fs.readFileSync(path.join(root, 'hello/app.js'), 'utf8'), {values: {launches: '7'}});
assert.equal(hello.values.launches, '8');
hello.click('Toast 提示'); hello.click('对话框'); assert.equal(hello.notices.length, 2);
const beforeHello = hello.widgets.size;
for (let i = 0; i < 120; i++) for (const timer of hello.timers.values()) timer();
assert.equal(hello.widgets.size, beforeHello);
assert([...hello.widgets.values()].some(item => item.text === '已运行 120 秒'));

const game = launch(fs.readFileSync(path.join(root, 'game2048/app.js'), 'utf8'));
assert.equal(game.evaluate("JSON.stringify(slide([2,2,2,2]).line)"), '[4,4,0,0]');
assert.equal(game.evaluate("JSON.stringify(slide([4,4,8,0]).line)"), '[8,8,0,0]', 'each tile merges once');
game.evaluate("board.splice(0,16,1024,1024,0,0,0,0,0,0,0,0,0,0,0,0,0,0); score=0; gameOver=false; won=false; move('left');");
assert.equal(game.evaluate('board[0]'), 2048); assert.equal(game.evaluate('score'), 2048);
assert.equal(game.values.best, '2048');
game.evaluate('board.splice(0,16,2,4,2,4,4,2,4,2,2,4,2,4,4,2,4,2); gameOver=false;');
assert.equal(game.evaluate('canMove()'), false); game.swipe('left');
assert.equal(game.evaluate('gameOver'), true);
game.click('重新开始');
assert.equal(game.evaluate('score'), 0); assert.equal(game.evaluate('gameOver'), false);
const beforeGame = game.widgets.size;
for (let i = 0; i < 100; i++) game.swipe(i % 2 ? 'down' : 'right');
assert.equal(game.widgets.size, beforeGame, 'moves reuse the same board widgets');

const counter = fs.readFileSync(process.argv[3], 'utf8');
for (const dark of [false, true]) for (const width of [800, 1024]) {
  const app = launch(counter, {dark, width, height: width === 800 ? 480 : 536, values: {count: '4'}});
  app.click('加一'); assert.equal(app.values.count, '5');
  app.click('减一'); assert.equal(app.values.count, '4');
  app.click('清零'); assert.equal(app.values.count, '0');
  app.failWrites(); app.click('加一'); assert.equal(app.evaluate('count'), 1); assert.equal(app.values.count, '0');
  assert([...app.widgets.values()].some(item => item.text.includes('未保存')));
}
const corrupted = launch(counter, {values: {count: 'bad value'}});
assert.equal(corrupted.evaluate('count'), 0); assert.equal(corrupted.values.count, 'bad value');
assert([...corrupted.widgets.values()].some(item => item.text.includes('旧数据无效')));
corrupted.click('加一'); assert.equal(corrupted.values.count, '1');
console.log('PASS: Hello persistence/timers, 2048 merges/win/loss/restart/widget reuse, guide counter actions/storage failure/corrupt data and two sizes/themes');
