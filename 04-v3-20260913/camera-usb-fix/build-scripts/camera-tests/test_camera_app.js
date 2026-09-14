const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const path = require('path');
const file = path.resolve(__dirname, '../../04-v3-20260913/camera-usb-fix/overlay/apps/system/desktop/camera/app.js');
const source = fs.readFileSync(file, 'utf8');
function harness(initial) {
  let devices = initial, timer, busy = false, error = 0;
  const items = [], starts = [];
  function add(o) { items.push(o); return items.length; }
  const camera = {
    devices: () => devices, status: () => ({busy, error, frames: 0}),
    modes: () => [{index: 0, width: 640, height: 480, defaultInterval: 333333, intervals: [333333, 666666], continuous: false}],
    startDevice: (...args) => { starts.push(args); busy = true; return true; },
    stop: () => {}, frames: () => 0,
  };
  const ui = {
    getSize: () => ({width: 942, height: 446}), background: () => {},
    text: (text,x,y,size,color) => add({text,x,y,w:0,h:size,kind:'text'}),
    button: (text,x,y,w,h,click) => add({text,x,y,w,h,click,kind:'button'}),
    setText: (id,text) => { items[id-1].text = text; },
    setHidden: (id,hidden) => { items[id-1].hidden = hidden; },
  };
  const ctx = {ui, system: {camera}, setInterval: fn => timer=fn, Date, console};
  vm.runInNewContext(source, ctx);
  return {items, starts, tick: () => timer(), api: ctx.CameraPreview,
    setDevices: d => devices=d, setBusy: b => busy=b,
    click: text => items.find(x => x.text === text).click()};
}
const usb = n => ({id:'usb'+n, name:'Camera '+n, generation:n+3, transport:'usb', previewSupported:true});
let h = harness([]);
assert(h.items.some(x => x.text.includes('已连接 0')));
h.click('开始预览'); h.tick(); assert.equal(h.starts.length,0);
h.setDevices([usb(0)]); h.tick(); h.click('开始预览'); h.tick();
assert.deepEqual(h.starts[0], ['usb0',3,0,666666]);
// While cleanup is still in progress, another start must not claim buffers.
h.api.stop(); h.click('开始预览'); h.tick(); assert.equal(h.starts.length,1);
h.setBusy(false); h.tick(); assert.equal(h.starts.length,2);
// An unplugged device is removed, and a reconnect has a distinct generation.
h.setDevices([]); h.setBusy(false); h.tick();
assert(h.items.some(x => x.text.includes('已连接 0')));
h.setDevices([{...usb(0), generation:33}]); h.tick(); h.click('开始预览'); h.tick();
assert.equal(h.starts.at(-1)[1],33);
const csi = {id:'csi0',name:'SC2336',generation:1,transport:'csi',previewSupported:true};
h = harness([csi, usb(0), usb(1), usb(2), usb(3)]);
assert(h.items.some(x => x.text.includes('已连接 5') && x.text.includes('CSI 1')));
const buttons = h.items.filter(x => x.kind==='button' && !x.hidden);
for (const x of buttons) {
  assert(x.x>=0 && x.y>=0 && x.x+x.w<=942 && x.y+x.h<=446);
  for (const y of buttons) if (x!==y)
    assert(x.x+x.w<=y.x || y.x+y.w<=x.x || x.y+x.h<=y.y || y.y+y.h<=x.y, 'buttons overlap');
}
h.items.find(x => x.text.includes('Camera 2') && x.kind==='button').click();
h.click('开始预览'); h.tick(); assert.equal(h.starts[0][0],'usb2');
console.log('PASS: camera UI empty/one/five devices, CSI/USB selection, cleanup wait, reconnect generation, bounds and overlap');
