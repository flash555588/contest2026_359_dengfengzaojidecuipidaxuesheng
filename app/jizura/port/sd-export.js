/* SPDX-License-Identifier: MIT */
'use strict';
const el=id=>document.getElementById(id), canvas=el('stage'), ctx=canvas.getContext('2d',{willReadFrequently:true});
let seed=852;
Math.random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
function pack(rgba){
 const pixels=new Uint16Array(640*360);
 for(let i=0;i<pixels.length;i++)pixels[i]=((rgba[i*4]>>3)<<11)|((rgba[i*4+1]>>2)<<5)|(rgba[i*4+2]>>3);
 const raw=new Uint8Array(pixels.length*2);for(let i=0;i<pixels.length;i++){raw[i*2]=pixels[i]&255;raw[i*2+1]=pixels[i]>>8;}
 let a=1,b=0;for(let i=0;i<raw.length;){const end=Math.min(i+5552,raw.length);while(i<end){a+=raw[i++];b+=a;}a%=65521;b%=65521;}
 const checksum=((b<<16)|a)>>>0,out=[];
 const word=v=>{out.push(v&255,v>>8);};
 let i=0;
 while(i<pixels.length){
  let run=1;while(run<32768&&i+run<pixels.length&&pixels[i+run]===pixels[i])run++;
  if(run>=3){word(32768|run-1);word(pixels[i]);i+=run;continue;}
  const start=i;i+=run;
  while(i<pixels.length&&i-start<32768){
   if(i+2<pixels.length&&pixels[i]===pixels[i+1]&&pixels[i]===pixels[i+2])break;
   i++;
  }
  word(i-start-1);for(let p=start;p<i;p++)word(pixels[p]);
 }
 const encoded=out.length<raw.length?new Uint8Array(out):raw;
 const data=new Uint8Array(8+encoded.length), view=new DataView(data.buffer);
 view.setUint32(0,checksum,true);view.setUint32(4,encoded===raw?1:0,true);data.set(encoded,8);return data;
}
el('start').onclick=async()=>{
 el('start').disabled=true;const proof={styles:[],errors:[],frames:0,width:640,height:360},warn=console.warn;
 console.warn=(...x)=>proof.errors.push(x.join(' '));
 try{
  const [catalog,config]=await Promise.all([fetch('/styles.json').then(r=>r.json()),fetch('/job.json').then(r=>r.json())]);
  J.setLang('zh-Hans');await J.ensureFonts(config.project.lyrics+' '+config.project.title,[...Object.keys(J.FONTS),'@var']);
  for(const n of config.styles){
   const style=catalog.styles[n], overrides={};for(let i=0;i<4;i++)overrides[i]={single:true,layout:style.layouts[i]};
   const p=Object.assign(J.defaultProject(),config.project,{extra:true,typo:true,kinetic:true,horror:true,style:style.id,mood:style.mood,res:540,fps:config.fps,seed:852,overrides});
   seed=852;const plan=J.plan(p,null),renderer=new J.Renderer(),layouts=new Set();
   if(plan.W/plan.H!==16/9)throw Error('Project must be 16:9');
   for(let f=0;f<config.frames;f++){
    const t=f/config.fps;renderer.frame(ctx,plan,t,{scale:640/plan.W});const cut=J.cutAt(plan,t);if(cut)layouts.add(cut.layout);
    const body=pack(ctx.getImageData(0,0,640,360).data);let sent=false,error;
    for(let retry=0;retry<4&&!sent;retry++){
     try{const response=await fetch('/frame/'+n+'/'+f,{method:'POST',body});if(!response.ok)throw Error(await response.text());sent=true;}
     catch(e){error=e;await new Promise(r=>setTimeout(r,200*(retry+1)));}
    }
    if(!sent)throw error;proof.frames++;
    el('status').textContent=style.name+' · '+(n+1)+'/27 · '+(f+1)+'/'+config.frames+' 帧';
    if(f%12===0)await new Promise(r=>setTimeout(r,0));
   }
   proof.styles.push({index:n,id:style.id,layouts:[...layouts]});
  }
  proof.pass=!proof.errors.length;
  const done=await fetch('/finish',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(proof)});if(!done.ok)throw Error(await done.text());
  el('status').textContent='导出完成 · '+proof.frames+' 帧';document.title='JIZURA_BANKS_PASS';
 }catch(e){proof.pass=false;proof.errors.push(e.stack);document.title='JIZURA_BANKS_FAIL';el('status').textContent=e.message;}
 finally{console.warn=warn;el('proof').textContent=JSON.stringify(proof,null,2);}
};
