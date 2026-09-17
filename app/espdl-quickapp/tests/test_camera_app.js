// SPDX-License-Identifier: Apache-2.0
const vm = require('node:vm'), fs = require('node:fs'), assert = require('node:assert/strict');
const widgets = [], properties = [], calls = [];
let timer, state = {busy:false, error:0};
let photoStatus = 0, photoPath = '';
function widget(label = '') { widgets.push(label); properties.push({}); return widgets.length - 1; }
const ui = {getSize:()=>({width:1022,height:536}), primary:0x252d46, secondary:0x616981,
  surface:0xe9eafa, card:0xf5f6fd, accent:0x5860bf, background(){},
  setSize:(id,w,h)=>Object.assign(properties[id],{w,h}),
  setStyle:(id,style)=>Object.assign(properties[id],style),
  setHidden:(id,hidden)=>properties[id].hidden=hidden, setColor(){},
  panel:()=>widget(), rect:()=>widget(), text:widget, button:widget, setText:(id,s)=>widgets[id]=s};
let deviceList = [{id:'usb:0',generation:7,transport:'usb',previewSupported:true,name:'HD Web Camera',
                   modes:[{width:640,height:480,index:1,continuous:true,intervals:[333333,1000000,333333],
                           defaultInterval:666666}]}];
const system = {camera:{
  devices:()=>deviceList, modes:()=>deviceList[0].modes, frames:()=>12,
  photoStatus:()=>({status:photoStatus, path:photoPath}),
  status:()=>state,
  stop(){calls.push('stop'); state={busy:false, error:0};},
  startDevice:(id,generation,mode,interval)=>{calls.push(['start',id,generation,mode,interval]); state={busy:true, error:0}; return true;}
}};
const ctx = {ui, system, Date, Math, Array, Object, String, setInterval:fn=>{timer=fn;}};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(process.argv[2], 'utf8'), ctx);
const app = ctx.CameraPreview;
assert(widgets.length <= 64, 'The camera page must fit the widget budget');
const startButton = widgets.indexOf('开始预览'), stopButton = widgets.indexOf('停止');
assert(startButton > 0 && stopButton > 0);
// A detected device is selected automatically and renames the row.
assert(widgets.some(text => text.includes('HD Web Camera')), widgets.join('|'));
assert(widgets.some(text => text.includes('MJPEG')), 'The mode button must report the MJPEG mode');
assert.equal(properties[startButton].enabled, true);
assert.equal(properties[stopButton].enabled, false);
// Selecting and starting keeps the existing single-device lifecycle.
app.start();
assert(calls.includes('stop'), 'Starting must release a previous session first');
timer();                    // pending -> startDevice
assert.equal(calls.filter(item => Array.isArray(item)).length, 1);
assert.equal(calls.find(item => Array.isArray(item))[3], 1, 'The 640x480 mode index is used');
assert.equal(properties[stopButton].enabled, true);
state = {busy:false, error:0};
timer();
assert(widgets.some(text => text.includes('预览已停止')), widgets.join('|'));
assert.equal(properties[startButton].enabled, true);
// A capture taken during the session is reported on the page after closing it.
app.start(); timer();
photoStatus = 2; photoPath = '/data/photos/camera-00000007.jpg';
state = {busy:false, error:0};
timer();
assert(widgets.some(text => text.includes('camera-00000007.jpg')), 'The saved photo must be reported');
// A previous session's photo must not be reported again.
app.start(); timer();
state = {busy:false, error:0};
timer();
assert(!widgets[widgets.length - 1].includes('camera-00000007.jpg'));
// Disconnecting the only camera must clear the selection without crashing.
deviceList = [];
app.refresh();
assert(widgets.some(text => text.includes('未检测到摄像头')) || widgets.some(text => text.includes('未连接摄像头')));
assert.equal(properties[startButton].enabled, false);
app.stop();
assert(widgets.some(text => text.includes('未检测到')));
console.log('PASS: camera device list, MJPEG mode, start/stop lifecycle and disconnect');
