#!/usr/bin/env python3
"""Paired LAN controller for local computer CLIs, built into ESPClaw."""
from __future__ import annotations
import argparse
import collections
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import threading
import time
import uuid
import re
import cli_registry

from protocol import ACTIVE, LIMIT, VERSION, AlreadyAccepted, tail, text as valid_text, request_id as valid_id, project_value, redact
from state_store import atomic, InstanceLock
from process_tree import ProcessTree
from session_runtime import SessionRuntime
from session_monitor import SessionMonitor
from http_api import handler, BridgeHTTPServer as ThreadingHTTPServer

class Bridge(SessionRuntime, SessionMonitor):
    def __init__(self, state: Path, claude: str, port=8787, default_project: str | None=None, show_terminal=True):
        self.root = state.resolve(); self.root.mkdir(parents=True, exist_ok=True)
        (self.root / 'receipts').mkdir(exist_ok=True)
        self.claude, self.port = claude, port
        self.show_terminal = show_terminal
        self.storage_error = False
        self.clis = cli_registry.discover(claude, self.root)
        self.lock = threading.RLock()
        auth_path = self.root / 'auth.json'
        if not auth_path.exists():
            self.atomic(auth_path, {'token': secrets.token_hex(32), 'pair_code': f'{secrets.randbelow(1000000):06d}'})
        auth = json.loads(auth_path.read_text())
        if not isinstance(auth,dict) or not re.fullmatch(r'[0-9a-f]{64}',str(auth.get('token',''))):raise ValueError('配对凭据格式无效，请保留备份后检查')
        self.token, self.pair_code = auth['token'], f'{secrets.randbelow(1000000):06d}'
        self.atomic(auth_path, {'token':self.token,'pair_code':self.pair_code})
        self.pair_used = False
        self.pair_total = 0
        self.pair_attempts = collections.deque()
        self.pair_failures = collections.OrderedDict()
        self.pair_deadline = time.monotonic() + 600
        self.jobs = collections.OrderedDict(); self.projects = []
        self.processes = {}; self.pending = {}; self.answers = {}; self.hook_keys = {}; self.input_acks = {}; self.input_threads = {}
        db = self.root / 'tasks.json'
        if db.exists():
            saved = json.loads(db.read_text(encoding='utf-8'))
            if not isinstance(saved,dict) or saved.get('schema_version',1) not in (1,2):raise ValueError('状态文件版本不支持，请保留备份后检查')
            self.projects = saved.get('projects', [])[:24]
            for job in saved.get('jobs', [])[-32:]:
                if not isinstance(job,dict) or not re.fullmatch(r'[0-9a-f]{32}',str(job.get('id',''))):raise ValueError('状态文件任务编号无效')
                if job['state'] in ACTIVE:
                    job.update(state='interrupted', detail='电脑桥接服务已重启，任务未自动重发')
                elif job.get('interactive') and job['state']=='failed' and job.get('detail','').endswith('（3221225786）'):
                    job.update(state='stopped',detail='电脑终端已关闭')
                job['approval'] = None; job['input_pending']=False
                if job.get('last_input',{}).get('state')=='queued':job['last_input']['state']='uncertain'
                self.jobs[job['id']] = job
        if default_project:
            self.add_project(default_project)
        self.save()

    atomic = staticmethod(atomic)

    def save(self):
        try:self.atomic(self.root / 'tasks.json', {'schema_version':2,'projects': self.projects, 'jobs': list(self.jobs.values())})
        except OSError:
            self.storage_error=True
            raise

    def add_project(self, value):
        project_value(value)
        path = Path(value).expanduser().resolve()
        if str(path).startswith(('\\\\','//')): raise ValueError('不允许网络项目路径')
        if not path.is_dir(): raise ValueError('项目目录不存在')
        name = str(path)
        with self.lock:
            if name not in self.projects:
                self.projects = (self.projects + [name])[-24:]
                self.save()
        return name

    def safe(self, value):
        return redact(value, [self.token, *self.hook_keys.values()])

    def note(self, job, text):
        text = self.safe(text)
        job['events'] = (job.get('events', []) + [tail(text, 180)])[-10:]
        job['updated'] = time.time()

    def snapshot(self, selected=''):
        with self.lock:
            jobs = list(self.jobs.values())
            job = self.jobs.get(selected) if selected else (jobs[-1] if jobs else None)
            summaries = [{k: j.get(k) for k in ('id','state','activity','project','prompt','updated','cli')} for j in jobs[-6:]]
            for item in summaries: item['prompt'] = tail(item['prompt'], 96)
            result = {'ok': True, 'protocol_version':VERSION, 'storage_error':self.storage_error, 'terminal_windows': self.show_terminal and os.name == 'nt', 'projects': list(self.projects), 'clis': cli_registry.public(self.clis), 'tasks': summaries, 'task': json.loads(json.dumps(job)) if job else None}
            for item in result['tasks']: item['prompt'] = self.safe(item['prompt'])
            if result['task']:
                task=result['task']
                if task.get('interactive') and task['state'] in ACTIVE:
                    task['monitor'] = 'connected' if time.time()-task.get('monitor_at',task['created'])<10 else 'stale'
                    if task['monitor']=='stale':task['detail']='终端同步暂时中断；电脑任务继续运行'
                result['task']['text'] = tail(self.safe(result['task'].get('text', '')),4096)
                if 'last_result' in task:task['last_result']=tail(self.safe(task['last_result']),4096)
                result['task']['prompt'] = tail(self.safe(result['task']['prompt']),1024)
                result['task']['events'] = result['task'].get('events', [])[-8:]
            # The device has a fixed 16 KiB response buffer, irrespective of job age.
            while len(json.dumps(result, ensure_ascii=False).encode()) > LIMIT - 128:
                if len(result['projects']) > 1: result['projects'].pop(0)
                elif len(result['tasks']) > 1: result['tasks'].pop(0)
                elif result['task'] and result['task']['events']: result['task']['events'].pop(0)
                elif result['task'] and len(result['task']['text']) > 128: result['task']['text'] = tail(result['task']['text'], 128)
                else: raise ValueError('状态数据过大')
            return result

    def submit(self, args):
        if self.storage_error:raise ValueError('电脑状态存储异常，禁止启动任务；请修复后重启服务')
        prompt, request_id = args.get('prompt'), args.get('request_id')
        engine = args.get('cli', 'claude')
        if engine not in self.clis: raise ValueError('CLI 尚未安装或配置')
        cli = self.clis[engine]
        valid_text(prompt, '任务内容')
        valid_id(request_id)
        project_arg = project_value(args.get('project',''))
        project_name = str(Path(project_arg).expanduser().resolve())
        digest = hashlib.sha256(json.dumps([project_name, prompt, args.get('continue_id', ''), engine], ensure_ascii=False).encode()).hexdigest()
        receipt = self.root / 'receipts' / hashlib.sha256(request_id.encode()).hexdigest()
        with self.lock:
            for old in self.jobs.values():
                if old['request_id'] == request_id:
                    if old.get('digest') != digest: raise ValueError('提交编号已用于另一条任务')
                    return self.snapshot(old['id'])
            if receipt.exists(): raise AlreadyAccepted('该提交已被受理并归档；不会重新执行，请创建明确的新任务')
            project = self.add_project(args.get('project', ''))
            if any((j['state'] in ACTIVE or j['id'] in self.processes) and j['project'] == project for j in self.jobs.values()):
                raise ValueError('该项目已有任务正在运行，请先停止或等待完成')
            if sum(j['state'] in ACTIVE for j in self.jobs.values()) >= 3:
                raise ValueError('电脑当前已有三个运行任务')
            parent = self.jobs.get(args.get('continue_id', ''))
            if args.get('continue_id') and (not parent or parent['project'] != project or parent['state'] in ACTIVE or not parent.get('session_id') or parent.get('cli', 'claude') != engine or not cli.get('resume')):
                raise ValueError('该会话暂时无法继续')
            while len(self.jobs) >= 32:
                oldest = next((k for k,j in self.jobs.items() if j['state'] not in ACTIVE and k not in self.processes), None)
                if oldest is None: raise ValueError('任务列表已满')
                del self.jobs[oldest]
                # Remove only files named from the server-generated UUID.
                for suffix in ('.jsonl', '.settings.json'):
                    (self.root / (oldest + suffix)).unlink(missing_ok=True)
            ident = uuid.uuid4().hex
            interactive = os.name == 'nt' and self.show_terminal and cli['protocol'] == 'claude'
            job = dict(id=ident, request_id=request_id, project=project, prompt=prompt, cli=engine,
                       interactive=interactive, input=interactive or cli.get('input', False),
                       digest=digest,
                       session_id=parent['session_id'] if parent else str(uuid.uuid4()),
                       resume=bool(parent), state='starting', detail='正在启动 ' + cli['name'],
                       text='', events=[], approval=None, created=time.time(), updated=time.time())
            self.atomic(receipt, {'id': ident, 'digest': digest})
            self.jobs[ident] = job; self.hook_keys[ident] = secrets.token_hex(24)
            self.save()
            threading.Thread(target=self.run, args=(ident,), daemon=True).start()
            return self.snapshot(ident)

    def handle_event(self, job, event):
        kind = event.get('type')
        if job.get('interactive'):
            if kind == 'console_started':
                job['cli_pid'] = event.get('pid')
                job.update(monitor_at=time.time(),activity='starting')
                self.note(job, 'Claude Code 原生交互终端已打开')
            elif kind == 'console_screen':
                screen = str(event.get('text',''))
                job['monitor_at']=time.time()
                if screen.strip():
                    job['text'] = tail(self.safe(screen),4096)
                if job['state'] == 'running' and not job.get('observed_at'):
                    job['detail'] = '原生终端已连接；等待 Claude 状态事件'
            elif kind == 'console_heartbeat':
                if event.get('ok'):job['monitor_at']=time.time()
            elif kind == 'console_input_ack':
                receipt = job.get('last_input')
                if receipt and receipt['request_id'] == event.get('request_id'):
                    receipt['state'] = 'delivered' if event.get('ok') else 'failed'
                    self.atomic(self.input_receipt_path(receipt['request_id']), receipt)
                    wake = self.input_acks.get(receipt['request_id'])
                    if wake: wake.set()
            elif kind == 'console_exit':
                if job.get('result_state')!='failed':job['result_state'] = 'done' if event.get('code') == 0 else 'failed'
            job['updated'] = time.time()
            return
        if job.get('cli') == 'codex':
            if kind == 'thread.started': job['session_id'] = event['thread_id']
            elif kind in ('item.started', 'item.updated', 'item.completed'):
                item = event.get('item', {})
                if item.get('type') == 'agent_message': job['text'] = tail(item.get('text', ''), 4096)
                else:
                    job['detail'] = '正在执行 ' + str(item.get('type', ''))
                    self.note(job, str(item.get('command', item.get('text', item.get('type', '')))))
            elif kind == 'turn.completed': job['result_state'] = 'done'
            elif kind in ('turn.failed', 'error'):
                job['result_state'] = 'failed'; self.note(job, str(event.get('error', event.get('message', 'Codex 失败'))))
            job['updated'] = time.time()
            return
        if event.get('session_id'): job['session_id'] = event['session_id']
        if kind == 'system' and event.get('subtype') == 'api_retry':
            error = str(event.get('error', ''))
            reason = 'Claude 服务限流' if error == 'rate_limit' else 'Claude 服务暂时不可用'
            job['detail'] = reason + '，正在重试 ' + str(event.get('attempt', ''))
            self.note(job, job['detail'])
        if kind == 'stream_event':
            value = event.get('event', {}); delta = value.get('delta', {})
            if delta.get('type') == 'text_delta':
                job['text'] = tail(job['text'] + delta.get('text', ''), 4096)
                job['detail'] = '正在回复'
            elif value.get('type') == 'content_block_start':
                block = value.get('content_block', {})
                if block.get('type') == 'tool_use':
                    self.note(job, '调用工具：' + str(block.get('name', '')))
                    job['detail'] = '正在执行 ' + str(block.get('name', ''))
                elif block.get('type') == 'thinking': job['detail'] = '正在思考'
        elif kind == 'user':
            for block in event.get('message', {}).get('content', []):
                if isinstance(block, dict) and block.get('type') == 'tool_result':
                    self.note(job, '工具失败' if block.get('is_error') else '工具已返回')
        elif kind == 'result':
            job['result_state'] = 'failed' if event.get('is_error') else 'done'
            if job['state'] != 'stopping': job['detail'] = '已收到本轮结果，正在结束'
            if event.get('result'): job['text'] = tail(event['result'], 4096)
            for error in event.get('errors', []): self.note(job, str(error))
            job['cost_usd'] = event.get('total_cost_usd')
        job['updated'] = time.time()

    def stop(self, ident):
        with self.lock:
            job = self.jobs.get(ident)
            if not job: raise ValueError('找不到任务')
            if job['state'] in ACTIVE:
                job.update(state='stopping', detail='正在停止')
                for key, pending in list(self.pending.items()):
                    if pending[0] == ident: self.answers[key] = False; pending[1].set()
                if ident in self.processes: self.processes[ident].stop()
                self.save()
            return self.snapshot(ident)

    def permission(self, ident, key, data):
        with self.lock:
            expected=self.hook_keys.get(ident)
            if not expected or not hmac.compare_digest(key,expected): raise PermissionError()
            job = self.jobs[ident]
            if job['state'] not in ACTIVE or job['state'] == 'stopping' or job.get('approval'): return self.decision(False)
            options = data.get('tool_input', {})
            if not isinstance(options,dict):raise ValueError('工具参数应为对象')
            approval_id = uuid.uuid4().hex; wake = threading.Event()
            self.pending[approval_id] = (ident, wake)
            preview = json.dumps(options, ensure_ascii=False)
            complete = len(preview.encode())<=2048
            if len(preview.encode()) > 2048:
                location = str(options.get('file_path', options.get('path', '')))
                preview = '目标：' + location + '\n' + preview.encode()[:1600].decode('utf-8', errors='ignore') + '\n（内容预览已截断）'
            job['approval'] = dict(id=approval_id, tool=tail(data.get('tool_name', ''), 80), input=self.safe(preview), can_approve=complete)
            job.update(state='waiting', detail='等待设备确认操作'); self.save()
        wake.wait(240)
        with self.lock:
            allow = self.answers.pop(approval_id, False)
            self.pending.pop(approval_id, None)
            if job['state'] == 'waiting': job.update(state='running', detail='继续工作' if allow else '本次操作未获准')
            job['approval'] = None; self.save()
            return self.decision(allow)

    @staticmethod
    def decision(allow):
        decision = {'behavior': 'allow' if allow else 'deny'}
        if not allow: decision['message'] = 'ESP 上的本次操作未获准'
        return {'hookSpecificOutput': {'hookEventName': 'PermissionRequest', 'decision': decision}}

    def approve(self, args):
        with self.lock:
            ident = args.get('approval_id', '')
            if ident not in self.pending or not isinstance(args.get('allow'), bool): raise ValueError('确认已失效，请刷新')
            if ident in self.answers: raise ValueError('本次操作已经确认')
            task=self.jobs[self.pending[ident][0]]
            if args['allow'] and (self.storage_error or task['state']!='waiting' or not task.get('approval',{}).get('can_approve',True)): raise ValueError('操作已停止、存储异常或参数无法完整显示，不能在 ESP 允许')
            self.answers[ident] = args['allow']; self.pending[ident][1].set()
            return {'ok': True, 'accepted': True}

    def input_receipt_path(self, request_id):
        return self.root / 'receipts' / ('input-' + hashlib.sha256(request_id.encode()).hexdigest())

    def send_input(self, args):
        if self.storage_error:raise ValueError('电脑状态存储异常，禁止继续输入')
        key = args.get('key')
        if key is not None:
            from protocol import KEYS
            if key not in KEYS or 'text' in args: raise ValueError('终端按键无效')
            text = None
        else: text = valid_text(args.get('text'))
        ident = args.get('id', '')
        request_id = args.get('request_id')
        valid_id(request_id)
        digest = hashlib.sha256(json.dumps([ident,text,key],ensure_ascii=False).encode()).hexdigest()
        path = self.input_receipt_path(request_id)
        with self.lock:
            if path.exists():
                receipt = json.loads(path.read_text(encoding='utf-8'))
                if receipt['digest'] != digest: raise ValueError('该输入编号已被使用')
                result = self.snapshot(ident); result['input_receipt'] = receipt
                return result
            job = self.jobs.get(ident)
            if not job or not job.get('input') or job['state'] != 'running' or ident not in self.processes:
                raise ValueError('该 CLI 当前不接受输入')
            if key and not job.get('interactive'): raise ValueError('此 CLI 不支持终端按键')
            if job.get('input_pending'): raise ValueError('上一次输入仍在发送，请稍后再试')
            receipt = dict(request_id=request_id,digest=digest,task_id=ident,state='queued')
            self.atomic(path,receipt)
            job['input_pending'] = True; job['last_input'] = receipt
            if job.get('interactive') and not key:
                job.update(activity='interacting',detail='输入已排队；等待 Claude 接收')
            process = self.processes[ident].process
            wake = threading.Event(); self.input_acks[request_id] = wake
            def write():
                try:
                    packet = {'request_id':request_id,'key':key} if key else {'request_id':request_id,'text':text}
                    payload = json.dumps(packet,ensure_ascii=True) if job.get('interactive') else text
                    process.stdin.write((payload+'\n').encode()); process.stdin.flush()
                    if job.get('interactive'): wake.wait(5)
                    with self.lock:
                        if receipt['state']=='queued': receipt['state']='uncertain' if job.get('interactive') else 'delivered'
                        message = '输入已送达终端' if receipt['state']=='delivered' else '输入未确认，请查看终端；不会自动重发'
                except (OSError,ValueError):
                    with self.lock: receipt['state']='failed'
                    message = 'CLI 已关闭输入通道'
                finally:
                    with self.lock:
                        job['input_pending']=False
                        self.input_acks.pop(request_id,None)
                        try:
                            self.atomic(path,receipt); self.note(job,message); self.save()
                        except OSError:
                            self.storage_error=True
                            self.note(job,'输入结果无法保存；已禁止新任务和输入，请检查电脑存储')
            self.note(job,'设备输入已排队'); self.save()
            thread=threading.Thread(target=write,daemon=True)
            self.input_threads[ident]=thread;thread.start()
            result = self.snapshot(ident); result['input_receipt']=dict(receipt)
            return result

    def action(self, action, args, address):
        if action == 'pair':
            with self.lock:
                now=time.monotonic()
                while self.pair_attempts and self.pair_attempts[0]<now-60:self.pair_attempts.popleft()
                if len(self.pair_attempts)>=30:raise PermissionError('配对尝试过多，请稍后再试')
                if self.pair_total>=30:raise PermissionError('本次启动的配对次数已用完，请在电脑重启服务')
                self.pair_total+=1
                self.pair_attempts.append(now)
                failures=self.pair_failures.setdefault(address,collections.deque())
                while len(self.pair_failures)>64:self.pair_failures.popitem(last=False)
                while failures and failures[0]<now-60:failures.popleft()
                if self.pair_used or len(failures)>=5 or now>self.pair_deadline:raise PermissionError('配对已使用或过期，请在电脑上重启桥接服务')
                if not hmac.compare_digest(str(args.get('code','')),self.pair_code):
                    failures.append(now);raise PermissionError('配对码不正确')
                self.pair_used=True
                return {'ok':True,'token':self.token}
        if action == 'status': return self.snapshot(args.get('id', ''))
        if action == 'project':
            path = self.add_project(args.get('path'))
            result = self.snapshot(args.get('id', '')); result['selected_project'] = path
            return result
        if action == 'submit': return self.submit(args)
        if action == 'input': return self.send_input(args)
        if action == 'stop': return self.stop(args.get('id', ''))
        if action == 'approve': return self.approve(args)
        raise ValueError('未知操作')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--state', type=Path, default=Path(__file__).with_name('.state'))
    p.add_argument('--bind', default='0.0.0.0'); p.add_argument('--port', type=int, default=8787)
    p.add_argument('--claude', default=shutil.which('claude') or str(Path.home()/'.local/bin/claude.exe'))
    p.add_argument('--project')
    p.add_argument('--no-terminal', action='store_true', help='Run tasks without visible Windows console windows')
    a = p.parse_args()
    lock = InstanceLock(a.state.resolve())
    try:
        bridge = Bridge(a.state,a.claude,a.port,a.project,show_terminal=not a.no_terminal)
        server = ThreadingHTTPServer((a.bind,a.port),handler(bridge))
        def interrupted(*_): raise KeyboardInterrupt()
        signal.signal(signal.SIGTERM,interrupted)
        print(f'ESPCLAW_CLI_BRIDGE_READY port={a.port}',flush=True)
        try: server.serve_forever()
        except KeyboardInterrupt: pass
        finally:
            for ident in list(bridge.processes): bridge.stop(ident)
            deadline=time.monotonic()+10
            while bridge.processes and time.monotonic()<deadline:time.sleep(.05)
            server.server_close()
    finally: lock.close()

if __name__ == '__main__': main()
