// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(process.argv[2],'utf8');
for(const [dark,nativeStartup,packedAPI] of [[false,false,false],[true,false,false],[false,true,false],[true,true,false],[false,false,true],[true,false,true],[false,true,true],[true,true,true]]) {
  const startupTasks=[];
  const widgets=new Map(),timers=new Map(),storage={},buttons=[],order=[];
  let id=0,timerId=0,touch,swipe,peak=0;
  function add(kind,props){let handle=1;while(widgets.has(handle))handle++;id=Math.max(id,handle);widgets.set(handle,{kind,...props});order.push(handle);peak=Math.max(peak,widgets.size);return handle;}
  function polygon(points,color,opacity,width,gradient){assert(points.length>=3&&points.length<=128);for(const p of points)assert(p.every(Number.isFinite));if(gradient){assert(gradient.every(Number.isFinite));assert.equal(gradient.length,7+gradient[1]*2);}return {points,color,opacity,width,gradient};}
  const ui={primary:dark?0xf4f2ff:0x252d46,getSize:()=>({width:1022,height:536}),background(){},
    nowMs:nativeStartup?()=>0:undefined,
    text:(text,x,y)=>add('text',{text,x,y}),panel:(x,y,w,h)=>add('panel',{x,y,w,h}),button:(text,x,y,w,h,fn)=>{buttons.push({text,fn});return add('button',{text,x,y,w,h,fn});},
    polygon:(...p)=>add('polygon',polygon(...p)),polygonSet:(h,...p)=>Object.assign(widgets.get(h),polygon(...p)),
    setHidden:(h,hidden)=>{assert(widgets.has(h));widgets.get(h).hidden=hidden;},front:h=>{assert(widgets.has(h));order.splice(order.indexOf(h),1);order.push(h);},
    setSize(){},setStyle(){},setColor(){},setOpacity(){},setPos(){},setText:(h,text)=>{widgets.get(h).text=text;},
    onTouch:fn=>{touch=fn;},onSwipe:fn=>{swipe=fn;},remove:h=>{assert(widgets.delete(h));order.splice(order.indexOf(h),1);}
  };
  if(packedAPI){
    const decode=b=>{const flat=new Int32Array(b),points=[];for(let i=0;i<flat.length;i+=2)points.push([flat[i],flat[i+1]]);return points;};
    ui.polygonBuffer=(b,...p)=>ui.polygon(decode(b),...p);
    ui.polygonSetBuffer=(h,b,...p)=>ui.polygonSet(h,decode(b),...p);
  }
  const ctx=vm.createContext({ui,system:{storage:{get:k=>storage[k],set:(k,v)=>{storage[k]=v;}}},
    console:{log(){},warn(){}},setTimeout:fn=>{startupTasks.push(fn);return ++timerId;},setInterval:(fn)=>{timers.set(++timerId,fn);return timerId;},clearInterval:h=>timers.delete(h)});
  vm.runInContext(source.replace('var ball=GrokBall.create(mount,','globalThis.__testMount=mount;var ball=GrokBall.create(mount,'),ctx,{timeout:1000});
  if(nativeStartup){assert.equal(ctx.VelaMood,undefined,'SDK loading returns before native UI construction');assert.equal(startupTasks.length,1);startupTasks.shift()();for(const fn of timers.values())fn();}
  const mood=ctx.VelaMood,ball=mood.engine;
  assert.equal(ctx.GrokBall.EMOTIONS.length,32);assert.equal(ctx.EB_RINGS.EXPRESSIONS.length,25);
  assert.equal(ball.emotionId,'02');
  assert.equal(ball._active,true,'native timer must enable the upstream animation gates');
  const tick=()=>{for(const fn of [...timers.values()])fn();};
  let spins=0;const realSpin=ball.spin;ball.spin=function(){spins++;return realSpin.apply(this,arguments);};
  touch('down',0.05,0.95);touch('up',0.05,0.95);assert.equal(spins,0,'controls/outside taps must not spin the ball');
  touch('down',0.58,0.4);for(let i=0;i<8;i++)tick();
  assert(ball._gaze.x>0.5,'gaze follows the finger');touch('up',0.58,0.4);
  for(let i=0;i<40;i++)tick();assert(Math.abs(ball._gaze.x)<0.1,'release clears gaze');
  swipe('left');assert.equal(ball.emotionId,'03');assert.equal(storage.emotion,'03');
  assert(mood.handleAIMessage(JSON.stringify({emotionId:'30',tips:'正在思考'})));assert.equal(ball.emotionId,'30');
  assert([...widgets.values()].some(w=>w.text==='正在思考'),'AI tips use the event text rather than object coercion');
  assert(!mood.handleAIMessage('{bad json'));assert.equal(ball.emotionId,'02');
  assert([...widgets.values()].some(w=>w.text==='触摸跟随 · 点击自旋 · 左右滑动切换'),'old AI tips reset when the state changes');
  mood.setEmotion('03');const blinkDeadline=ball._blinkNext,pool=new Set();
  for(let i=0;i<140;i++){tick();pool.add(ball._exprIdx);}
  assert(ball._blinkNext>blinkDeadline,'automatic blink scheduling stays enabled');
  assert(pool.size>1,'the upstream eye-ring pool rotates automatically');
  ball.spin(1);let ribbon=false,frontRibbon=false;
  const scene=ctx.__testMount.children[0],back=scene.children[1],head=scene.children[2].children[0],front=scene.children[3];
  assert.equal(widgets.get(head.handles[0]).gradient[0],1,'upstream body has a radial gradient');
  assert.equal(widgets.get(scene.children[2].children[1].handles[0]).color,0x1a1a1a,'upstream eyes stay black');
  function checkLayers(){
    const headIndex=order.indexOf(head.handles[0]);
    for(const n of back.children)for(const h of n.handles)if(!widgets.get(h).hidden)assert(order.indexOf(h)<headIndex,'rear ribbon stays behind body');
    for(const n of front.children)if(n.tag==='path')for(const h of n.handles)if(!widgets.get(h).hidden){
      assert(order.indexOf(h)>headIndex,'front ribbon stays above body');
      frontRibbon=true;
    }
  }
  for(let i=0;i<100;i++) {tick();checkLayers();
    ribbon=ribbon||[...widgets.values()].filter(w=>w.kind==='polygon').length>3;}
  assert(ribbon,'spin emits real ribbon contours');
  assert(frontRibbon,'spin has a visible front ribbon');
  mood.setEmotion('30');for(let i=0;i<160;i++){tick();checkLayers();}
  mood.setEmotion('33');ctx.GrokPort.sync();
  assert(front.children.filter(n=>n.tag!=='text').length>=20,'task completion must emit confetti when active');
  const beforePicker=widgets.size;
  for(const group of [0,1,2,0,2]) {
    mood.showPicker(group);assert([...widgets.values()].some(w=>w.text==='选择表情'));
    const before=JSON.stringify([...widgets.values()].filter(w=>w.kind==='polygon'));tick();
    assert.equal(JSON.stringify([...widgets.values()].filter(w=>w.kind==='polygon')),before,'animation pauses behind the picker');
  }
  const modalState=ball.emotionId,modalSpins=spins;
  [...widgets.values()].find(w=>w.kind==='button'&&w.text==='思考').fn();
  touch('down',0.5,0.43);touch('up',0.5,0.43);swipe('left');
  assert.equal(ball.emotionId,modalState,'background controls/swipes stay blocked by the picker');assert.equal(spins,modalSpins);
  const pick=[...widgets.values()].find(w=>w.kind==='button'&&w.text==='40  检索资料');pick.fn();
  assert.equal(ball.emotionId,'40');assert.equal(storage.emotion,'40');assert.equal(widgets.size,beforePicker,'picker releases all of its widgets');
  assert(![...widgets.values()].some(w=>w.text==='选择表情'));
  for(const emotion of mood.states) {mood.setEmotion(emotion);mood.renderStatic();assert.equal(ball.emotionId,emotion);for(let i=0;i<22;i++)tick();}
  assert(peak<160,'temporary FX widgets remain bounded');
  mood.startTour(1800);assert(mood.isTourRunning());mood.stopTour();assert(!mood.isTourRunning());
  assert.equal(timers.size,1,'only one animation timer remains');
  mood.setEmotion('10');mood.renderStatic();
  mood.startTour(1800);mood.setEmotion('10');mood.renderStatic();
  const paused=JSON.stringify([...widgets.values()].filter(w=>w.kind==='polygon'));
  mood.setActive(false);assert(!mood.isTourRunning(),'pausing also stops the tour');for(let i=0;i<8;i++)tick();
  assert.equal(JSON.stringify([...widgets.values()].filter(w=>w.kind==='polygon')),paused);
  // Simulate a slow device. One frame is traversed in bounded slices; the
  // engine must not advance and mutate the scene halfway through that frame.
  let workClock=0;ui.nowMs=()=>workClock+=5;
  mood.setActive(true);mood.setEmotion('30');mood.renderStatic();
  assert(ctx.GrokPort.isRendering(),'slow-device render yields before finishing');
  const heldClock=ctx.GrokPort.clock;tick();assert.equal(ctx.GrokPort.clock,heldClock);
  for(let i=0;i<500&&ctx.GrokPort.isRendering();i++)tick();
  assert(!ctx.GrokPort.isRendering(),'deferred frame finishes without starvation');
  assert(widgets.size<160,'sliced rendering keeps the existing widget bound');
  mood.setActive(false);
  console.log('GROK_APP_OK',dark?'dark':'light',nativeStartup?'deferred native startup':'offline startup',packedAPI?'packed points':'array points','recycled handles; 32 states; gaze, swipe, AI fallback, spin ribbons, tour, pause; peak widgets',peak);
}
