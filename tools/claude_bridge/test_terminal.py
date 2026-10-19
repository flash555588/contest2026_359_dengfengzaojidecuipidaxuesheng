"""Real local process tests; no model service, credentials or network requests."""
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
import bridge
import terminal_worker


class OutputTests(unittest.TestCase):
    def test_claude_check_output_is_shown(self):
        output = []
        terminal_worker.show_event({'type':'user','message':{'content':[
            {'type':'tool_result','content':[{'type':'text','text':'Ran 4 tests\nOK'}]},
            {'type':'tool_result','is_error':True,'content':'check failed'}]}}, output.append)
        text = ''.join(output)
        self.assertIn('Ran 4 tests\nOK', text)
        self.assertIn('工具失败', text)
        self.assertIn('check failed', text)


@unittest.skipUnless(os.name == 'nt', 'Windows console integration')
class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.b = bridge.Bridge(self.root / 'state', 'unused-claude')

    def wait(self, predicate):
        end = time.monotonic() + 15
        while not predicate() and time.monotonic() < end: time.sleep(.05)
        self.assertTrue(predicate(), [(j['state'],j['detail'],j.get('text',''),j.get('events',[])) for j in self.b.jobs.values()])

    def tearDown(self):
        for key in list(self.b.processes): self.b.stop(key)
        self.wait(lambda: not self.b.processes)
        self.tmp.cleanup()

    def test_visible_console_input_and_owned_stop(self):
        script = self.root / 'console.py'
        script.write_text('''import ctypes,json,sys,subprocess,os
k=ctypes.WinDLL('kernel32');k.GetConsoleWindow.restype=ctypes.c_void_p
hwnd=k.GetConsoleWindow();visible=bool(ctypes.WinDLL('user32').IsWindowVisible(ctypes.c_void_p(hwnd)))
child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'])
print(json.dumps(dict(hwnd=hwnd,visible=visible,pid=os.getpid(),child=child.pid)),flush=True)
for line in sys.stdin: print('echo:'+line.strip(),flush=True)
''', encoding='utf-8')
        self.b.clis['fixture'] = dict(name='本地终端验证', command=[sys.executable, '-u', str(script)], protocol='text', input=True)
        key = self.b.submit(dict(cli='fixture', project=str(self.root), prompt='first', request_id='console-test-0001'))['task']['id']
        job = self.b.jobs[key]
        self.wait(lambda: 'echo:first' in job['text'])
        info = json.loads(job['text'].splitlines()[0])
        self.assertTrue(info['hwnd']); self.assertTrue(info['visible'])
        self.b.send_input(dict(id=key, text='second',request_id='console-input-0001'))
        self.wait(lambda: 'echo:second' in job['text'])
        self.b.stop(key)
        self.wait(lambda: job['state'] == 'stopped' and key not in self.b.processes)
        import ctypes
        kernel = ctypes.WinDLL('kernel32')
        kernel.OpenProcess.restype = ctypes.c_void_p
        for pid in (info['pid'], info['child']):
            handle = kernel.OpenProcess(0x100000, False, pid)
            if handle:
                self.assertEqual(kernel.WaitForSingleObject(ctypes.c_void_p(handle), 2000), 0)
                kernel.CloseHandle(ctypes.c_void_p(handle))
        print('VISIBLE_CONSOLE_VERIFIED', info['hwnd'], 'stdin/stdout/stop verified')

    def test_codex_protocol_kept_separate_from_console(self):
        script = self.root / 'json_cli.py'
        script.write_text('import sys,json\np=sys.stdin.read()\nfor e in [{"type":"thread.started","thread_id":"console-session"},{"type":"item.completed","item":{"type":"agent_message","text":p}},{"type":"turn.completed"}]: print(json.dumps(e),flush=True)\n', encoding='utf-8')
        self.b.clis['codex'] = dict(name='JSON 终端验证', command=[sys.executable, str(script)], protocol='codex', resume=True)
        key = self.b.submit(dict(cli='codex', project=str(self.root), prompt='console-json-ok', request_id='console-test-0002'))['task']['id']
        job = self.b.jobs[key]
        self.wait(lambda: job['state'] == 'done' and key not in self.b.processes)
        self.assertEqual(job['text'], 'console-json-ok')
        self.assertEqual(job['session_id'], 'console-session')
        lines = (self.b.root / (key + '.jsonl')).read_text().splitlines()
        self.assertEqual([json.loads(line)['type'] for line in lines], ['thread.started','item.completed','turn.completed'])

    def test_native_tty_and_esp_input(self):
        script = self.root / 'native.py'
        script.write_text('import sys,time\nprint("NATIVE_TTY="+str((sys.stdin.isatty(),sys.stdout.isatty(),sys.stderr.isatty())),flush=True)\nprint("INITIAL="+sys.argv[-1],flush=True)\nline=input()\nprint("ESP_NATIVE_INPUT="+line,flush=True)\ntime.sleep(1)\n', encoding='utf-8')
        self.b.clis['claude']['command'] = [sys.executable, '-u', str(script)]
        key = self.b.submit(dict(cli='claude',project=str(self.root),prompt='initial-once',request_id='native-console-001'))['task']['id']
        job = self.b.jobs[key]
        self.assertTrue(job['interactive']); self.assertTrue(job['input'])
        self.wait(lambda: 'NATIVE_TTY=(True, True, True)' in job['text'])
        self.assertIn('INITIAL=initial-once', job['text'])
        self.b.send_input(dict(id=key,text='native-input-ok',request_id='native-input-0001'))
        self.wait(lambda: job['state'] == 'done' and key not in self.b.processes)
        self.assertIn('ESP_NATIVE_INPUT=native-input-ok', job['text'])

    def test_native_console_close_is_stopped(self):
        script=self.root/'closed.py'
        script.write_text('import ctypes\nctypes.windll.kernel32.ExitProcess(0xc000013a)\n')
        self.b.clis['claude']['command']=[sys.executable,str(script)]
        key=self.b.submit(dict(cli='claude',project=str(self.root),prompt='/help',request_id='native-close-001'))['task']['id']
        self.wait(lambda:self.b.jobs[key]['state']=='stopped' and key not in self.b.processes)
        self.assertEqual(self.b.jobs[key]['detail'],'电脑终端已关闭')


if __name__ == '__main__': unittest.main(verbosity=2)
