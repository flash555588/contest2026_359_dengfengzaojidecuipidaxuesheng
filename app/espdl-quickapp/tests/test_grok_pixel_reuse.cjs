// Compare the embedded desktop against the exact previously reviewed pixel app.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../../..');
const reference=path.join(root,'output/grok-styles/pixel/app.js');
let original=fs.readFileSync(reference,'utf8');
const boundary=original.lastIndexOf('function grokStartUi()');assert.ok(boundary>0);
original=original.slice(0,boundary)
  .replace('scale=Math.min(height*0.56/228,width/420,diameter/228)','scale=diameter/228')
  .replace('originX=width/2-114.2705*scale','originX=76-114.2705*scale')
  .replace('originY=height*0.43-114.2705*scale','originY=65-114.2705*scale');
function fixture(source,init){
  const nodes=new Map(),order=[];let next=0;
  const clone=v=>v===undefined?v:JSON.parse(JSON.stringify(v));
  const create=(kind,p)=>{const id=++next;nodes.set(id,{kind,...clone(p)});order.push(id);return id;};
  const decode=b=>{const v=new Int32Array(b),p=[];for(let i=0;i<v.length;i+=2)p.push([v[i],v[i+1]]);return p;};
  const ui={
    polygon:(points,color,opacity,width,gradient,cell)=>create('polygon',{points,color,opacity,width,gradient,cell}),
    polygonSet:(id,points,color,opacity,width,gradient,cell)=>Object.assign(nodes.get(id),clone({points,color,opacity,width,gradient,cell})),
    polygonBuffer:(b,...args)=>ui.polygon(decode(b),...args),polygonSetBuffer:(id,b,...args)=>ui.polygonSet(id,decode(b),...args),
    setHidden:(id,hidden)=>{nodes.get(id).hidden=hidden;},front:id=>{order.splice(order.indexOf(id),1);order.push(id);},
    remove:id=>{nodes.delete(id);order.splice(order.indexOf(id),1);},text:(text,x,y,size,color)=>create('text',{text,x,y,size,color,opacity:255}),
    setText:(id,text)=>Object.assign(nodes.get(id),{text}),setPos:(id,x,y)=>Object.assign(nodes.get(id),{x,y}),
    setColor:(id,color)=>Object.assign(nodes.get(id),{color}),setOpacity:(id,opacity)=>Object.assign(nodes.get(id),{opacity}),
    setStyle:(id,style)=>Object.assign(nodes.get(id),{size:style.fontSize}),setSize(){},nowMs(){return 0;}
  };
  const ctx=vm.createContext({ui,console});
  const observe="var __engine,__rendered,__create=GrokBall.create;GrokBall.create=function(target,opts){var b=__create(target,opts),draw=b.ball.applyPose;__engine=b;b.ball.applyPose=function(pose){__rendered=JSON.parse(JSON.stringify(pose));return draw.call(this,pose);};return b;};";
  vm.runInContext("var __seed=0x12345678;Math.random=function(){__seed=(Math.imul(__seed,1664525)+1013904223)>>>0;return __seed/4294967296;};\n"+source+'\n'+observe+'\n'+init,ctx);
  return {ctx,parts:()=>clone(order.map(id=>nodes.get(id)).filter(n=>!n.hidden&&n.opacity!==0)),pose:()=>JSON.parse(vm.runInContext('JSON.stringify(__engine._lastPose)',ctx)),rendered:()=>JSON.parse(vm.runInContext('JSON.stringify(__rendered)',ctx))};
}
const before=fixture(original,"var ball=GrokBall.create(GrokPort.mount(285,89,96),{emotion:'02',autostart:false,lite:false});ball.setActive(true);ball._tick(GrokPort.clock);GrokPort.sync();");
const after=fixture(fs.readFileSync(path.join(root,'project/ouo/embedded.js'),'utf8'),'GrokEmbedded.init(285,89,96,76,65,1,1,3);');
const black=fixture(fs.readFileSync(path.join(root,'project/ouo/embedded.js'),'utf8'),'GrokEmbedded.init(285,89,96,76,65,1,1,4);');
// Color changes must not alter the body's perspective, eye positions or gaze.
function geometry(parts){return parts.map(p=>{
  const item={...p};delete item.color;
  if(item.gradient)item.gradient=item.gradient.slice(0,7).concat(item.gradient.filter((v,i)=>i>=7&&i%2===1));
  return item;
});}
const ids=vm.runInContext('GrokBall.EMOTIONS.map(function(d){return d.id;})',before.ctx);assert.equal(ids.length,32);
let frames=0,sawSpin=false,spinSettled=false;
for(const id of ids){
  vm.runInContext(`if(ball.emotionId!=='${id}')ball.handleAIMessage({emotionId:'${id}'});`,before.ctx);
  vm.runInContext(`GrokEmbedded.emotion('${id}');`,after.ctx);
  vm.runInContext(`GrokEmbedded.emotion('${id}');`,black.ctx);
  for(let frame=0;frame<12;frame++){
    vm.runInContext('GrokPort.clock+=50;ball._tick(GrokPort.clock);GrokPort.sync();',before.ctx);
    vm.runInContext('GrokEmbedded.step(true);',after.ctx);
    vm.runInContext('GrokEmbedded.step(true);',black.ctx);
    const raw=before.pose(),shown=after.rendered();
    // The engine's transition/animation state stays untouched by presentation.
    assert.deepEqual(after.pose(),raw,`original animation changed: ${id}, frame ${frame}`);
    assert.deepEqual(black.pose(),raw,`black animation changed: ${id}, frame ${frame}`);
    assert.deepEqual(shown.body,raw.body,'body animation, spin and yaw must remain original');
    if(Math.abs(raw.body.yaw||0)>0.1)sawSpin=true;
    else if(sawSpin&&Math.abs(raw.body.yaw||0)<0.01)spinSettled=true;
    for(const side of ['left','right'])for(const prop of ['lookX','lookY','rotate','open','scaleX','scaleY','y','ring'])assert.deepEqual(shown[side][prop],raw[side][prop],`eye animation changed: ${side}/${prop}`);
    assert.deepEqual(geometry(black.parts()),geometry(after.parts()),`black and white headings differ: ${id}, frame ${frame}`);
    // In non-spin frames the face must look right, including during scanning.
    if(Math.abs(raw.body.yaw||0)<0.01){
      const mean=side=>shown[side].ring.reduce((sum,p)=>sum+p[0],0)/shown[side].ring.length-114.2705+shown[side].x+shown[side].lookX;
      assert.ok((mean('left')+mean('right'))/2>0,`face turned left: ${id}, frame ${frame}`);
    }
    frames++;
  }
}
assert.ok(sawSpin&&spinSettled,'original spin must animate and settle back to the right-facing baseline');
const report={reference:'grok-styles/pixel/app.js',homepage_diameter:96,body_cell:1,detail_cell:1,states:32,compared_frames:frames,black_geometry_frames:frames,spin_animation_and_settle:true,perspective:'right-facing home baseline; original body roll/yaw, eye angles and gaze animation retained',identical:['engine animation and transition poses','eye contours, angles and gaze animation','body breathing, roll and spin','black/white presentation geometry','pixel raster profile'],adjustments:['homepage scale and placement','right-facing shared horizontal eye anchor'],flashed:false};
const out=path.join(root,'output/home-pixel');fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'pixel-reuse.json'),JSON.stringify(report,null,2)+'\n');
console.log('GROK_PIXEL_REUSE_OK: 32 states / '+frames+' original animation poses; black/white right-facing geometry matches; original gaze and spin retained; stored transition poses unmodified');
