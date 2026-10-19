"""Task execution lifecycle; the service owns shared state and policy."""
from pathlib import Path
import codecs,json,os,re,subprocess,sys,threading,time
import cli_registry
from process_tree import ProcessTree
from protocol import tail,finish
from session_monitor import hook_settings

class SessionRuntime:
    def run(self, ident):
        process = tree = stderr_thread = None
        try:
            with self.lock:
                job = self.jobs[ident]
                if job['state'] == 'stopping':
                    job.update(state='stopped', detail='任务已停止')
                    return
                cli = self.clis[job.get('cli', 'claude')]
                if cli['protocol'] == 'claude':
                    settings = hook_settings(self.port,ident,self.hook_keys[ident])
                    path = self.root / (ident + '.settings.json'); self.atomic(path, settings)
                    if job.get('interactive'):
                        command = cli['command'] + ['--permission-mode', 'manual', '--settings', str(path),
                                   '--resume' if job['resume'] else '--session-id', job['session_id'], '--', job['prompt']]
                    else:
                        command = cli['command'] + ['-p', '--output-format', 'stream-json', '--verbose', '--include-partial-messages',
                                   '--permission-mode', 'manual', '--permission-prompts', 'none', '--settings', str(path),
                                   '--resume' if job['resume'] else '--session-id', job['session_id']]
                elif cli['protocol'] == 'codex': command = cli_registry.codex_command(cli, job)
                else: command = cli['command']
                # The worker waits for the ownership gate before spawning any CLI.
                command = [sys.executable, str(Path(__file__).with_name('terminal_worker.py')),
                               '--protocol', 'interactive' if job.get('interactive') else cli['protocol'], '--title', 'ESPClaw · ' + cli['name'] + ' · ' + Path(job['project']).name,
                               '--', *command]
                options = {'creationflags': subprocess.CREATE_NEW_CONSOLE if self.show_terminal else subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {'start_new_session': True}
                process = subprocess.Popen(command, cwd=job['project'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE, env={**os.environ, 'CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS': '60000'}, **options)
                tree = ProcessTree(process); self.processes[ident] = tree
                job.update(state='running', detail='正在连接 ' + cli['name']); self.save()
            gate = b'\0'
            payload = b'' if job.get('interactive') else (job['prompt'] + ('\n' if job.get('input') else '')).encode()
            process.stdin.write(gate + payload); process.stdin.flush()
            if not job.get('input'): process.stdin.close()
            def errors():
                while True:
                    raw = process.stderr.readline(8192)
                    if not raw: break
                    with self.lock: self.note(job, raw.decode('utf-8', errors='replace').strip())
            stderr_thread = threading.Thread(target=errors, daemon=True); stderr_thread.start()
            def deadline():
                end = time.monotonic() + 1800
                while process.poll() is None and time.monotonic() < end: time.sleep(.5)
                if process.poll() is None:
                    with self.lock: job['detail'] = '任务超过 30 分钟，已请求停止'
                    self.stop(ident)
            threading.Thread(target=deadline, daemon=True).start()
            count = 0
            decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
            with (self.root / (ident + '.jsonl')).open('wb') as log:
                while True:
                    line = process.stdout.read1(4096) if cli['protocol'] == 'text' else process.stdout.readline(1024 * 1024 + 1)
                    if not line: break
                    if len(line) > 1024 * 1024: raise ValueError('Claude Code 单条事件过大')
                    safe_line = self.safe(line.decode('utf-8',errors='replace')).encode()
                    if count + len(safe_line) <= 8 * 1024 * 1024: log.write(safe_line); count += len(safe_line)
                    if cli['protocol'] == 'text':
                        with self.lock:
                            decoded = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', decoder.decode(line))
                            job['text'] = tail(self.safe(job['text'] + decoded),4096); job['updated'] = time.time()
                        continue
                    try: event = json.loads(line)
                    except (ValueError, UnicodeError): continue
                    if isinstance(event, dict):
                        with self.lock: self.handle_event(job, event)
            code = process.wait(timeout=10)
            with self.lock:
                finish(job,code,cli['protocol'])
        except Exception as exc:
            with self.lock:
                job = self.jobs[ident]
                if job['state'] == 'stopping': job.update(state='stopped', detail='任务已停止')
                else: job.update(state='failed', detail=tail(str(exc), 240))
            if tree: tree.stop()
            elif process and process.poll() is None: process.kill()
        finally:
            if tree: tree.close()
            if process:
                if process.poll() is None: process.kill()
                process.wait(timeout=10)
                if stderr_thread: stderr_thread.join(timeout=2)
                if process.stdin and not process.stdin.closed:
                    try: process.stdin.close()
                    except OSError: pass
                process.stdout.close(); process.stderr.close()
            receipt=job.get('last_input')
            if receipt:
                wake=self.input_acks.get(receipt['request_id'])
                if wake:wake.set()
            writer=self.input_threads.get(ident)
            if writer:writer.join(timeout=6)
            with self.lock:
                self.hook_keys.pop(ident, None);self.input_threads.pop(ident,None)
                job['approval'] = None; job['updated'] = time.time()
                for key, pending in list(self.pending.items()):
                    if pending[0] == ident:
                        self.answers[key] = False; pending[1].set()
                try:
                    self.save()
                    path=self.root/(ident+'.settings.json');path.unlink(missing_ok=True)
                except OSError:
                    job.update(state='failed',detail='任务已结束，但无法保存状态；请检查电脑存储权限和空间')
                finally:
                    # No observers may treat cleanup as finished before disk I/O
                    # and input writers have finished using this state directory.
                    self.processes.pop(ident,None)
