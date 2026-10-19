/* SPDX-License-Identifier: MIT */
const assert = require('node:assert/strict'), fs = require('node:fs'), {performance}=require('node:perf_hooks');
const hot = require('../port/frame-hot.js');
require('../port/sprites.js');
const Sprites = global.JizuraPortSprites;
const oldText = fs.readFileSync(__dirname+'/fixtures/stage1-sprites.js','utf8');
const start=oldText.indexOf('      const write = (i, a) =>');
const end=oldText.indexOf('      x.putImageData(image, 0, 0);',start);
assert(start>0&&end>start);
const oldFill=new Function('mask','d',oldText.slice(start,end));
let seed=852,cases=0;
const random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
for(const n of [1,17,65535,65536,743*743])for(const kind of ['zero','solid','ramp','runs','random']){
  const a=new Uint8Array(n);
  for(let i=0;i<n;i++)a[i]=kind==='zero'?0:kind==='solid'?123:kind==='ramp'?i%256:kind==='runs'?(i%200<100?0:255):(random()*256|0);
  const mask=new Sprites().registerMask(a,n,1),old=new Uint8ClampedArray(n*4),next=new Uint8ClampedArray(n*4);
  oldFill(mask,old);Sprites.fillMask(mask,next);assert.deepEqual(next,old);cases++;
}
function camera(events,t,start=0){let spike=0,shake=0;for(let i=start;i<events.length;i++){const ev=events[i];if(ev.t>t)break;const dt=(t-ev.t)*24;if(dt>14)continue;if(ev.type==='chroma')spike+=ev.amp*Math.pow(.55,dt);else if(ev.type==='shake')shake+=ev.amp*Math.pow(.62,dt);}return [spike,shake];}
const events=Array.from({length:10000},(_,i)=>({t:i*.03,amp:random(),dur:i%97===0?400:random(),type:i%2?'shake':'chroma'}));
for(let i=0;i<1000;i++){
  const t=i<3?[0,14/24,300][i]:random()*350;
  assert.deepEqual(camera(events,t),camera(events,t,hot.recentStart(events,t)));
  assert.deepEqual(hot.activeEvents(events,t,24),events.filter(ev=>t>=ev.t&&t<ev.t+Math.max(ev.dur,1/24)));cases++;
}
// Replacements, long durations, reverse seeks and exact interval boundaries.
for(const ev of events)ev.dur=600;
for(const t of [300,0,1,-1,14/24,14/24+Number.EPSILON,14/24-Number.EPSILON]){
  assert.deepEqual(camera(events,t),camera(events,t,hot.recentStart(events,t)));
  assert.deepEqual(hot.activeEvents(events,t,60),events.filter(ev=>t>=ev.t&&t<ev.t+Math.max(ev.dur,1/60)));cases++;
}
for(const duration of [undefined,null,NaN,-Infinity,Infinity,0,-0,'2','invalid',true,false])for(const fps of [24,60,0,-1,Infinity,NaN]){
  const e=[{t:0,dur:duration}];
  assert.deepEqual(hot.activeEvents(e,.01,fps),e.filter(ev=>.01>=ev.t&&.01<ev.t+Math.max(ev.dur,1/fps)));cases++;
}
let created=0;const ctx={createRadialGradient(...args){created++;return {args,stops:[],addColorStop(...v){this.stops.push(v);}}}},r={};
const b=hot.background(r,ctx,1920,1080,.5,'white');
assert.equal(hot.background(r,ctx,1920,1080,.5,'white'),b);
assert.notEqual(hot.background(r,ctx,1920,1080,.25,'white'),b);
const v=hot.vignette(r,ctx,640,360,.2);assert.equal(hot.vignette(r,ctx,640,360,.2),v);
assert.notEqual(hot.vignette(r,ctx,640,360,.3),v);assert.equal(created,4);cases++;
const alpha=new Uint8Array(743*743);for(let i=0;i<alpha.length;i++)alpha[i]=i%743<330?255:0;
const mask=new Sprites().registerMask(alpha,743,743),d=new Uint8ClampedArray(alpha.length*4);
function bench(fn){const a=[];for(let i=0;i<15;i++){const t=performance.now();fn();a.push(performance.now()-t);}a.sort((x,y)=>x-y);return a[7];}
const timings={stage1DecodeMs:bench(()=>{d.fill(0);oldFill(mask,d);}),stage2DecodeMs:bench(()=>{d.fill(0);Sprites.fillMask(mask,d);})};
console.log(JSON.stringify({pass:true,cases,hostOnly:true,timings,rleBytes:mask.data.byteLength,cameraEvents:events.length,gradientCreations:created},null,2));
