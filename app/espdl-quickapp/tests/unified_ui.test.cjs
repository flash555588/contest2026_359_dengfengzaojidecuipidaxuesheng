/* SPDX-License-Identifier: Apache-2.0 */
const {test}=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const desktop=path.resolve(__dirname,'../overlay/apps/system/desktop');
const sources={recorder:'recorder/app.js',espdl:'espdl/app.js',
  hello:'../espclaw/qpk/hello/app.js',game:'../espclaw/qpk/game2048/app.js'};
for(const [width,height] of [[640,400],[800,450],[1024,504],[1024,568]])
for(const light of [true,false])for(const [name,file] of Object.entries(sources))
test(`${name} shared theme and controls ${width}x${height} light=${light}`,()=>{
  const widgets=[],jobs=[],timers=[];
  let background;
  const theme={light,background:light?0xf4f5f8:0x15171e,card:light?0xffffff:0x22252f,
    surface:light?0xf3f2f7:0x2e303d,primary:light?0x292c38:0xeeeef5,
    secondary:light?0x808491:0xa2a7b7,accent:light?0x77658f:0xc1aedb,
    onAccent:light?0xffffff:0x22252f,border:light?0xe8e9ef:0x383b48,
    danger:0xb34d63,success:0x39816a,cardRadius:18};
  const widget=(kind,label,x,y,w,h,callback)=>{
    widgets.push({kind,label,x,y,w,h,callback,style:{}});return widgets.length-1;
  };
  const ui={...theme,theme,getSize:()=>({width,height}),background:c=>background=c,
    text:(label,x,y,font)=>widget('text',label,x,y,undefined,font),
    panel:(x,y,w,h)=>widget('panel','',x,y,w,h),rect:(x,y,w,h)=>widget('rect','',x,y,w,h),
    button:(...args)=>widget('button',...args),number:(...args)=>widget('number',...args),
    setText:(id,label)=>widgets[id].label=label,setSize:(id,w,h)=>Object.assign(widgets[id],{w,h}),
    setPos:(id,x,y)=>Object.assign(widgets[id],{x,y}),
    setStyle:(id,style)=>Object.assign(widgets[id].style,style),
    setHidden:(id,hidden)=>widgets[id].hidden=hidden,setColor(){},front(){},onSwipe(){}};
  const audio={};
  for(const op of ['list','record','play','remove'])audio[op]=()=>{jobs.push(op);return jobs.length;};
  const system={audio,hardware:{poll:()=>({done:true,ok:true,result:{clips:
    Array.from({length:12},(_,i)=>({clip:`rec_${i+1}`,durationMs:1000,bytes:1000}))}})},
    storage:{get:()=>'',set(){}},camera:{devices:()=>[]},
    espdl:{status:()=>({busy:false}),preview:(x,y,w)=>widget('preview','',x,y,w,w*3/4)}};
  const context={ui,system,console:{log(){}},app:{getInfo:()=>({name:'你好',packageName:'hello',versionName:'1'})},
    prompt:{showToast(){},dialog(){}},setInterval:f=>timers.push(f)};
  vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(desktop,file),'utf8'),context);
  if(name==='recorder')timers.forEach(f=>f());
  assert.equal(background,theme.background);
  const controls=widgets.filter(w=>w.kind==='button'&&!w.hidden);
  for(const w of controls){
    assert(w.x>=0&&w.y>=0&&w.x+w.w<=width+1&&w.y+w.h<=height+1,`outside viewport: ${w.label}`);
    assert(w.h>=40,`touch height: ${w.label}`);
  }
  for(let i=0;i<controls.length;i++)for(let j=i+1;j<controls.length;j++){
    const a=controls[i],b=controls[j];
    assert(!(a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y),`overlapping controls: ${a.label}/${b.label}`);
  }
  if(name==='recorder')assert.deepEqual(jobs,['list'],'opening must only read the recording list');
});
