'use strict';
const assert=require('node:assert/strict');
const Controller=require('../../quickapp/claude_control/client.js');
function fixture(cfg) {
  let now=10000;const calls=[],results=new Map(),messages=[];
  const config={url:'http://192.168.1.2:8787',token:'a'.repeat(64),selected:'one',...cfg};
  const api={request:(...args)=>{calls.push(args);return calls.length;},poll:id=>results.get(id)||{done:false}};
  const c=new Controller(api,config,()=>{}, {now:()=>now,message:s=>messages.push(s)});
  return {c,config,calls,results,messages,advance:()=>{now+=5000;}};
}
{
  const f=fixture();f.c.send('submit',{request_id:'request-first',prompt:'task'});
  f.results.set(1,{done:true,error:-1});f.c.tick(()=>{});
  assert.equal(f.config.pending.data.request_id,'request-first');
  const recovered=new Controller({request:(...args)=>{f.calls.push(args);return 2;}},f.config,()=>{});
  recovered.reconcile(()=>{});
  assert.equal(JSON.parse(f.calls[1][3]).request_id,'request-first');
}
{
  const f=fixture();let seen=0;
  f.c.send('status',{id:'one'},()=>{seen++;});
  f.config.selected='two';f.c.send('status',{id:'two'},()=>{seen+=10;});
  f.results.set(1,{done:true,status:200,body:{ok:true}});f.c.tick(()=>{});
  assert.equal(seen,0);assert.equal(JSON.parse(f.calls[1][3]).id,'two');
  f.results.set(2,{done:true,status:200,body:{ok:true}});f.c.tick(()=>{});assert.equal(seen,10);
}
{
  const f=fixture();f.c.send('input',{id:'one',text:'x',request_id:'input-first'});
  f.results.set(1,{done:true,error:-1});f.c.tick(()=>{});f.advance();f.c.tick(()=>{});
  assert.equal(f.calls[1][2],'status');
  f.results.set(2,{done:true,status:200,body:{ok:true,task:{last_input:{request_id:'input-first',state:'delivered'}}}});
  f.c.tick(()=>{});assert.equal(f.c.pending,null);
}
{
  const f=fixture();f.c.send('submit',{request_id:'request-first'});
  f.results.set(1,{done:true,status:400,body:{accepted:false,error:'busy'}});f.c.tick(()=>{});
  assert.equal(f.c.pending,null);assert.equal(f.c.connected,true);assert.equal(f.messages[0],'busy');
}
{
  const f=fixture();f.c.send('pair',{code:'123456'},()=>{},'http://10.0.0.2:8787');
  assert.equal(f.calls[0][1],'');assert.equal(f.calls[0][0],'http://10.0.0.2:8787');
  assert.equal(Controller.validUrl('http://192.168.001.2:8787'),false);
  assert.equal(Controller.validUrl('http://192.168.1.2:0'),false);
  assert.equal(Controller.validUrl('http://8.8.8.8:8787'),false);
}
{
  const f=fixture();f.c.send('status',{},()=>{});
  f.results.set(1,{done:true,error:-1});f.c.tick(()=>{});
  assert.equal(f.messages.length,0);assert.equal(f.c.failures,1);
  const first=f.c.nextPoll;f.advance();f.c.tick(()=>{});
  f.results.set(2,{done:true,error:-1});f.c.tick(()=>{});
  assert.ok(f.c.nextPoll-first>5000);assert.equal(f.messages.length,0);
  f.advance();f.c.tick(()=>{});f.results.set(3,{done:true,status:200,body:{ok:true}});f.c.tick(()=>{});
  assert.equal(f.c.failures,0);assert.equal(f.c.connected,true);
}
{
  const f=fixture();f.c.send('status',{},()=>{});
  f.results.set(1,{done:true,status:403,body:{ok:false}});f.c.tick(()=>{});
  f.advance();f.c.tick(()=>{});assert.equal(f.calls.length,1);assert.equal(f.messages.length,1);
  f.c.send('pair',{code:'123456'},()=>{});f.results.set(2,{done:true,status:200,body:{ok:true,token:'b'.repeat(64)}});f.c.tick(()=>{});
  assert.equal(f.c.authRequired,false);
}
console.log('CLIENT_STATE_OK: receipts, stale selection, quiet reconnect/backoff, expired auth stops polling');
{
  const pending={action:'input',url:'http://192.168.1.2:8787',data:{id:'one',text:'do work',request_id:'recover-after-auth'}};
  const f=fixture({pending});f.c.send('status',{id:'one'},()=>{});
  f.results.set(1,{done:true,status:403,body:{ok:false}});f.c.tick(()=>{});
  f.c.send('pair',{code:'123456'},()=>{});
  f.results.set(2,{done:true,status:200,body:{ok:true,token:'b'.repeat(64)}});f.c.tick(()=>{});
  assert.equal(f.c.pending,pending);assert.equal(f.calls.filter(c=>c[2]==='input').length,0);
  f.c.reconcile(()=>{});assert.equal(JSON.parse(f.calls[2][3]).request_id,'recover-after-auth');
}
