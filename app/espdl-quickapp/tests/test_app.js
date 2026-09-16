// SPDX-License-Identifier: Apache-2.0
const vm = require('node:vm'), fs = require('node:fs'), assert = require('node:assert/strict');
const widgets = [], calls = [], properties = [];
function widget(label = '') { widgets.push(label); properties.push({}); return widgets.length - 1; }
let state = {busy:false}, timer, startId = 1, render = true;
const ui = {getSize:()=>({width:1022,height:536}), primary:0x252d46, secondary:0x616981,
  surface:0xe9eafa, card:0xf5f6fd, accent:0x5860bf, background(){},
  setSize:(id,w,h)=>Object.assign(properties[id],{w,h}),
  setStyle:(id,style)=>Object.assign(properties[id],style),
  setHidden:(id,hidden)=>properties[id].hidden=hidden, setColor(){},
  panel:()=>widget(), rect:()=>widget(), text:widget, button:widget, setText:(id,s)=>widgets[id]=s};
let canvas;
const dl = {preview(x,y,w){ assert.equal(w,480); canvas=widget(); return canvas; },clear(){calls.push('clear');},status:()=>state,
  cancel(){calls.push('cancel');},start:(...args)=>{calls.push(args);state={busy:true,request:startId,stage:2};return startId++;},
  render:(id,f)=>{calls.push(['render',id,f]);return render;}};
const ctx = {ui,system:{espdl:dl,camera:{devices:()=>[{id:'usb:0',generation:7,transport:'usb',previewSupported:true,name:'Camera'}]}},
  setInterval:fn=>{timer=fn;}};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(process.argv[2],'utf8'),ctx);
const app=ctx.EspDlApp;
assert(widgets.length <= 64); assert.equal(properties[canvas].hidden,true);
const startButton=widgets.indexOf('开始跟随'), stopButton=widgets.indexOf('停止');
assert.equal(properties[startButton].enabled,true); assert.equal(properties[stopButton].enabled,false);
app.start();assert.equal(calls.find(Array.isArray)[0],1);
assert.equal(properties[startButton].enabled,false); assert.equal(properties[stopButton].enabled,true);
state={busy:true,request:1,frame:1,preview:true,error:0,elapsedMs:40,
  track:{state:1,id:1,target:0,offsetX:250,offsetY:0},items:[{label:'人脸',score:.9}]};
timer();assert(widgets.some(s=>s.includes('向右')));
assert.equal(properties[canvas].hidden,false);
state={...state,frame:2,track:{state:2,id:1,target:-1,offsetX:0,offsetY:0},items:[]};
timer();assert(widgets.includes('目标暂时丢失'));
assert(!widgets.includes('25.0%'), 'Lost targets must not retain old offsets');
app.selectMode(0);app.start();assert.equal(calls.filter(x=>Array.isArray(x)&&typeof x[0]==='number').length,1);
assert.equal(properties[canvas].hidden,true);
state={busy:false,request:1};timer();app.start();assert.equal(state.request,2);
state={busy:false,request:2,frame:1,preview:true,error:0,elapsedMs:300,items:[{label:'cup',score:.8}]};
render=false;timer();assert(!widgets.includes('cup'));assert.equal(properties[canvas].hidden,true);
render=true;timer();assert(widgets.includes('cup'));assert.equal(properties[canvas].hidden,false);
assert.equal(properties[startButton].enabled,true);assert.equal(properties[stopButton].enabled,false);
app.start();state={busy:false,request:3,error:-19};timer();assert(widgets.some(s=>s.includes('相机已断开')));
app.cancel();assert(calls.includes('cancel'));
assert.equal(properties[canvas].hidden,true);
assert(!widgets.some(s=>s.includes('数字')||s.includes('行人')));
console.log('PASS: two modes, continuous updates, matching frame render, cancellation, mode switch and disconnect');
fs.writeFileSync(require('node:path').join(__dirname,'../evidence/js-validation.json'),
  JSON.stringify({twoModes:true,matchingFrameRender:true,cancelAndSwitch:true,disconnect:true},null,2)+'\n');
