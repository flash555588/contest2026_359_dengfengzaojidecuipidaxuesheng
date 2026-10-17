// SPDX-License-Identifier: Apache-2.0
const {test}=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../overlay/apps/system/desktop/camera/app.js'),'utf8');
function fixture(width=1024,height=504,dark=false){
  const widgets=[],calls=[];let tick,now=1000,scans=0,state={busy:false,error:0};
  let devices=Array.from({length:9},(_,i)=>({id:'usb:'+i,generation:1,name:'USB Camera '+i,transport:'usb',previewSupported:true}));
  let modes=[{width:640,height:480,index:1,intervals:[666666,1000000],continuous:false}];
  let recording={status:0,revision:0,video:false,path:''};
  function add(text,x,y,w,h,action){widgets.push({text,x,y,w,h,action,hidden:false});return widgets.length-1;}
  const ui={getSize:()=>({width,height}),card:dark?0x22252f:0xffffff,surface:dark?0x2e303d:0xf5f5f7,
    primary:0x252d46,secondary:0x616981,accent:0x77658f,background(){},
    text:(t,x,y,font)=>add(t,x,y,20,font+4),panel:(x,y,w,h)=>add('',x,y,w,h),
    button:(t,x,y,w,h,action)=>add(t,x,y,w,h,action),
    setSize:(id,w,h)=>Object.assign(widgets[id],{w,h}),setStyle:(id,s)=>Object.assign(widgets[id],s),
    setColor(){},setHidden:(id,hidden)=>widgets[id].hidden=hidden,setText:(id,text)=>widgets[id].text=text};
  const camera={devices:()=>{scans++;return devices;},modes:()=>modes,status:()=>state,frames:()=>0,
    recordStatus:()=>recording,setVideoMode:video=>{recording.video=video;return true;},
    photoStatus:()=>({status:0,path:''}),stop(){calls.push('stop');state={busy:false,error:0};},
    startDevice(...args){calls.push(args);state={busy:true,error:0};return true;}};
  const context=vm.createContext({ui,system:{camera},Date:{now:()=>now},setInterval:fn=>{tick=fn;}});
  vm.runInContext(source,context);
  return {widgets,calls,app:context.CameraPreview,tick(){now+=500;tick();},scans:()=>scans,
    devices:value=>devices=value,modes:value=>modes=value,state:value=>state=value,
    recording:value=>recording={...recording,...value},
    click(text){const w=widgets.find(w=>w.text===text&&w.action&&!w.hidden);assert(w,text);if(w.enabled!==false)w.action();}};
}
test('camera controls fit compact and board viewports in both themes, including paged devices',()=>{
  for(const [w,h] of [[640,400],[800,450],[1024,504],[1024,600]])for(const dark of [false,true]){
    const f=fixture(w,h,dark);f.click('设备');
    for(let page=0;page<9;page++){
      for(const item of f.widgets.filter(v=>!v.hidden)){
        assert(item.x>=0&&item.y>=0&&item.x+item.w<=w&&item.y+item.h<=h,JSON.stringify({w,h,item}));
      }
      f.click('下一页');
    }
    assert(f.widgets.length<=64);
    assert(f.widgets.some(v=>!v.hidden&&v.text.includes('USB Camera 8')));
    f.click('上一页');assert.equal(f.calls.length,0,'paging must not open hardware');
  }
});
test('video mode preserves mic notice and prevents restart until saving completes',()=>{
  const f=fixture();f.click('照片');assert(f.widgets.some(w=>w.text==='视频'));
  f.app.start();f.tick();f.recording({status:3,revision:1});f.state({busy:false,error:0});f.tick();
  assert.equal(f.widgets.find(w=>w.text==='开始预览').enabled,false);
  assert(f.widgets.some(w=>w.text.includes('正在保存录像')));
  const n=f.calls.length;f.app.start();assert.equal(f.calls.length,n);
  f.recording({status:4,path:'/sdcard/videos/VID_test.avi'});f.tick();
  assert.equal(f.widgets.find(w=>w.text==='开始预览').enabled,true);
  assert(f.widgets.some(w=>w.text.includes('录像已保存')));
  f.recording({status:-28,revision:2});f.tick();assert(f.widgets.some(w=>w.text==='SD 卡空间不足'));
});
test('camera discovery is throttled while idle and suspended during preview; disconnect clears start',()=>{
  const f=fixture();let before=f.scans();
  for(let i=0;i<3;i++)f.tick();assert.equal(f.scans(),before);
  f.tick();assert.equal(f.scans(),before+1);
  f.app.start();f.tick();assert.equal(f.calls.filter(Array.isArray).length,1);
  before=f.scans();for(let i=0;i<20;i++)f.tick();assert.equal(f.scans(),before);
  f.app.stop();f.devices([]);f.app.refresh();
  assert.equal(f.widgets.find(v=>v.text==='开始预览').enabled,false);
  assert(f.widgets.some(v=>!v.hidden&&v.text.includes('未检测到摄像头')));
});
test('camera chooses a supported device and rejects modes without valid frame intervals',()=>{
  const f=fixture();f.devices([{id:'usb:0',generation:2,name:'Unsupported',transport:'usb',previewSupported:false},
    {id:'csi:0',generation:2,name:'CSI Camera',transport:'csi',previewSupported:true}]);f.app.refresh();
  f.app.start();f.tick();assert.equal(f.calls.find(Array.isArray)[0],'csi:0');f.app.stop();
  f.devices([{id:'usb:1',generation:3,name:'Bad modes',transport:'usb',previewSupported:true}]);
  f.modes([{index:1,width:640,height:480,intervals:[0,-1,NaN],continuous:false}]);f.app.refresh();
  assert.equal(f.widgets.find(v=>v.text==='开始预览').enabled,false);
  const count=f.calls.filter(Array.isArray).length;f.app.start();f.tick();assert.equal(f.calls.filter(Array.isArray).length,count);
});
