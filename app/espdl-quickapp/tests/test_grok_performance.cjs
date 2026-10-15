// SPDX-License-Identifier: Apache-2.0
// Deterministic before/after scenes and bridge-call counts, not device FPS.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const [beforePath,afterPath,reportPath]=process.argv.slice(2);
function scene(source,scenario,packedAPI) {
  let seed=0x12345678,id=0,peak=0,timer,touch;
  const widgets=new Map(),order=[],calls={},buttons={};
  const math=Object.create(Math);math.random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
  const count=name=>{calls[name]=(calls[name]||0)+1;};
  function add(kind,props){count(kind);let handle=1;while(widgets.has(handle))handle++;id=Math.max(id,handle);widgets.set(handle,{kind,...props});order.push(handle);peak=Math.max(peak,widgets.size);return handle;}
  function paint(points,color,opacity,width,gradient){return {points,color,opacity,width,gradient};}
  const ui={primary:0x252d46,getSize:()=>({width:1022,height:536}),background(){},
    text:(text,x,y)=>add('text',{text,x,y}),panel:(x,y,w,h)=>add('panel',{x,y,w,h}),
    button:(text,x,y,w,h,fn)=>{buttons[text]=fn;return add('button',{text,x,y,w,h});},
    polygon:(...p)=>add('polygon',paint(...p)),polygonSet:(h,...p)=>{count('polygonSet');Object.assign(widgets.get(h),paint(...p));},
    setHidden:(h,hidden)=>{count('setHidden');widgets.get(h).hidden=hidden;},
    front:h=>{count('front');assert(widgets.has(h));order.splice(order.indexOf(h),1);order.push(h);},
    setText:(h,text)=>{count('setText');widgets.get(h).text=text;},
    setPos:(h,x,y)=>{count('setPos');Object.assign(widgets.get(h),{x,y});},
    setSize(){},setStyle(){count('setStyle');},setColor(){count('setColor');},setOpacity(){count('setOpacity');},
    onTouch:fn=>touch=fn,onSwipe(){},remove:h=>{count('remove');assert(widgets.delete(h));order.splice(order.indexOf(h),1);}};
  if(packedAPI){const decode=b=>{const flat=new Int32Array(b),points=[];for(let i=0;i<flat.length;i+=2)points.push([flat[i],flat[i+1]]);return points;};ui.polygonBuffer=(b,...p)=>ui.polygon(decode(b),...p);ui.polygonSetBuffer=(h,b,...p)=>ui.polygonSet(h,decode(b),...p);}
  const ctx=vm.createContext({Math:math,ui,system:{storage:{get(){},set(){}}},console:{log(){},warn(){}},
    setInterval:fn=>{timer=fn;return 1;},clearInterval(){},setTimeout:fn=>fn()});
  vm.runInContext(source,ctx);const mood=ctx.VelaMood;
  mood.setEmotion(scenario==='thinking'?'30':scenario==='confetti'?'33':'02');
  if(scenario==='spin')mood.engine.spin(1);
  for(const name of Object.keys(calls))calls[name]=0;
  const hashes=[];
  const tick=()=>{timer();hashes.push(crypto.createHash('sha256').update(JSON.stringify(order.map(h=>widgets.get(h)).filter(w=>w.kind==='polygon'&&!w.hidden).map(w=>paint(w.points,w.color,w.opacity,w.width,w.gradient)))).digest('hex'));};
  if(scenario==='all-states')for(const state of mood.states){mood.setEmotion(state);for(let i=0;i<40;i++)tick();}
  else for(let i=0;i<240;i++){if(scenario==='gaze'){const x=.5+Math.sin(i/15)*.1;touch(i?'move':'down',x,.4);}tick();}
  return {calls,frames:hashes.length,peakWidgets:peak,hashes};
}
const before=fs.readFileSync(beforePath,'utf8'),after=fs.readFileSync(afterPath,'utf8');
const results=[];
for(const name of ['idle','gaze','thinking','spin','confetti','all-states']) {
  const a=scene(before,name),b=scene(after,name,true),fallback=scene(after,name,false);
  assert.deepEqual(b.hashes,a.hashes,`${name}: visible polygon geometry, colours, gradients and painter order must match`);
  assert.deepEqual(fallback.hashes,a.hashes,`${name}: legacy array points fallback must preserve geometry and painter order`);
  assert(b.peakWidgets<=a.peakWidgets,`${name}: cache must not add native widgets`);
  const bridgeCalls=r=>Object.values(r.calls).reduce((sum,n)=>sum+n,0);
  results.push({scenario:name,frames:a.frames,geometryIdentical:true,peakWidgets:b.peakWidgets,
    before:{calls:a.calls,total:bridgeCalls(a)},after:{calls:b.calls,total:bridgeCalls(b)},
    bridgeCallReductionPercent:100*(1-bridgeCalls(b)/bridgeCalls(a))});
}
// A completely unchanged frame must not cross the native UI bridge; edits to
// gradient stops and same-valued attributes must invalidate only what changes.
function staticScene() {
  const calls=[];let id=0;
  const ui={polygon:()=>++id,polygonSet:()=>calls.push('paint'),setHidden:()=>calls.push('hidden'),front:()=>calls.push('front')};
  const ctx=vm.createContext({ui});vm.runInContext(after.slice(0,after.indexOf('var document=')),ctx);
  const port=ctx.GrokPort,root=port.mount(1024,600),defs=new port.Node('defs'),grad=new port.Node('radialGradient');
  grad.setAttribute('id','test');for(const [key,value] of Object.entries({cx:'50%',cy:'50%',r:'50%'}))grad.setAttribute(key,value);
  for(const [offset,color] of [['0','#fff'],['1','#000']]){const stop=new port.Node('stop');stop.setAttribute('offset',offset);stop.setAttribute('stop-color',color);grad.appendChild(stop);}
  defs.appendChild(grad);root.appendChild(defs);
  const polygon=new port.Node('path');polygon.setAttribute('d','M 0 0 L 100 0 L 50 100 Z');polygon.setAttribute('fill','url(#test)');root.appendChild(polygon);
  port.sync();calls.length=0;polygon.setAttribute('d',polygon.attrs.d);port.sync();assert.deepEqual(calls,[]);
  grad.children[0].setAttribute('stop-color','#123456');port.sync();assert.deepEqual(calls,['paint']);
  calls.length=0;let workClock=0;ui.nowMs=()=>workClock+=16;port.sync();assert(port.isRendering());
  polygon.style.display='none';polygon.setAttribute('d',polygon.attrs.d);
  assert(!port.isRendering(),'an equal setter still cancels a pending traversal after a direct display edit');
  while(!port.sync()){}assert.deepEqual(calls,['hidden']);
}
staticScene();
function recycledScene(){
  const live=new Set(),order=[];let id=0;
  const ui={polygon(){let h=1;while(live.has(h))h++;live.add(h);order.push(h);return h;},polygonSet(){},setHidden(){},front(h){assert(live.has(h));order.splice(order.indexOf(h),1);order.push(h);},remove(h){assert(live.delete(h));order.splice(order.indexOf(h),1);}};
  const ctx=vm.createContext({ui});vm.runInContext(after.slice(0,after.indexOf('var document=')),ctx);
  const port=ctx.GrokPort,root=port.mount(1024,600),back=new port.Node('g');root.appendChild(back);
  function path(){const node=new port.Node('path');node.setAttribute('d','M 0 0 L 100 0 L 50 100 Z');node.setAttribute('fill','#fff');return node;}
  const rear=path();back.appendChild(rear);root.appendChild(path());port.sync();assert.deepEqual(order,[1,2]);
  rear.remove();back.appendChild(path());port.sync();assert.deepEqual(order,[1,2],'reusing a rear ribbon handle must restore painter order even when the numeric sequence is unchanged');
  const circle=new port.Node('circle');circle.setAttribute('r','5');circle.setAttribute('fill','#fff');root.appendChild(circle);port.sync();const geometry=circle.cachedContours;
  circle.setAttribute('opacity','0.5');circle.setAttribute('transform','translate(30,40)');port.sync();assert.strictEqual(circle.cachedContours,geometry,'opacity and movement must not rebuild the local circle');
}
recycledScene();
const report={kind:'deterministic offline bridge workload; not board FPS',sourceSha256:{before:crypto.createHash('sha256').update(before).digest('hex'),after:crypto.createHash('sha256').update(after).digest('hex')},results};
if(reportPath)fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report,null,2));
