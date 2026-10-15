/* SPDX-License-Identifier: Apache-2.0 */
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(process.argv[2],'utf8');
for(const dark of [false,true])for(const nativeStartup of [false,true]){
 const widgets=new Map(),timers=[],deferred=[],commands=[],saved={};let next=0,touch,swipe;
 const add=(kind,data)=>{const id=++next;widgets.set(id,{kind,...data});return id;};
 const ui={primary:dark?0xffffff:0x252d46,getSize:()=>({width:1024,height:536}),background(){},
  nowMs:nativeStartup?()=>0:undefined,
  grokSprite:(x,y,w,h,d,palette)=>add('sprite',{x,y,w,h,d,palette}),grokControl:(id,op,value)=>{assert.equal(widgets.get(id).kind,'sprite');commands.push({id,op,value});},
  text:(text,x,y)=>add('text',{text,x,y}),panel:(x,y,w,h)=>add('panel',{x,y,w,h}),button:(text,x,y,w,h,action)=>add('button',{text,action}),
  setText:(id,text)=>{assert(widgets.has(id));widgets.get(id).text=text;},setSize(){},setStyle(){},
  remove:id=>assert(widgets.delete(id)),onTouch:fn=>touch=fn,onSwipe:fn=>swipe=fn};
 const context=vm.createContext({ui,system:{storage:{get:key=>saved[key],set:(key,value)=>saved[key]=value}},
  setTimeout:fn=>deferred.push(fn),setInterval:fn=>{timers.push(fn);return timers.length;},clearInterval(){}});
 vm.runInContext(source,context,{timeout:1000});
 if(nativeStartup){assert.equal(context.VelaMood,undefined);assert.equal(deferred.length,1);deferred.shift()();}
 const mood=context.VelaMood,baseCount=widgets.size,tick=()=>timers.forEach(fn=>fn());
 assert.equal(mood.states.length,32);assert.equal([...widgets.values()].filter(w=>w.kind==='sprite').length,1);
 assert.equal([...widgets.values()].find(w=>w.kind==='sprite').palette,dark?1:0);
 for(const id of mood.states){mood.setEmotion(id);assert.equal(mood.engine.emotionId,id);assert.equal(saved.emotion,id);}
 assert.equal(widgets.size,baseCount,'state changes reuse the sprite and controls');
 mood.setEmotion('02');swipe('left');assert.equal(mood.engine.emotionId,'03');
 assert(mood.handleAIMessage({emotionId:'30',tips:'正在思考'}));assert.equal(mood.engine.emotionId,'30');
 assert([...widgets.values()].some(w=>w.text==='正在思考'));
 assert(!mood.handleAIMessage(null));assert.equal(mood.engine.emotionId,'02');
 const before=commands.length;touch('down',.05,.95);touch('up',.05,.95);assert(commands.slice(before).every(c=>c.op===2&&c.value===0),'outside touches can clear gaze but never spin or select');
 touch('down',.58,.43);touch('up',.58,.43);assert(commands.some(c=>c.op===2&&c.value===42),'tap sends the spin action');
 const choose=text=>{const row=[...widgets.values()].find(w=>w.kind==='button'&&w.text===text);assert(row,text);row.action();};
 mood.showPicker(0);assert(widgets.size>baseCount);choose('工作状态');
 const option=[...widgets.values()].find(w=>w.kind==='button'&&w.text.startsWith('39  '));assert(option);option.action();
 assert.equal(mood.engine.emotionId,'39');assert.equal(widgets.size,baseCount,'closing the picker releases temporary widgets');
 mood.startTour(300);assert(mood.isTourRunning());for(let i=0;i<8;i++)tick();mood.stopTour();assert(!mood.isTourRunning());
 mood.setActive(false);const paused=commands.length;for(let i=0;i<10;i++)tick();assert.equal(commands.length,paused);
 mood.setActive(true);tick();assert(commands.length>paused);assert.equal(widgets.size,baseCount);
}
console.log('GROK_SD_CONTROLLER_OK: 4 configurations, 32 states, native sprite reuse, gaze/spin, picker, tour and pause');
