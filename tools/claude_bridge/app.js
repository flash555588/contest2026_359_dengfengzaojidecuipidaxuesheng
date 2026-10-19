'use strict';
const $ = id => document.getElementById(id);
let cfg = {url:location.origin,token:sessionStorage.getItem('esp-claude-token') || '',selected:'',draft:''};
try {cfg = {...cfg,...JSON.parse(sessionStorage.getItem('esp-cli-config') || '{}'),url:location.origin};} catch (_) {}
const persist = () => sessionStorage.setItem('esp-cli-config',JSON.stringify(cfg));
let current = null, serial = 0, storageError=false;
const results = new Map();
const api = {
  request(url,token,action,body) {
    if (url !== location.origin) return -1;
    const id=++serial, abort=new AbortController();
    const timer=setTimeout(() => abort.abort(),5000);
    fetch('/api/'+action,{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+token},body,signal:abort.signal})
      .then(async response => ({done:true,error:0,status:response.status,body:await response.json()}))
      .catch(() => ({done:true,error:-1,status:0}))
      .then(value => {clearTimeout(timer);results.set(id,value);});
    return id;
  },
  poll(id) {const value=results.get(id);if(value)results.delete(id);return value || {done:false};}
};
const controller = new CliController(api,cfg,persist,{validUrl:url=>url===location.origin,message:s=>{$('error').textContent=s;},recovered:()=>{$('error').textContent='';},changed:render});
function render() {
  const active=current && ['starting','running','waiting','stopping'].includes(current.state);
  const ready=!controller.queued && (!controller.flight || controller.flight.action==='status');
  $('pair').hidden=!!cfg.token && !controller.authRequired; $('work').hidden=!cfg.token || controller.authRequired;
  $('newButton').disabled=!ready || !!controller.pending || storageError;
  $('followButton').disabled=!ready || !!controller.pending || storageError || !current || current.input_pending || active && !current.input;
  $('followButton').textContent=active && current.input ? '发送输入' : '继续当前会话';
  $('stopButton').disabled=!current || !active || current.state==='stopping' || !ready;
  $('refreshButton').textContent=controller.pending ? '核对操作' : '刷新';
  $('refreshButton').disabled=!ready;
  $('decision').hidden=!current?.approval;
  $('allowButton').disabled=!ready || storageError || !current?.approval || current.approval.can_approve===false;
  $('denyButton').disabled=!ready || !current?.approval;
  $('keys').hidden=!current?.interactive;
  document.querySelectorAll('[data-key]').forEach(b=>{b.disabled=!ready || storageError || !active || current?.state!=='running' || current?.input_pending || !!controller.pending;});
  $('status').textContent=storageError ? '电脑状态存储异常，已禁止新任务和输入；仍可停止任务。' : controller.pending ? '回执未确认，请核对原操作，避免重复执行。' : !controller.connected ? '正在自动重连…' : current ? current.state+' · '+current.detail : '选择项目和 CLI 后创建任务。';
  $('result').textContent=current?.last_result && (['idle','error','background'].includes(current.activity) || !active) ? current.last_result : current?.text || '';
  $('events').textContent=(current?.events || []).join(' | ');
  $('approval').textContent=current?.approval ? current.approval.tool+'\n'+current.approval.input : '';
}
function receive(data) {
  storageError=!!data.storage_error;
  const previousCli=$('cli').value;
  $('cli').replaceChildren(...(data.clis || []).map(cli=>{const o=document.createElement('option');o.value=cli.id;o.textContent=cli.name;o.selected=cli.id===previousCli;return o;}));
  if (!$('project').value) $('project').value=data.projects?.[0] || '';
  current=data.task || null;
  if(current)cfg.selected=current.id;else if(cfg.selected)cfg.selected='';
  $('tasks').replaceChildren(...(data.tasks || []).slice().reverse().map(task=>{const o=document.createElement('option');o.value=task.id;o.textContent=(task.state==='running'?task.activity || task.state:task.state)+' · '+task.prompt;o.selected=task.id===cfg.selected;return o;}));
  persist();render();
}
function refresh() {if(controller.pending)controller.reconcile(receive);else controller.send('status',{id:cfg.selected},receive);}
function submit(follow) {
  const text=$('prompt').value;
  if(!text.trim())return;
  cfg.draft=text;persist();
  const done=data=>{$('prompt').value='';receive(data);};
  if(follow && current?.input && current.state==='running')controller.send('input',{id:current.id,text,request_id:controller.nextId()},done);
  else controller.send('submit',{cli:follow&&current?current.cli:$('cli').value,project:follow&&current?current.project:$('project').value,prompt:text,request_id:controller.nextId(),...(follow&&current?{continue_id:current.id}:{})},done);
}
$('pairButton').addEventListener('click',()=>controller.send('pair',{code:$('code').value},data=>{cfg.token=data.token;persist();refresh();}));
$('newButton').addEventListener('click',()=>submit(false));
$('followButton').addEventListener('click',()=>submit(true));
$('refreshButton').addEventListener('click',refresh);
$('stopButton').addEventListener('click',()=>{if(current)controller.send('stop',{id:current.id},receive);});
$('tasks').addEventListener('change',()=>{cfg.selected=$('tasks').value;persist();refresh();});
for(const [id,allow] of [['allowButton',true],['denyButton',false]])$(id).addEventListener('click',()=>{if(current?.approval)controller.send('approve',{approval_id:current.approval.id,allow},refresh);});
document.querySelectorAll('[data-key]').forEach(b=>b.addEventListener('click',()=>controller.send('input',{id:current.id,key:b.dataset.key,request_id:controller.nextId()},receive)));
$('prompt').value=cfg.draft || '';
if(!cfg.token && ['127.0.0.1','localhost'].includes(location.hostname))controller.send('localpair',{},data=>{cfg.token=data.token;persist();refresh();});
render();setInterval(()=>controller.tick(receive),250);
