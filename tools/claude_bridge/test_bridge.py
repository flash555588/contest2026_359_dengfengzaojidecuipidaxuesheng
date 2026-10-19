"""Protocol, process ownership, idempotency, recovery and bounded-output tests."""
import http.client
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import bridge

FAKE = r'''
import json,sys,time,subprocess,os,urllib.request
from pathlib import Path
prompt=sys.stdin.read()
def emit(value):print(json.dumps(value),flush=True)
session=sys.argv[sys.argv.index('--resume')+1] if '--resume' in sys.argv else sys.argv[sys.argv.index('--session-id')+1]
emit({'type':'system','session_id':session})
if prompt=='WAIT':
 child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'])
 Path('child.pid').write_text(str(child.pid))
 emit({'type':'stream_event','event':{'type':'content_block_start','content_block':{'type':'tool_use','name':'Bash'}}})
 time.sleep(60)
elif prompt=='OVERSIZE':
 sys.stdout.write('x'*1100000+'\n');sys.stdout.flush()
elif prompt=='PERMISSION':
 settings=json.loads(Path(sys.argv[sys.argv.index('--settings')+1]).read_text())
 hook=settings['hooks']['PermissionRequest'][0]['hooks'][0]
 request=urllib.request.Request(hook['url'],json.dumps({'tool_name':'Write','tool_input':{'file_path':'fixture.txt','content':'test'}}).encode(),{**hook['headers'],'Content-Type':'application/json'})
 answer=json.load(urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request))
 allow=answer['hookSpecificOutput']['decision']['behavior']=='allow'
 if allow:Path('fixture.txt').write_text('approved')
 emit({'type':'result','session_id':session,'is_error':not allow,'result':'allowed' if allow else 'denied'})
elif prompt=='BIG':
 for i in range(512):emit({'type':'stream_event','event':{'type':'content_block_delta','delta':{'type':'text_delta','text':'中文'*1024}}})
 emit({'type':'result','session_id':session,'is_error':False,'result':'中文'*5000})
elif prompt=='FAIL':emit({'type':'result','is_error':True,'result':'expected failure'});sys.exit(1)
else:emit({'type':'result','session_id':session,'is_error':False,'result':prompt})
'''

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.root = Path(self.tmp.name)
        self.project = self.root / '任意项目 with spaces'; self.project.mkdir()
        self.fake = self.root / 'fake.py'; self.fake.write_text(FAKE, encoding='utf-8')
        self.b = bridge.Bridge(self.root / 'state', 'fake-claude', show_terminal=False)
        self.server = bridge.ThreadingHTTPServer(('127.0.0.1', 0), bridge.handler(self.b)); self.server.daemon_threads = True
        self.b.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.b.clis['claude']['command']=[sys.executable,str(self.fake)]

    def tearDown(self):
        for ident in list(self.b.processes): self.b.stop(ident)
        end = time.monotonic() + 10
        while self.b.processes and time.monotonic() < end: time.sleep(.02)
        self.server.shutdown(); self.server.server_close(); self.tmp.cleanup()

    def wait(self, ident, state):
        end = time.monotonic() + 15
        while time.monotonic() < end:
            job = self.b.snapshot(ident)['task']
            if job['state'] == state and (state in bridge.ACTIVE or ident not in self.b.processes): return job
            time.sleep(.02)
        self.fail('Task did not reach '+state+': '+job['state']+' '+job['detail'])

    def submit(self, prompt, request='unique-request-0001', **extra):
        return self.b.submit(dict(project=str(self.project), prompt=prompt, request_id=request, **extra))['task']['id']

    def post(self, path, data, token=None, origin=None):
        c = http.client.HTTPConnection('127.0.0.1', self.b.port, timeout=5)
        headers = {'Content-Type':'application/json'}
        if token: headers['Authorization'] = 'Bearer '+token
        if origin: headers['Origin'] = origin
        c.request('POST', path, json.dumps(data), headers); r = c.getresponse(); value = json.loads(r.read()); c.close()
        return r.status, value

    def test_auth_and_origin(self):
        self.assertEqual(self.post('/api/status', {})[0], 403)
        self.assertEqual(self.post('/api/status', {}, self.b.token, 'http://evil.test')[0], 403)
        self.assertEqual(self.post('/api/status', {}, self.b.token)[0], 200)
        self.assertEqual(self.post('/api/pair', {'code':self.b.pair_code})[1]['token'], self.b.token)
        for _ in range(5): self.assertEqual(self.post('/api/pair', {'code':'invalid'})[0], 403)
        self.assertEqual(self.post('/api/pair', {'code':self.b.pair_code})[0], 403)

    def test_prompt_is_data_and_resume(self):
        prompt='--dangerously-skip-permissions; $(echo injected)'
        ident=self.submit(prompt); job=self.wait(ident, 'done')
        self.assertEqual(job['text'], prompt)
        second=self.submit('next', 'unique-request-0002', continue_id=ident)
        self.assertEqual(self.wait(second,'done')['session_id'],job['session_id'])

    def test_idempotent_after_restart_and_pruning(self):
        ident=self.submit('hello'); self.wait(ident,'done')
        self.assertEqual(self.submit('hello'),ident)
        with self.assertRaises(ValueError): self.submit('different')
        restarted=bridge.Bridge(self.b.root,'fake-claude',show_terminal=False)
        self.assertEqual(restarted.submit(dict(project=str(self.project),prompt='hello',request_id='unique-request-0001'))['task']['id'],ident)
        with self.b.lock: del self.b.jobs[ident]; self.b.save()
        with self.assertRaises(ValueError): self.submit('hello')

    def test_project_scope_and_bounded_result(self):
        with self.assertRaises(ValueError): self.b.add_project(str(self.root/'missing'))
        ident=self.submit('BIG'); job=self.wait(ident,'done')
        self.assertLessEqual(len(job['text'].encode()),4096)
        self.assertLess(len(json.dumps(self.b.snapshot(ident),ensure_ascii=False).encode()),bridge.LIMIT)
        self.assertLessEqual((self.b.root/(ident+'.jsonl')).stat().st_size,8*1024*1024)
        second=self.root/'other';second.mkdir()
        with self.assertRaises(ValueError):self.b.submit(dict(project=str(second),prompt='BIG',request_id='unique-request-0001'))

    def test_permission_allow_deny(self):
        ident=self.submit('PERMISSION'); job=self.wait(ident,'waiting')
        approval=job['approval']['id']
        self.assertEqual(job['approval']['tool'],'Write')
        self.b.approve({'approval_id':approval,'allow':True})
        self.wait(ident,'done'); self.assertEqual((self.project/'fixture.txt').read_text(),'approved')
        with self.assertRaises(ValueError):self.b.approve({'approval_id':approval,'allow':True})
        (self.project/'fixture.txt').unlink()
        ident=self.submit('PERMISSION','unique-request-0002'); job=self.wait(ident,'waiting')
        self.b.approve({'approval_id':job['approval']['id'],'allow':False})
        self.wait(ident,'failed'); self.assertFalse((self.project/'fixture.txt').exists())

    def test_failure_and_oversize_recover(self):
        for i,prompt in enumerate(['FAIL','OVERSIZE','hello']):
            ident=self.submit(prompt,'request-fail-'+str(i));self.wait(ident,'done' if prompt=='hello' else 'failed')

    def test_stop_process_tree(self):
        ident=self.submit('WAIT');self.wait(ident,'running')
        end=time.monotonic()+5
        while not (self.project/'child.pid').exists() and time.monotonic()<end:time.sleep(.02)
        self.assertTrue((self.project/'child.pid').exists())
        child=int((self.project/'child.pid').read_text())
        with self.assertRaises(ValueError):self.submit('overlap','request-overlap')
        self.b.stop(ident);self.wait(ident,'stopped');time.sleep(.1)
        if os.name=='nt':
            import ctypes
            from ctypes import wintypes as w
            k=ctypes.WinDLL('kernel32');k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
            k.GetExitCodeProcess.argtypes=[w.HANDLE,ctypes.POINTER(w.DWORD)];k.CloseHandle.argtypes=[w.HANDLE]
            handle=k.OpenProcess(0x1000,False,child)
            if handle:
                code=w.DWORD();k.GetExitCodeProcess(handle,ctypes.byref(code));k.CloseHandle(handle);self.assertNotEqual(code.value,259)
        else:
            try: os.kill(child,0)
            except ProcessLookupError: pass
            else:
                self.assertIn('Z',(Path('/proc')/str(child)/'stat').read_text().split()[2])

if __name__=='__main__':unittest.main(verbosity=2)
