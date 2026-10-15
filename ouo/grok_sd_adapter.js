// SPDX-License-Identifier: Apache-2.0
// UI/controller compatibility for SD playback. Geometry is rendered offline.
var GrokPort={clock:0,mount:function(w,h){return {width:w,height:h};},sync:function(){},isRendering:function(){return false;}};
GrokBall.create=function(mount,options){
  var view=mount,diameter=Math.round(Math.min(view.height*0.56,view.width*228/420));
  var side=Math.ceil(diameter*128/96),handle=ui.grokSprite(Math.round((view.width-side)/2),Math.round(view.height*0.43-side/2),side,side,diameter,ui.primary>0x888888?1:0);
  var events={},ball={emotionId:options.emotion||'02',_touring:false};
  function emit(key,value){(events[key]||[]).forEach(function(fn){fn(value);});}
  function select(id){id=String(id);if(/^\d{1,2}$/.test(id)){id=('0'+Number(id)).slice(-2);}if(!GrokBall.EMOTIONS.some(function(e){return e.id===id;}))id='02';ball.emotionId=id;ui.grokControl(handle,0,Number(id));emit('change',{id:id});}
  var tourIndex=0,tourIds=[],tourAt=0,tourPeriod=1800,spinUntil=0;
  ball.on=function(key,fn){(events[key]||(events[key]=[])).push(fn);};
  ball.setEmotion=function(id){select(id);};
  ball.handleAIMessage=function(message){if(!message||message.emotionId===undefined)return false;select(message.emotionId);if(message.tips)emit('tips',{text:message.tips});return true;};
  ball.setActive=function(active){ui.grokControl(handle,1,active?1:0);};
  ball.renderStatic=function(){ui.grokControl(handle,3,1);};
  ball._tick=function(now){ui.grokControl(handle,3,0);if(ball._touring&&now>=tourAt){select(tourIds[tourIndex++%tourIds.length]);tourAt=now+tourPeriod;}};
  ball.startTour=function(ids,period){tourIds=ids.slice();tourIndex=0;tourPeriod=Math.max(300,period||1800);tourAt=GrokPort.clock;ball._touring=true;};
  ball.stopTour=function(){ball._touring=false;};
  ball.spin=function(){spinUntil=GrokPort.clock+3000;ui.grokControl(handle,2,42);};
  ball.setGaze=function(x,y){spinUntil=0;var col=x<-.15?0:x>.15?2:1,row=y<-.15?0:y>.15?2:1;ui.grokControl(handle,2,43+row*3+col);};
  ball.clearGaze=function(){if(GrokPort.clock>=spinUntil)ui.grokControl(handle,2,0);};
  select(ball.emotionId);return ball;
};
