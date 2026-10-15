// SPDX-License-Identifier: Apache-2.0
function grokStartUi() {
  var view=ui.getSize(),dark=ui.primary>0x888888,fg=dark?0xf0eff5:0x242632,muted=dark?0x9998a6:0x7a7b85;
  ui.background(dark?0x161820:0xf8f9fa);
  var saved=system.storage.get('emotion'),ids=GrokBall.EMOTIONS.map(function(e){return e.id;}),active=true;
  if(ids.indexOf(saved)<0)saved='02';
  var mount=GrokPort.mount(view.width,view.height);
  var ball=GrokBall.create(mount,{emotion:saved,autostart:false,lite:false});
  // Enable blink/pool/entry FX in the upstream engine; native timer owns ticks.
  ball.setActive(true);
  var captionY=Math.round(view.height*0.43+Math.min(view.height*0.56,view.width*228/420)/2+12);
  var stateLabel=ui.text('',view.width/2-150,captionY,24,fg);
  ui.setSize(stateLabel,300,34);ui.setStyle(stateLabel,{center:1});
  var hint='触摸跟随 · 点击自旋 · 左右滑动切换';
  var tipsLabel=ui.text(hint,view.width/2-240,captionY+38,16,muted);
  ui.setSize(tipsLabel,480,24);ui.setStyle(tipsLabel,{center:1});
  function syncLabel() {var e=GrokBall.EMOTIONS.filter(function(e){return e.id===ball.emotionId;})[0];ui.setText(stateLabel,e?e.name:'待机');}
  ball.on('change',function(e){syncLabel();ui.setText(tipsLabel,hint);});
  ball.on('tips',function(t){ui.setText(tipsLabel,String(t&&t.text!==undefined?t.text:t).slice(0,100));});
  function setEmotion(id,automatic) {
    if(picker)closePicker();
    id=String(id);if(ids.indexOf(id)<0)id='02';
    ball.setEmotion(id,{auto:!!automatic});if(!automatic)system.storage.set('emotion',id);
    ball._tick(GrokPort.clock);GrokPort.sync();syncLabel();
  }
  function handleAIMessage(message) {
    if(picker)closePicker();
    var ret=ball.handleAIMessage(message);if(!ret)setEmotion('02',false);
    else {system.storage.set('emotion',ball.emotionId);syncLabel();}
    GrokPort.sync();return ret;
  }
  var picker=null,touching=false,startX=0,startY=0;
  function closePicker() {
    if(!picker)return;var handles=picker;picker=null;handles.forEach(function(h){ui.remove(h);});ball.setActive(active);
  }
  function showPicker(group) {
    closePicker();ball.stopTour();touching=false;ball.clearGaze();ball.setActive(false);picker=[];
    var width=Math.min(768,view.width-48),height=448,x=(view.width-width)/2,y=(view.height-height)/2;
    var surface=dark?0x20232d:0xffffff,soft=dark?0x2c303c:0xf1f0f6,accent=0x7764a3;
    function keep(h){picker.push(h);return h;}
    function button(text,bx,by,w,h,fn,chosen) {
      var id=keep(ui.button(text,bx,by,w,h,fn,chosen?accent:soft));
      ui.setStyle(id,{fontSize:16,textColor:chosen?0xffffff:fg,borderWidth:0,radius:10});return id;
    }
    var shade=keep(ui.panel(0,0,view.width,view.height,0x10131b,0,168));ui.setStyle(shade,{borderWidth:0,radius:0});
    var panel=keep(ui.panel(x,y,width,height,surface));ui.setStyle(panel,{radius:20,borderWidth:1,borderColor:dark?0x383d49:0xe4e5ec});
    keep(ui.text('选择表情',x+28,y+22,24,fg));
    button('关闭',x+width-112,y+18,84,44,closePicker,false);
    ['生命周期','情绪反应','工作状态'].forEach(function(name,i){
      button(name,x+28+i*168,y+78,156,44,function(){showPicker(i);},i===group);
    });
    var start=group===0?0:group===1?8:20,end=group===0?8:group===1?20:32,w=(width-80)/3;
    GrokBall.EMOTIONS.slice(start,end).forEach(function(e,i){
      button(e.id+'  '+e.name,x+28+(i%3)*(w+12),y+140+Math.floor(i/3)*64,w,52,function(){closePicker();setEmotion(e.id,false);},ball.emotionId===e.id);
    });
    keep(ui.text('32 种表情 · 选择后立即切换 · 滑动也可逐个切换',x+28,y+height-34,14,muted));
  }
  ui.onTouch(function(state,nx,ny) {
    if(picker)return;
    if(state==='down'||state==='move') {
      if(state==='down'){
        var dx=(nx-0.5)*view.width,dy=(ny-0.43)*view.height,r=Math.min(view.height*0.56,view.width*228/420)/2+16;
        touching=dx*dx+dy*dy<=r*r;if(!touching)return;
        startX=nx;startY=ny;ball.stopTour();
      }
      if(!touching)return;
      ball.setGaze((nx-0.5)*2,(ny-0.5)*2);
    } else {
      if(state==='up'&&touching&&Math.hypot(nx-startX,ny-startY)<0.04)ball.spin(1);
      touching=false;ball.clearGaze();
    }
  });
  ui.onSwipe(function(direction) {
    if(picker)return;
    ball.stopTour();var delta=direction==='left'||direction==='down'?1:-1;
    setEmotion(ids[(ids.indexOf(ball.emotionId)+delta+ids.length)%ids.length],false);
  });
  // Compact touch controls live in the quiet lower corners, away from the ball.
  ui.button('表情',24,view.height-52,84,44,function(){if(picker)return;var index=ids.indexOf(ball.emotionId);showPicker(index<8?0:index<20?1:2);},dark?0x272934:0xe9e6e0);
  ui.button('巡演',120,view.height-52,84,44,function(){if(!picker)ball.startTour(ids,1800);},dark?0x272934:0xe9e6e0);
  ui.button('停止',216,view.height-52,84,44,function(){if(!picker)ball.stopTour();},dark?0x272934:0xe9e6e0);
  ui.button('思考',view.width-108,view.height-52,84,44,function(){if(!picker)handleAIMessage({emotionId:'30',tips:'正在思考'});},dark?0x35324b:0xe9e3f1);
  globalThis.VelaMood={
    setEmotion:setEmotion,setGaze:function(x,y){ball.setGaze(x,y);},handleAIMessage:handleAIMessage,
    startTour:function(period){ball.startTour(ids,period||1800);},stopTour:function(){ball.stopTour();},
    setActive:function(on){active=!!on;if(!active){ball.stopTour();touching=false;ball.clearGaze();}ball.setActive(active);},renderStatic:function(){ball.renderStatic();GrokPort.sync();},
    showPicker:showPicker,closePicker:closePicker,
    isTourRunning:function(){return ball._touring;},states:ids.slice(),engine:ball
  };
  setInterval(function(){if(active&&!picker){if(GrokPort.isRendering()){GrokPort.sync();return;}GrokPort.clock+=50;ball._tick(GrokPort.clock);GrokPort.sync();}},50);
  syncLabel();
  // On the device, render the first frame in the animation turn so SDK and
  // control initialization do not consume that frame's execution budget.
  if(typeof ui.nowMs!=='function')GrokPort.sync();
}
// Loading the SDK and constructing its native UI are separate event-loop
// turns. Both execute under the normal runtime budget; no application gets
// an extended deadline. The non-device fallback is used by the offline SDK.
if(typeof ui.nowMs==='function')setTimeout(grokStartUi,20);
else grokStartUi();
