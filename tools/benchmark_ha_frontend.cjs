'use strict';
// Deterministic host workload; native write counts are not board FPS or CPU%.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {performance}=require('node:perf_hooks');
const repo=path.resolve(__dirname,'..'),testPath=path.join(repo,'app/espdl-quickapp/tests/homeassistant_frontend.test.cjs');
const harness=fs.readFileSync(testPath,'utf8').split(/\r?\ntest\(/)[0]+'\nglobalThis.makeRuntime=boot;';
const scope={require,console,__dirname:path.dirname(testPath)};
vm.runInNewContext(harness,scope,{filename:testPath});
const states=Array.from({length:200},(_,i)=>({entity_id:'sensor.metric_'+i,state:String(20+i/10),
  attributes:{friendly_name:'传感器 '+i,device_class:'temperature',unit_of_measurement:'°C'}}));
const devices=Array.from({length:7},(_,i)=>['dev_'+i,'设备 '+i,'',states.filter((_,j)=>j%7===i).map(s=>s.entity_id)]);
const savedLayout=JSON.stringify({server:'http://192.168.1.20:8123',sizes:{'device.dev_0':'large','device.dev_1':'large'}});
function measure(source,changing){
  const data=JSON.parse(JSON.stringify(states));
  const r=scope.makeRuntime(1024,536,{source,states:data,devices,savedLayout,nowMs:()=>1000});
  r.context.ESPHomeHA.refresh();for(let i=0;i<60;i++)r.tick();
  r.nativeWrites.length=0;const started=performance.now();
  for(let j=0;j<10;j++){
    if(changing)data[100].state=String(30+j);
    r.context.ESPHomeHA.refresh();for(let i=0;i<35;i++)r.tick();
  }
  const elapsed_ms=performance.now()-started,by_method={};
  for(const write of r.nativeWrites)by_method[write.method]=(by_method[write.method]||0)+1;
  const snapshot=r.context.ESPHomeHA.snapshot();
  if(snapshot.entities.length!==200||snapshot.deviceCards.length!==7)throw Error('Lost grouped entities');
  return {elapsed_ms:Number(elapsed_ms.toFixed(2)),native_writes:r.nativeWrites.length,by_method};
}
const before=fs.readFileSync(process.argv[2],'utf8');
const after=fs.readFileSync(path.join(repo,'app/espdl-quickapp/overlay/apps/system/desktop/homeassistant/app.js'),'utf8');
const results={method:'Node host, 200 entities / 7 devices / 2 visible large cards / 10 complete state refreshes',
  unchanged:{before:measure(before,false),after:measure(after,false)},
  changing:{before:measure(before,true),after:measure(after,true)}};
fs.writeFileSync(process.argv[3],JSON.stringify(results,null,2));console.log(JSON.stringify(results,null,2));
