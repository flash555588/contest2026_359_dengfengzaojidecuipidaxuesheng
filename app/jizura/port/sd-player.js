/* SPDX-License-Identifier: MIT */
'use strict';
const $=id=>document.getElementById(id),ctx=$('stage').getContext('2d');
let catalog,project,plan,renderer,index=0,seed=852,position=1.1,playing=false,touring=false,last=0,busy=false;
let randomSeed=852;Math.random=()=>{randomSeed=(Math.imul(randomSeed,1664525)+1013904223)>>>0;return randomSeed/4294967296;};
function make(style,n){
 const overrides={};for(let i=0;i<4;i++)overrides[i]={single:true,layout:style.layouts[i]};
 return Object.assign(J.defaultProject(),project,{extra:true,typo:true,kinetic:true,horror:true,style:style.id,mood:style.mood,
   res:540,fps:24,seed:n,overrides});
}
function draw(){if(!renderer||busy)return;renderer.frame(ctx,plan,position,{scale:ctx.canvas.width/plan.W});$('seek').value=position;$('clock').textContent=position.toFixed(2).padStart(5,'0')+' / 08.00';}
async function select(next,reseed=false){
 if(busy)return;busy=true;index=(next+catalog.styles.length)%catalog.styles.length;if(reseed)seed+=97;
 $('play').disabled=$('recompose').disabled=true;const style=catalog.styles[index];
 $('status').textContent='读取本地字体 · '+style.name;
 try{
  const p=make(style,seed);J.setLang('zh-Hans');await J.ensureFonts(p.lyrics+' '+p.title,[...Object.keys(J.FONTS),'@var']);
  randomSeed=852;plan=J.plan(p,null);renderer=new J.Renderer();
  $('name').textContent=style.name+' · '+String(index+1).padStart(2,'0')+' / 27';
  $('composition').textContent=[...new Set(plan.cuts.filter(c=>c.text).map(c=>c.layout))].slice(0,4).join(' / ');
  for(const button of $('library').children)button.setAttribute('aria-pressed',String(button.dataset.id===style.id));
  $('status').textContent='本地素材就绪 · '+(playing?'播放中':'可切换风格，或换一组构图');
 }finally{busy=false;$('play').disabled=$('recompose').disabled=false;draw();}
}
$('play').onclick=()=>{playing=!playing;$('play').textContent=playing?'暂停':'播放';last=performance.now();};
$('seek').oninput=()=>{position=+$('seek').value;draw();};
$('recompose').onclick=()=>select(index,true);
$('tour').onclick=()=>{touring=!touring;playing=touring;$('tour').setAttribute('aria-pressed',String(touring));$('play').textContent=playing?'暂停':'播放';last=performance.now();};
async function validate(){
 if(busy)return;$('validate').disabled=true;const result={offline:true,styles:[],errors:[],network:[]},prior=index,oldWarn=console.warn;
 console.warn=(...args)=>result.errors.push(args.join(' '));
 try{
  playing=false;$('play').textContent='播放';
  for(let i=0;i<catalog.styles.length;i++){
   await select(i);const cuts=new Set(),enters=new Set(),exits=new Set();
   for(const t of [.4,1.1,2.5,4.5,6.5]){position=t;draw();const cut=J.cutAt(plan,t);if(cut){cuts.add(cut.layout);enters.add(cut.enter);exits.add(cut.exit);}ctx.getImageData(0,0,960,540);}
   result.styles.push({id:catalog.styles[i].id,layouts:[...cuts],enter:[...enters],exit:[...exits]});
   await new Promise(r=>setTimeout(r,0));
  }
  result.layoutCount=new Set(result.styles.flatMap(s=>s.layouts)).size;
  result.enterCount=new Set(result.styles.flatMap(s=>s.enter)).size;result.exitCount=new Set(result.styles.flatMap(s=>s.exit)).size;
  result.network=performance.getEntriesByType('resource').map(r=>r.name).filter(n=>!n.startsWith(location.origin+'/'));
  result.fonts=[...document.fonts].filter(f=>f.status==='loaded').map(f=>({family:f.family,weight:f.weight}));
  result.pass=result.styles.length===27&&result.layoutCount>=12&&result.enterCount>=5&&result.exitCount>=5&&!result.network.length&&!result.errors.length;
 }catch(e){result.errors.push(e.stack);result.pass=false;}
 console.warn=oldWarn;$('proof').textContent=JSON.stringify(result,null,2);document.title=result.pass?'JIZURA_SD_STYLES_PASS':'JIZURA_SD_STYLES_REVIEW';
 $('validate').disabled=false;position=1.1;await select(prior);
}
$('validate').onclick=validate;
function tick(now){if(playing&&!busy&&plan){position+=(now-last)/1000;if(position>=8){position=0;if(touring)select(index+1);}draw();}last=now;requestAnimationFrame(tick);}requestAnimationFrame(tick);
Promise.all([fetch('styles.json').then(r=>r.json()),fetch('projects/demo.json').then(r=>r.json())]).then(async([c,p])=>{
 catalog=c;project=p;
 for(let i=0;i<c.styles.length;i++){const style=c.styles[i],button=document.createElement('button');button.className='theme';button.dataset.id=style.id;button.setAttribute('aria-pressed','false');
  const swatches=document.createElement('span');swatches.className='swatches';for(const color of [style.dark.background,style.dark.accent,style.light.background]){const dot=document.createElement('i');dot.style.background=color;swatches.append(dot);}button.append(swatches,document.createTextNode(style.name));const small=document.createElement('small');small.textContent=style.id.toUpperCase();button.append(small);button.onclick=()=>select(i);$('library').append(button);
 }
 await select(0);
}).catch(e=>{$('status').textContent='素材未就绪：'+e.message;});
