// SPDX-License-Identifier: Apache-2.0
// The same pinned engine, presented inside the selected A card viewport.
var GrokEmbedded=(function(){
  var ball,current='02',staticDirty=true,enabled=true;
  // Share the pinned engine's complete registry; do not maintain a reduced list.
  var emotions=GrokBall.EMOTIONS.map(function(def){
    return {id:def.id,name:def.name,group:def.group};
  });
  function normalize(id){
    if(typeof id==='number'){
      if(!isFinite(id)||Math.floor(id)!==id)return '02';
    }else if(typeof id!=='string'||!/^\d{1,2}$/.test(id))return '02';
    var value=Number(id),next=(value<10?'0':'')+value;
    return emotions.some(function(def){return def.id===next;})?next:'02';
  }
  function init(width,height,diameter,centerX,centerY,bodyCell,detailCell,homeStyle){
    var mount=GrokPort.mount(width,height,diameter,centerX,centerY,bodyCell,detailCell,homeStyle);
    ball=GrokBall.create(mount,{emotion:'02',autostart:false,lite:false});
    ball.setActive(true);
    var apply=ball.ball.applyPose;
    var displayPose={body:{},left:{},right:{}},rings=[null,null],centers=[0,0];
    var homePixel=homeStyle===3||homeStyle===4,black=homeStyle===2||homeStyle===4;
    if(homePixel||black)ball.ball.applyPose=function(pose){
      // Presentation must not mutate the engine's stored transition pose.
      Object.assign(displayPose.body,pose.body);
      Object.assign(displayPose.left,pose.left);
      Object.assign(displayPose.right,pose.right);
      if(homePixel){
        var eyes=[pose.left,pose.right];
        for(var i=0;i<2;i++)if(rings[i]!==eyes[i].ring){
          rings[i]=eyes[i].ring;var sum=0;
          for(var j=0;j<rings[i].length;j++)sum+=rings[i][j][0];
          centers[i]=sum/rings[i].length;
        }
        // Eye contours contain baked-in face positions. Re-anchor their
        // shared baseline to the right while preserving individual shapes,
        // eye angles, gaze, breathing, roll, spin yaw and ribbon animation.
        var face=EB_RINGS.SHAPES.blob.face;
        var shift=38-face.x-((centers[0]+centers[1])/2-EB_RINGS.HEAD_C)*face.sx;
        displayPose.left.x+=shift;displayPose.right.x+=shift;
      }
      if(black){
        displayPose.body.color='#1a1a1a';
        displayPose.left.color=displayPose.right.color='#f6f5f0';
      }
      return apply.call(this,displayPose);
    };
    ball._tick(GrokPort.clock);GrokPort.sync();
  }
  function emotion(id){
    var next=normalize(id);
    if(next!==current||ball.emotionId!==next){
      ball.handleAIMessage({emotionId:next});current=ball.emotionId;staticDirty=true;
    }
    return ball.emotionId;
  }
  function step(animate){
    if(enabled!==animate){enabled=animate;ball.setActive(animate);staticDirty=true;}
    if(GrokPort.isRendering()){GrokPort.sync();return;}
    if(animate){GrokPort.clock+=50;ball._tick(GrokPort.clock);GrokPort.sync();}
    else if(staticDirty){ball.renderStatic();GrokPort.sync();staticDirty=false;}
  }
  return {init:init,emotion:emotion,step:step,emotions:emotions};
})();
