const {test}=require('node:test');const assert=require('node:assert/strict');const fs=require('node:fs');const vm=require('node:vm');const path=require('node:path');
test('SD controller preserves IDs, actions, picker pause and AI bridge in both themes',()=>{
  for(const primary of [0x242632,0xf0eff5]){
    let handle=1,now=0,interval,touch,swipe;const calls=[],buttons=[],stored=new Map();
    const noop=()=>{};const ui={primary,getSize:()=>({width:1024,height:536}),nowMs:()=>now,background:noop,text:()=>handle++,setSize:noop,setStyle:noop,setText:noop,remove:noop,panel:()=>handle++,button:(text,x,y,w,h,fn)=>{buttons.push({text,fn});return handle++;},onTouch:fn=>touch=fn,onSwipe:fn=>swipe=fn,grokSprite:(...args)=>{assert.equal(args[5],primary>0x888888?1:0);return handle++;},grokControl:(...args)=>{calls.push(args);return args[2];}};
    const context={ui,system:{storage:{get:k=>stored.get(k),set:(k,v)=>stored.set(k,v)}},setTimeout:fn=>fn(),setInterval:fn=>{interval=fn;}};
    vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(__dirname,'../app.js'),'utf8'),context);
    const mood=context.VelaMood;assert.equal(mood.states.length,32);
    for(const id of mood.states){mood.setEmotion(id);assert.equal(mood.engine.emotionId,id);assert.equal(calls.findLast(v=>v[1]===0)[2],Number(id));}
    mood.handleAIMessage({emotionId:'30',tips:'思考'});assert.equal(mood.engine.emotionId,'30');mood.setEmotion('99');assert.equal(mood.engine.emotionId,'02');
    const click=text=>buttons.find(v=>v.text===text).fn();click('表情');assert.deepEqual(calls.at(-1).slice(1),[1,0]);click('关闭');assert.deepEqual(calls.at(-1).slice(1),[1,1]);
    touch('down',.5,.43);touch('up',.5,.43);assert(calls.some(v=>v[1]===2&&v[2]===42));assert.equal(calls.at(-1)[2],42,'touch release must retain spin');
    touch('down',.55,.4);touch('move',.65,.6);assert(calls.some(v=>v[1]===2&&v[2]>=43&&v[2]<=51));touch('up',.65,.6);
    swipe('left');assert.equal(mood.engine.emotionId,'03');mood.startTour(300);for(let i=0;i<15;i++){now+=50;interval();}assert(mood.isTourRunning());mood.stopTour();assert(!mood.isTourRunning());
    mood.setActive(false);const before=calls.length;interval();assert.equal(calls.length,before);
  }
});
