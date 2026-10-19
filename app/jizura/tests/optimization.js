/* SPDX-License-Identifier: MIT */
'use strict';
const $ = id => document.getElementById(id), sleep = () => new Promise(r => setTimeout(r, 0));
const refFrame = $('ref-frame'), optFrame = $('opt-frame'), stage1Frame = $('stage1-frame'), benchFrame = $('bench-frame');
let R, O, S1, B2, rw, ow, sw, bw, demo, playing = false, busy = false, position = 0.65, lastTick = 0;
const ctxR = $('reference').getContext('2d'), ctxO = $('optimized').getContext('2d');
const download = (name, blob) => { const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name; a.textContent = name; $('downloads').append(a); };
function project(J, extra = {}) {
  return Object.assign(J.defaultProject(), { extra: true, typo: true, kinetic: true, horror: true,
    title: '慢下来的光', artist: 'JIZURA / 原创验证', lang: 'zh-Hans',
    lyrics: '[00:00.00]把喧嚣留在身后\n[00:02.00]让今天*闪闪发光*!\n[00:04.00]Hello / light\n[00:06.00]每一笔 / 都有回响',
    aspect: '16:9', res: 360, fps: 24, seed: 852,
    overrides: {0:{single:true,layout:'center',enter:'assemble',exit:'explode'},1:{single:true,enter:'assemble',exit:'explode'},2:{single:true,enter:'assemble'},3:{single:true,exit:'explode'}}, ...extra });
}
function setupDemo() {
  rw.resetRandom(); ow.resetRandom();
  const style = $('style').value || 'noir', pr = project(R,{style}), po = project(O,{style});
  demo = {pr,po,rp:R.plan(pr,null),op:O.plan(po,null),rr:new R.Renderer(),or:new O.Renderer()};
  drawDemo();
}
function drawDemo() {
  if (!demo || busy) return;
  const scale=640/demo.rp.W;
  demo.rr.frame(ctxR,demo.rp,position,{scale}); demo.or.frame(ctxO,demo.op,position,{scale});
  $('clock').textContent=position.toFixed(2).padStart(5,'0')+' / 08.00'; $('seek').value=position;
}
function stop() { playing=false; $('play').textContent='播放'; }
$('play').onclick=()=>{playing=!playing;$('play').textContent=playing?'暂停':'播放';lastTick=performance.now();};
$('seek').oninput=()=>{position=+$('seek').value;drawDemo();};
$('style').onchange=setupDemo;
$('editor').onclick=()=>window.open('optimized/zh-hans/','_blank');
function tick(now) { if(playing&&!busy){position=(position+(now-lastTick)/1000)%8;drawDemo();}lastTick=now;requestAnimationFrame(tick); }
requestAnimationFrame(tick);

function diff(a,b) {
  if(a.length!==b.length)throw Error('Pixel dimensions differ');
  let count=0,max=0;for(let i=0;i<a.length;i++){const d=Math.abs(a[i]-b[i]);if(d){count++;max=Math.max(max,d);}}
  return {differentChannels:count,maxDelta:max};
}
function read(c) { return c.getContext('2d',{willReadFrequently:true}).getImageData(0,0,c.width,c.height).data; }
function canvas(w,h) { const c=document.createElement('canvas');c.width=w;c.height=h;return c; }
function glyphMemory(J) {
  let white=0,mask=0,tint=0,pieces=0;
  for(const g of J.glyphs.map.values())for(const p of g.pieces){pieces++;if(p.portMask)mask+=p.portMask.data.byteLength;else white+=p.cv.width*p.cv.height*4;}
  for(const m of J.glyphs.tint.values())for(const c of m.values())tint+=c.width*c.height*4;
  return {pieces,whiteBytes:white,maskBytes:mask,tintBytes:tint,rasterLruBytes:J.portSprites?J.portSprites.bytes:0,
    note:'Payload/backing-size accounting; excludes VM, Canvas implementation overhead and unrelated surfaces.'};
}
function median(a) { a=[...a].sort((x,y)=>x-y);return a[Math.floor(a.length/2)]; }
function p95(a) { a=[...a].sort((x,y)=>x-y);return a[Math.min(a.length-1,Math.floor(a.length*.95))]; }

async function validate() {
  busy=true;stop();$('validate').disabled=true;$('movie').disabled=true;
  $('status').textContent='正在重置测试实例，隔离预览和编号历史…';
  try{await prepareEngines(true);}catch(e){busy=false;$('validate').disabled=$('movie').disabled=false;$('status').textContent='初始化失败：'+e.message;return;}
  const result={hostOnly:true,upstreamVersion:'0.10.1',counts:{},styles:0,glyphs:0,frames:0,differentFrames:0,differentChannels:0,maxDelta:0,errors:[],warnings:[],glyphTiming:{reference:[],optimized:[]},registered:{},tests:{},
    inputMode:'Reference-generated scene cloned before rendering; identical numeric font-advance snapshots; independent local metrics functions, glyph rasterization and renderers.',
    timingScope:'Browser JS/Canvas command submission; GPU presentation and ESP frame rate are not measured.',
    loadedFonts:{reference:[...rw.document.fonts].filter(f=>f.status==='loaded').map(f=>({family:f.family,weight:f.weight})),optimized:[...ow.document.fonts].filter(f=>f.status==='loaded').map(f=>({family:f.family,weight:f.weight}))}};
  const oldR=rw.console.warn,oldO=ow.console.warn,oldS=sw.console.warn,oldB=bw.console.warn;
  // Console belongs to each sandbox. Any swallowed renderer exception is a failure.
  rw.console.warn=(...a)=>result.warnings.push('reference '+a.join(' '));
  ow.console.warn=(...a)=>result.warnings.push('optimized '+a.join(' '));
  sw.console.warn=(...a)=>result.warnings.push('stage2-benchmark '+a.join(' '));
  bw.console.warn=(...a)=>result.warnings.push('stage3-benchmark '+a.join(' '));
  let label='initialization';
  const assert=(ok,m)=>{if(!ok)throw Error(label+': '+m);};
  try {
    result.initialDemo={freshSandboxes:true,
      referenceGlyphs:[...R.glyphs.map].map(([key,g])=>({key,ids:g.pieces.map(p=>p.id)})),
      optimizedGlyphs:[...O.glyphs.map].map(([key,g])=>({key,ids:g.pieces.map(p=>p.id)})),
      metricDifferences:[...R.metrics.m].filter(([key,value])=>value!==O.metrics.m.get(key)).slice(0,20)};
    result.counts=Object.fromEntries(R.GROUP_KEYS.map(g=>[g,R.order(g).length]));result.styles=R.STYLE_ORDER.length;
    assert(JSON.stringify(result.counts)===JSON.stringify(Object.fromEntries(O.GROUP_KEYS.map(g=>[g,O.order(g).length]))),'registry mismatch');
    // Real font masks, including shatter geometry and white/tinted output.
    R.glyphs.clear();O.glyphs.clear();
    for(const font of ['gothic_black','mincho_bold','dela'])for(const ch of ['光','响','A','鬱','。',' '])for(const res of [64,256,512]) {
      label='glyph '+font+' '+ch+' '+res;
      let t=performance.now();const a=R.glyphs.get(font,ch,res);result.glyphTiming.reference.push(performance.now()-t);
      t=performance.now();const b=O.glyphs.get(font,ch,res);result.glyphTiming.optimized.push(performance.now()-t);
      assert(a.pieces.length===b.pieces.length,'piece count');
      for(let i=0;i<a.pieces.length;i++){
        const x=a.pieces[i],y=b.pieces[i];
        for(const key of ['id','res','cx','cy','w','h','area'])assert(x[key]===y[key],key+' '+x[key]+' vs '+y[key]);
        assert(JSON.stringify(R.fragments(x,852))===JSON.stringify(O.fragments(y,852)),'fragment geometry');
        for(const col of ['#fff','#d5ac72','rgba(255,20,90,0.45)']){
          const ac=R.glyphs.sprite(x,col),bc=O.glyphs.sprite(y,col);
          assert(ac.width===bc.width&&ac.height===bc.height,'sprite size');
          assert(diff(read(ac),read(bc)).differentChannels===0,'sprite pixels');
        }
      }
      result.glyphs++;if(result.glyphs%9===0){$('status').textContent=label;await sleep();}
    }
    result.glyphMemory={reference:glyphMemory(R),optimized:glyphMemory(O)};
    result.lastGlyphKernel=O.portLastGlyph;
    // Exercise cache eviction while retaining piece identities and fragment seeds.
    O.portSprites.clear();O.portSprites.budget=32*1024;
    const ga=R.glyphs.get('gothic_black','光',256),gb=O.glyphs.get('gothic_black','光',256);
    for(let repeat=0;repeat<5;repeat++)for(let i=0;i<ga.pieces.length;i++){
      const color=`rgb(${repeat*41},${i*37%256},120)`,a=R.glyphs.sprite(ga.pieces[i],color),b=O.glyphs.sprite(gb.pieces[i],color);
      assert(diff(read(a),read(b)).differentChannels===0,'eviction pixels');
    }
    result.tests.eviction={pass:true,evictions:O.portSprites.stats.evictions,budgetBytes:O.portSprites.budget,retainedBytes:O.portSprites.bytes};
    assert(O.portSprites.bytes<=O.portSprites.budget,'cache budget');
    O.portSprites.clear();O.portSprites.budget=4*1024*1024;

    function pair(extra,width=320,height=180) {
      rw.resetRandom();ow.resetRandom();
      const pr=project(R,extra),rp=R.plan(pr,null),op=ow.JSON.parse(JSON.stringify(rp));
      O.metrics.m=new ow.Map(R.metrics.m);
      return {rp,op,rr:new R.Renderer(),or:new O.Renderer(),a:canvas(width,height),b:canvas(width,height)};
    }
    function frame(p,t,opt={}) {
      p.rr.frame(p.a.getContext('2d'),p.rp,t,{scale:p.a.width/p.rp.W,...opt});
      O.metrics.m=new ow.Map(R.metrics.m);
      p.or.frame(p.b.getContext('2d'),p.op,t,{scale:p.b.width/p.op.W,...opt});
      const d=diff(read(p.a),read(p.b));result.frames++;result.differentChannels+=d.differentChannels;result.maxDelta=Math.max(result.maxDelta,d.maxDelta);
      if(d.differentChannels){
        result.differentFrames++;
        if(!result.firstDiff){
          const planDifferences=[];
          const walk=(a,b,path)=>{
            if(planDifferences.length>=16||a===b)return;
            if(a&&b&&typeof a==='object'&&typeof b==='object'){
              for(const k of new Set([...Object.keys(a),...Object.keys(b)]))walk(a[k],b[k],path+'.'+k);
            }else if(JSON.stringify(a)!==JSON.stringify(b))planDifferences.push({path,reference:a,optimized:b});
          };
          walk(JSON.parse(JSON.stringify(p.rp)),JSON.parse(JSON.stringify(p.op)),'plan');
          const metrics=[];
          for(const [key,value] of R.metrics.m)if(value!==O.metrics.m.get(key))metrics.push({key,reference:value,optimized:O.metrics.m.get(key)});
          const glyphs=[];
          for(const [key,g] of R.glyphs.map){const h=O.glyphs.map.get(key);if(!h||g.pieces.map(p=>p.id).join(',')!==h.pieces.map(p=>p.id).join(','))glyphs.push({key,reference:g.pieces.map(p=>p.id),optimized:h&&h.pieces.map(p=>p.id)});}
          result.firstDiff={label,t,plansEqual:JSON.stringify(p.rp)===JSON.stringify(p.op),planDifferences,metrics:metrics.slice(0,20),glyphs:glyphs.slice(0,20)};
          p.a.toBlob(blob=>download('first-diff-reference.png',blob));p.b.toBlob(blob=>download('first-diff-optimized.png',blob));
        }
        if(result.errors.length<20)result.errors.push(label+' @'+t+': '+JSON.stringify(d));
      }
    }
    for(const group of R.GROUP_KEYS){result.registered[group]=0;
      for(const key of R.order(group)) {
        label=group+'.'+key;
        const override=group==='fx'?{}:group==='decor'?{single:true,decor:[key]}:{single:true,[group]:key};
        const p=pair({overrides:{0:override,1:override,2:override,3:override}},256,144);
        if(group==='fx'){p.rp.events=[{t:.3,dur:.5,type:key,amp:.8,seed:852}];p.op.events=[{t:.3,dur:.5,type:key,amp:.8,seed:852}];}
        for(const t of group==='fx'?[.31,.45,.65]:[.03,.45,1.2,3.9,4.3])frame(p,t);
        result.registered[group]++;if(result.frames%25===0){$('status').textContent='逐帧比对 '+label+' · '+result.frames+' 帧';await sleep();}
        if(result.differentFrames&&new URLSearchParams(location.search).has('diagnose'))throw Error('Stopped at first pixel difference for diagnosis');
      }
    }
    result.styleRenders=0;
    for(const style of R.STYLE_ORDER){label='style.'+style;const p=pair({style});for(const t of [.3,1.5,2.1,3.9,4.2])frame(p,t);result.styleRenders++;await sleep();}
    for(const options of [{transparent:true},{transparent:true,layer:'front'},{transparent:true,layer:'back'},{keyBg:'green'},{unify:true}]) {
      label='special '+JSON.stringify(options);const p=pair(options);
      for(const t of [.15,1.85,2.05,3.95,4.1,.45])frame(p,t,options);
    }
    label='1024x576';const full=pair({},1024,576);
    for(const t of [.15,.55,1.85,2.1,3.95,4.2])frame(full,t);
    result.tests.nativeResolution={width:1024,height:576,frames:6};
    label='gradient/context invalidation';const changing=pair({});
    for(const [width,height,texture,bg] of [[320,180,.6,'#ffffff'],[640,360,.6,'#ffffff'],[640,280,.2,'#111111'],[320,180,1,'#111111'],[320,180,.6,'#ffffff']]){
      changing.a.width=changing.b.width=width;changing.a.height=changing.b.height=height;
      changing.rp.fx.texture=changing.op.fx.texture=texture;
      changing.rp.style.schemes.forEach(s=>s.bg=bg);changing.op.style.schemes.forEach(s=>s.bg=bg);
      for(const t of [.5,2.1,4.2])frame(changing,t);
    }
    result.tests.gradientInvalidation={frames:15,pass:true};
    label='texture/text cache invalidation';
    const cached=pair({style:'paper'},640,360);
    for(const edit of [()=>{},()=>{},p=>p.fx.texture=.3,p=>p.style.schemes.forEach(s=>s.paper=false),
      p=>p.style.schemes.forEach(s=>s.bg='#1287ca'),p=>p.style.texture.paper=.25,
      p=>p.style.schemes.forEach(s=>s.bg='rgba(0,0,0,0.2)'),p=>p.style.schemes.forEach(s=>s.bg='#121212')]){
      edit(cached.rp);edit(cached.op);frame(cached,.65);frame(cached,.85);
    }
    assert(O.portTextLines.stats.hits>0,'no typesetting cache hits');
    assert(O.portTextLines.map.size<=128&&O.portTextLines.chars<=4096,'typesetting budget');
    for(const typeset of [true,false,true]){cached.rp.typeset=cached.op.typeset=typeset;frame(cached,.7);}
    // Clipping, inherited shadow state and transparent exports are unchanged.
    const clipped=pair({style:'paper'});
    for(const c of [clipped.a,clipped.b]){const x=c.getContext('2d');x.beginPath();x.arc(130,90,71.3,0,Math.PI*2);x.clip();x.shadowColor='#ff0000';x.shadowBlur=2;}
    frame(clipped,.7,{});
    const alpha=pair({style:'paper'});
    for(const opt of [{transparent:true},{transparent:true,layer:'front'},{transparent:true,layer:'back'},{}])frame(alpha,.65,opt);
    result.tests.canvasCaches={frames:24,textCacheStats:{...O.portTextLines.stats},textEntries:O.portTextLines.map.size,textChars:O.portTextLines.chars,
      additionalFrameSurfaces:0};
    result.frameTiming=[];
    for(const style of ['noir','paper','crimson']) {
      label='benchmark '+style;const p=pair({style},640,360),r=[],o=[];
      // Warm both paths; alternate measurement order to reduce ordering bias.
      for(let i=0;i<12;i++)frame(p,i/12);
      const renderR=t=>p.rr.frame(p.a.getContext('2d'),p.rp,t,{scale:p.a.width/p.rp.W});
      const renderO=t=>p.or.frame(p.b.getContext('2d'),p.op,t,{scale:p.b.width/p.op.W});
      for(let pass=0;pass<3;pass++)for(let i=0;i<48;i++){
        const t=i/6,measure=(f,list)=>{const s=performance.now();f(t);list.push(performance.now()-s);};
        if(i%2){measure(renderO,o);measure(renderR,r);}else{measure(renderR,r);measure(renderO,o);}
        if(i%12===11)await sleep();
      }
      result.frameTiming.push({style,width:640,height:360,samples:r.length,referenceMedianMs:median(r),optimizedMedianMs:median(o),referenceP95Ms:p95(r),optimizedP95Ms:p95(o)});
      await sleep();
    }
    result.spriteStats={...O.portSprites.stats,retainedBytes:O.portSprites.bytes,budgetBytes:O.portSprites.budget};
    // Compare the previous verified build against this revision with completed
    // Canvas work. Readback overhead is included equally; this is not display FPS.
    result.completedFrameTiming=[];
    for(const variant of ['noir','paper','crimson','long-events','background-noir','background-paper']){
      label='stage2/stage3 '+variant;$('status').textContent='整帧复测 '+variant;await sleep();
      const style=variant==='long-events'?'noir':variant.replace('background-','');
      sw.resetRandom();bw.resetRandom();
      const sp=S1.plan(project(S1,{style}),null),op=bw.JSON.parse(JSON.stringify(sp));
      const sr=new S1.Renderer(),or=new B2.Renderer(),sc=canvas(640,360),oc=canvas(640,360);
      if(variant.startsWith('background-'))for(const plan of [sp,op]){plan.cuts=[];plan.events=[];plan.hud=false;}
      if(variant==='long-events'){
        for(const plan of [sp,op]){
          plan.cuts.forEach(c=>{c.start+=1100;c.end+=1100;});plan.duration+=1100;
          plan.events=Array.from({length:20000},(_,i)=>({t:i*.06,dur:.18,amp:.01,type:i%2?'shake':'chroma',seed:852}));
          plan.events.unshift({t:0,dur:2000,type:'flash',amp:.1,seed:852});
        }
      }
      const a=[],b=[];
      const run=(renderer,c,plan,t)=>{renderer.frame(c.getContext('2d'),plan,t,{scale:c.width/plan.W});return read(c);};
      const base=variant==='long-events'?1100:0;
      // Warm the whole measured time range. Copy numeric inputs outside the
      // measured region; each engine calls its own local metrics function.
      for(let i=0;i<32;i++)run(sr,sc,sp,base+i/4);
      B2.metrics.m=new bw.Map(S1.metrics.m);
      for(let i=0;i<32;i++)run(or,oc,op,base+i/4);
      for(let repeat=0;repeat<3;repeat++)for(let i=0;i<32;i++){
        const t=base+i/4;let sa,oa;
        const sample=(fn,list)=>{const began=performance.now();const pixels=fn();list.push(performance.now()-began);return pixels;};
        if(i%2){oa=sample(()=>run(or,oc,op,t),b);sa=sample(()=>run(sr,sc,sp,t),a);}
        else{sa=sample(()=>run(sr,sc,sp,t),a);oa=sample(()=>run(or,oc,op,t),b);}
        assert(diff(sa,oa).differentChannels===0,'stage2/stage3 completed frame mismatch');
        if(i%8===7)await sleep();
      }
      result.completedFrameTiming.push({variant,samples:a.length,width:640,height:360,events:sp.events.length,
        stage2MedianMs:median(a),stage3MedianMs:median(b),stage2P95Ms:p95(a),stage3P95Ms:p95(b),
        textCacheStats:{...B2.portTextLines.stats},textEntries:B2.portTextLines.map.size,textChars:B2.portTextLines.chars,
        stage2ScratchBytes:sr.scratch.width*sr.scratch.height*4,stage3ScratchBytes:or.scratch.width*or.scratch.height*4,
        timingScope:'Full Canvas readback included; synthetic long-events fixture where stated; not physical display FPS.'});
    }
    result.pass=!result.errors.length&&!result.warnings.length&&result.differentFrames===0;
    const side=743,reference=side*side*13;
    result.scratchAccounting={referenceSegmentationBytes:reference,optimizedLastMeasuredBytes:result.lastGlyphKernel.scratchBytes,
      reductionPercent:100*(1-result.lastGlyphKernel.scratchBytes/reference),note:'512 bucket, excludes shared input RGBA and Canvas, output masks and VM objects.'};
    $('scratch').textContent=result.scratchAccounting.reductionPercent.toFixed(1)+'%';
    $('pixels').textContent=result.differentFrames===0?'0 像素差异':result.differentFrames+' 帧差异';
  }catch(e){result.errors.push(label+': '+e.stack);result.pass=false;}
  finally{
    rw.console.warn=oldR;ow.console.warn=oldO;sw.console.warn=oldS;bw.console.warn=oldB;busy=false;$('validate').disabled=false;$('movie').disabled=false;
    $('results').textContent=JSON.stringify(result,null,2);$('status').textContent=result.pass?'验证通过 · 样本逐帧一致。浏览器耗时与缓存数据见右侧。':'发现差异，请检查记录。';
    download('optimization-results.json',new Blob([JSON.stringify(result,null,2)],{type:'application/json'}));
    document.title=result.pass?'JIZURA_OPTIMIZATION_PASS':'JIZURA_OPTIMIZATION_REVIEW';setupDemo();
  }
}
$('validate').onclick=validate;
$('movie').onclick=async()=>{
  busy=true;stop();$('movie').disabled=true;
  try{
    ow.resetRandom();const p=project(O,{style:$('style').value||'noir'}),plan=O.plan(p,null);
    const movie=await O.exportMP4({plan,project:p,audio:null,range:{t0:0,t1:8},quality:'high',signal:new AbortController().signal,
      onProgress:(fraction)=>$('status').textContent='导出演示 '+Math.round(fraction*100)+'%'});
    download('optimized-demo.mp4',movie.blob);$('status').textContent='演示已导出 · '+movie.width+'×'+movie.height+' · '+movie.codec;
  }catch(e){$('status').textContent='导出失败：'+e.message;}
  finally{busy=false;$('movie').disabled=false;drawDemo();}
};
async function prepareEngines(fresh=false){
  const ready=f=>new Promise(resolve=>{if(f.contentWindow.J)resolve();else f.onload=resolve;});
  const frames=[refFrame,optFrame,stage1Frame,benchFrame];
  if(fresh)await Promise.all(frames.map(f=>new Promise(resolve=>{f.onload=resolve;f.src=f.getAttribute('src');})));
  else await Promise.all(frames.map(ready));
  rw=refFrame.contentWindow;ow=optFrame.contentWindow;sw=stage1Frame.contentWindow;bw=benchFrame.contentWindow;R=rw.J;O=ow.J;S1=sw.J;B2=bw.J;
  R.setLang('zh-Hans');O.setLang('zh-Hans');S1.setLang('zh-Hans');B2.setLang('zh-Hans');
  // A browser may lazily load another unicode-range when an effect uppercases
  // or scrambles a glyph. Preload every face and the engine's character pools in
  // both sandboxes before comparing; fallback/loading races are not port errors.
  const text='把喧嚣留在身后让今天闪闪发光每一笔都有回响光响鬱。 Hello light'+
    Array.from({length:95},(_,i)=>String.fromCharCode(i+32)).join('')+
    Object.values(R.POOLS).map(pool=>Object.values(pool).join('')).join('');
  await Promise.all([R.ensureFonts(text,Object.keys(R.FONTS)),O.ensureFonts(text,Object.keys(O.FONTS)),S1.ensureFonts(text,Object.keys(S1.FONTS)),B2.ensureFonts(text,Object.keys(B2.FONTS))]);
}
async function initialize(){
  await prepareEngines();
  for(const key of R.STYLE_ORDER){const option=document.createElement('option');option.value=key;option.textContent=R.STYLES[key].name||key;$('style').append(option);}
  $('effects').textContent=R.GROUP_KEYS.reduce((n,g)=>n+R.order(g).length,0);setupDemo();
  $('play').disabled=$('validate').disabled=$('movie').disabled=false;$('status').textContent='字体与引擎已就绪。可播放对比，或运行逐帧验证。';
}
initialize().catch(e=>{$('status').textContent=e.stack;});
