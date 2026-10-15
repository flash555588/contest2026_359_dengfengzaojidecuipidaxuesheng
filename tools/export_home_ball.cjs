// Export the pinned engine's vector scene for an isolated native home review.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const variant=process.argv[4]?JSON.parse(fs.readFileSync(process.argv[4],'utf8')):{};
const diameter=variant.diameter||100,centerX=variant.centerX||82,viewportHeight=89;
const occlusion=variant.occlusion===undefined?0.25:variant.occlusion;
const centerY=viewportHeight-diameter*(0.5-occlusion);
assert(diameter>0&&occlusion>=0&&occlusion<0.5);
let source=fs.readFileSync(process.argv[2],'utf8');
const boundary=source.lastIndexOf('function grokStartUi()');assert(boundary>0);source=source.slice(0,boundary);
source=source.replace('scale=Math.min(height*0.56/228,width/420,diameter/228)','scale=diameter/228');
// Align the ball with the card's left content inset, plus a small allowance
// for the orbit ribbon: the larger body starts 32 px inside the left edge.
source=source.replace('originX=width/2-114.2705*scale',`originX=${centerX}-114.2705*scale`);
// Derive vertical position from the requested occlusion and fixed card viewport.
source=source.replace('originY=height*0.43-114.2705*scale',`originY=${centerY}-114.2705*scale`);
const result=[];
const requestedScenes=variant.scenes||[{emotion:'02',key:'idle'},{emotion:'30',key:'thinking'}];
assert(requestedScenes.length>0&&requestedScenes.length<=32);
assert(new Set(requestedScenes.map(s=>s.key)).size===requestedScenes.length);
for(const scene of requestedScenes){
  const emotion=scene.emotion;assert(/^\d{2}$/.test(emotion)&&/^[a-z][a-z-]*$/.test(scene.key));
  const objects=new Map(),order=[];let next=0;
  const create=(kind,props)=>{const id=++next;objects.set(id,{kind,...props});order.push(id);return id;};
  const decode=b=>{const v=new Int32Array(b),p=[];for(let i=0;i<v.length;i+=2)p.push([v[i],v[i+1]]);return p;};
  const ui={polygon:(points,color,opacity,width,gradient,cell)=>create('polygon',{points,color,opacity,width,gradient,cell}),
    polygonSet:(id,points,color,opacity,width,gradient,cell)=>Object.assign(objects.get(id),{points,color,opacity,width,gradient,cell}),
    polygonBuffer:(b,...args)=>ui.polygon(decode(b),...args),polygonSetBuffer:(id,b,...args)=>ui.polygonSet(id,decode(b),...args),
    setHidden:(id,hidden)=>{objects.get(id).hidden=hidden;},front:id=>{order.splice(order.indexOf(id),1);order.push(id);},
    remove:id=>{objects.delete(id);order.splice(order.indexOf(id),1);},text:(text,x,y,size,color)=>create('text',{text,x,y,size,color,opacity:255}),
    setText:(id,text)=>Object.assign(objects.get(id),{text}),setPos:(id,x,y)=>Object.assign(objects.get(id),{x,y}),
    setColor:(id,color)=>Object.assign(objects.get(id),{color}),setOpacity:(id,opacity)=>Object.assign(objects.get(id),{opacity}),
    setStyle:(id,style)=>Object.assign(objects.get(id),{size:style.fontSize}),setSize(){}};
  const ctx=vm.createContext({ui,console:{log(){}}});
  vm.runInContext("var __seed=0x12345678;Math.random=function(){__seed=(Math.imul(__seed,1664525)+1013904223)>>>0;return __seed/4294967296;};\n"+source+`\nvar mount=GrokPort.mount(285,${viewportHeight},${diameter});var ball=GrokBall.create(mount,{emotion:'${emotion}',autostart:false,lite:false});ball.setActive(true);
    // Home-card presentation only: keep the face upright and forward.
    // Original eye contours, blinking, breathing and orbit animation remain.
    var homeApplyPose=ball.ball.applyPose;
    ball.ball.applyPose=function(pose){
      pose.body.rotate=0;pose.body.yaw=0;
      var face=EB_RINGS.SHAPES.blob.face,hc=EB_RINGS.HEAD_C;
      [pose.left,pose.right].forEach(function(eye,index){
        var cx=0,cy=0;eye.ring.forEach(function(p){cx+=p[0];cy+=p[1];});cx/=eye.ring.length;cy/=eye.ring.length;
        eye.rotate=0;eye.lookX=0;eye.lookY=0;
        // Wide circular eye families need more separation at this small size.
        var eyeOffset=${variant.separateWideEyes&&['13','34'].includes(emotion)?38:26};
        eye.x=(index===0?-eyeOffset:eyeOffset)-face.x-(cx-hc)*face.sx;
        eye.y=-23-face.y-(cy-hc)*face.sy;
      });
      return homeApplyPose.call(this,pose);
    };`,ctx);
  const tick=()=>vm.runInContext('GrokPort.clock+=50;ball._tick(GrokPort.clock);GrokPort.sync();',ctx);
  const definition=vm.runInContext(`GrokBall.EMOTIONS.filter(function(e){return e.id==='${emotion}';})[0]`,ctx);
  assert(definition&&vm.runInContext('ball.emotionId',ctx)===emotion);
  // The inactive constructor renders a static pose. Enter an event state
  // again after activation so its one-shot effects run through the engine.
  if(variant.enterEffects)vm.runInContext(`if(ball._def.base.body.ribbons>0||ball._def.base.body.confetti>0)ball.setEmotion('${emotion}');`,ctx);
  for(let i=0;i<(scene.warmupTicks===undefined?24:scene.warmupTicks);i++)tick();
  const frames=[];
  const textFrames=[],observed=[];
  for(let i=0;i<30;i++){
    tick();tick();
    const parts=order.map(id=>objects.get(id)).filter(p=>p.kind==='polygon'&&!p.hidden);
    assert(parts.length>2&&parts.length<64);
    for(const p of parts){assert(p.points.length>=3&&p.points.length<=128);assert(p.cell===2||p.cell===4);}
    // Flat body only; eyes, ribbon gradients and their painter order remain.
    frames.push(parts.map(p=>p.gradient&&p.gradient[0]===1?{...p,color:0xf2efe8,gradient:null}:p));
    textFrames.push(order.map(id=>objects.get(id)).filter(p=>p.kind==='text'&&!p.hidden&&p.opacity>0).map(p=>({...p})));
    observed.push(vm.runInContext('ball.emotionId',ctx));
  }
  result.push({emotion,key:scene.key,name:definition.name,group:definition.group,frames,textFrames,observed});
}
fs.writeFileSync(process.argv[3],JSON.stringify({renderer:'pinned Grok engine and vector adapter, native home-card presentation',variant:variant.id||'current',diameter,viewport:[285,viewportHeight],center:[centerX,centerY],nominalHeadroom:viewportHeight-diameter*(1-occlusion),nominalHiddenBottom:diameter*occlusion,bodyFill:'#F2EFE8',bodyFillStyle:'solid',faceAngle:'front-upright',bodyRollDegrees:0,scenes:result}));
console.log('HOME_BALL_SCENES_OK',diameter,'px, left-aligned solid body, hidden ratio',occlusion,',',result.length*30,'frames');
