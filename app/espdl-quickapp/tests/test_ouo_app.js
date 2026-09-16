// SPDX-License-Identifier: Apache-2.0
const vm = require('node:vm'), fs = require('node:fs'), assert = require('node:assert/strict');
const widgets = [], colors = {}, sizes = {}, positions = {}, styles = {};
const intervals = [], stored = {};
let touch, swipe, timer;
let nextId = 0;
function widget(fill) {
  const id = ++nextId;
  widgets.push(id);
  if (fill !== undefined) colors[id] = fill;
  return id;
}
const ui = {
  width: 1022, height: 536,
  primary: 0x252d46, secondary: 0x616981, surface: 0xe9eafa, card: 0xf5f6fd,
  accent: 0x5860bf,
  getSize: () => ({width: 1022, height: 536}),
  background(color) { this.background_color = color; },
  panel: (x, y, w, h, color) => widget(color),
  text: value => widget(),
  button: value => widget(),
  setColor: (id, color) => { colors[id] = color; },
  setSize: (id, w, h) => { sizes[id] = [w, h]; },
  setPos: (id, x, y) => { positions[id] = [x, y]; },
  setStyle: (id, style) => { styles[id] = style; },
  setText() {}, setHidden() {}, show() {}, hide() {},
  onTouch: fn => { touch = fn; },
  onSwipe: fn => { swipe = fn; }
};
const system = {storage: {get: key => stored[key], set: (key, value) => { stored[key] = value; }}};
const ctx = {ui, system, console: {log() {}},
  setInterval: (fn, period) => { intervals.push(fn); if (!timer) timer = fn; return intervals.length; },
  clearInterval: () => {}, Number, Math, JSON, String, Object, Array, Error};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(process.argv[2], 'utf8'), ctx);
const mood = ctx.VelaMood;
assert(mood, 'VelaMood must be exported');
assert.equal(mood.states.length, 32);
assert.equal(widgets.length, 3, 'Only the backdrop and two eyes are drawn');
// Stored emotion wins; the eyes and backdrop take its colours.
const stand = mood.states.indexOf('02');
assert.equal(colors[widgets[0]], 0x10222c, 'Backdrop uses the state colour');
assert.equal(colors[widgets[1]], 0x73dfc0);
// Idle behaviour still animates in place.
timer(); timer();
const restingY = positions[widgets[1]][1];
assert(Number.isFinite(restingY));
// Touching makes the eyes look towards the finger and open wider.
touch('down', 0.9, 0.35);
for (let i = 0; i < 8; i++) timer();
const touched = positions[widgets[1]][0];
const widen = sizes[widgets[1]][1];
assert(touched > 322, 'Eyes must shift towards the touch');
assert(widen > Math.max(36, 42 * 2), 'Held touch opens the eyes');
// Releasing answers with a squint before settling back.
touch('up', 0.9, 0.35);
timer();
assert(sizes[widgets[1]][1] < widen, 'Release squints');
for (let i = 0; i < 6; i++) timer();
assert(positions[widgets[1]][0] < touched, 'Eyes drift back after release');
// Swiping still switches emotion and persists the manual choice.
const before = colors[widgets[0]];
swipe('left');
assert.notEqual(colors[widgets[0]], before, 'Swipe changes the emotion');
assert.equal(stored.emotion, mood.states[(stand + 1) % mood.states.length]);
// The AI hook keeps working, including its fallback.
assert.equal(mood.handleAIMessage(JSON.stringify({emotionId: '11'})), true);
assert.equal(colors[widgets[0]], 0x32272a);
assert.equal(mood.handleAIMessage('{bad json'), false);
assert.equal(colors[widgets[0]], 0x10222c);
console.log('PASS: OuO touch follow, squint release, swipe switch and AI hook');
