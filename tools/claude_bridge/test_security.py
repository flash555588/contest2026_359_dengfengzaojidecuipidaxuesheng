"""Adversarial LAN API and recovery tests with isolated local state."""
import http.client
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import bridge
from protocol import loads, text
from state_store import InstanceLock


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.b=bridge.Bridge(self.root/'state','unused',show_terminal=False)
        self.server=bridge.ThreadingHTTPServer(('127.0.0.1',0),bridge.handler(self.b));self.b.port=self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever,daemon=True).start()
    def tearDown(self):
        for key in list(self.b.processes):self.b.stop(key)
        end=time.monotonic()+10
        while (self.b.processes or any(j.get('input_pending') for j in self.b.jobs.values())) and time.monotonic()<end:time.sleep(.02)
        self.server.shutdown();self.server.server_close();self.tmp.cleanup()
    def post(self,body,extra=None):
        c=http.client.HTTPConnection('127.0.0.1',self.b.port,timeout=3)
        headers={'Content-Type':'application/json','Authorization':'Bearer '+self.b.token};headers.update(extra or {})
        c.request('POST','/api/status',body,headers);r=c.getresponse();data=r.read();c.close();return r.status,json.loads(data)
    def test_strict_request_contract(self):
        for body in [b'{"id":"a","id":"b"}',b'{"id":NaN}',b'[]',b'{"id":'+b'['*20+b'0'+b']'*20+b'}']:
            self.assertEqual(self.post(body)[0],400)
        self.assertEqual(self.post(b'{}',{'Content-Type':'text/plain'})[0],400)
        self.assertEqual(self.post(b'{}',{'Origin':'null'})[0],403)
        self.assertEqual(self.post(b'{}',{'Host':'attacker.example:'+str(self.b.port)})[0],400)
        self.assertEqual(self.post(b'{}')[0],200)
        self.assertEqual(self.post(b'{}',{'Host':'127.0.0.1'})[0],200)
        self.assertEqual(self.post(b'{}',{'Host':'192.168.31.95'})[0],200)
        self.assertEqual(self.post(b'{}',{'Host':'127.0.0.1','Origin':'http://127.0.0.1'})[0],403)
        self.assertEqual(self.post(b'{}',{'Host':'127.0.0.1','Sec-Fetch-Site':'same-origin'})[0],403)
        self.assertEqual(self.post(b'{}',{'Host':'127.0.0.1:1'})[0],403)
        self.assertEqual(self.post(b'{}',{'Host':'localhost'})[0],403)
        self.assertEqual(self.post(b'{}',{'Host':'8.8.8.8'})[0],403)
        self.assertEqual(self.post(b'{}',{'Authorization':'Bearer invalid','Host':'192.168.31.95'})[0],403)
    def test_header_and_concurrency_bounds(self):
        sock=socket.create_connection(self.server.server_address,timeout=2)
        sock.sendall(b'POST /api/status HTTP/1.0\r\nX-Huge: '+b'x'*20000+b'\r\n\r\n')
        data=sock.recv(1024);sock.close();self.assertNotIn(b'200 OK',data)
        peers=[]
        try:
            for _ in range(16):
                peer=socket.create_connection(self.server.server_address,timeout=2);peer.sendall(b'POST / HTTP/1.0\r\n');peers.append(peer)
            time.sleep(.1)
            extra=socket.create_connection(self.server.server_address,timeout=2);extra.settimeout(2)
            self.assertEqual(extra.recv(1),b'');extra.close()
        finally:
            for peer in peers:peer.close()
    def test_pair_once_and_token_compatibility(self):
        token=self.b.token;code=self.b.pair_code
        self.assertEqual(self.b.action('pair',{'code':code},'127.0.0.1')['token'],token)
        with self.assertRaises(PermissionError):self.b.action('pair',{'code':code},'127.0.0.1')
        recovered=bridge.Bridge(self.b.root,'unused',show_terminal=False)
        self.assertEqual(recovered.token,token)
    def test_single_instance_lock(self):
        first=InstanceLock(self.root/'lock')
        try:
            with self.assertRaises(RuntimeError):InstanceLock(self.root/'lock')
        finally:first.close()
        second=InstanceLock(self.root/'lock');second.close()
    def test_control_characters_and_network_paths(self):
        for value in ['hello\x1b[31m','\0','hello\x03','hello\rsubmit']:
            with self.assertRaises(ValueError):text(value)
        with self.assertRaises(ValueError):self.b.add_project('\\\\server\\share')
        self.assertEqual(text('中文\n多行\t输入'),'中文\n多行\t输入')
    def test_oversized_permission_cannot_be_approved(self):
        job=dict(id='fixture',state='running',approval=None,events=[])
        self.b.jobs['fixture']=job;self.b.hook_keys['fixture']='x'*32
        answers=[]
        thread=threading.Thread(target=lambda:answers.append(self.b.permission('fixture','x'*32,{'tool_name':'Write','tool_input':{'path':'file','content':'x'*3000}})))
        thread.start()
        end=time.monotonic()+2
        while not job['approval'] and time.monotonic()<end:time.sleep(.01)
        self.assertFalse(job['approval']['can_approve'])
        key=job['approval']['id']
        with self.assertRaises(ValueError):self.b.approve({'approval_id':key,'allow':True})
        duplicate=self.b.permission('fixture','x'*32,{'tool_name':'Other'})
        self.assertEqual(duplicate['hookSpecificOutput']['decision']['behavior'],'deny')
        self.b.approve({'approval_id':key,'allow':False});thread.join(2);self.assertFalse(thread.is_alive())
    def test_input_receipt_survives_lost_reply_and_restart(self):
        script=self.root/'echo.py';script.write_text('import sys\nfor line in sys.stdin:print("line="+line.strip(),flush=True)\n')
        self.b.clis['echo']=dict(name='echo',command=[sys.executable,'-u',str(script)],protocol='text',input=True)
        key=self.b.submit(dict(cli='echo',project=str(self.root),prompt='first',request_id='submit-first-001'))['task']['id'];job=self.b.jobs[key]
        end=time.monotonic()+5
        while 'line=first' not in job['text'] and time.monotonic()<end:time.sleep(.02)
        args=dict(id=key,text='second',request_id='input-first-001')
        self.b.send_input(args);self.b.send_input(args)
        end=time.monotonic()+5
        while (job.get('input_pending') or 'line=second' not in job['text']) and time.monotonic()<end:time.sleep(.02)
        self.assertEqual(job['text'].count('line=second'),1)
        self.assertEqual(job['last_input']['state'],'delivered')
        with self.assertRaises(ValueError):self.b.send_input({**args,'text':'changed'})
        self.b.stop(key)
        end=time.monotonic()+5
        while self.b.processes and time.monotonic()<end:time.sleep(.02)
        recovered=bridge.Bridge(self.b.root,'unused',show_terminal=False)
        result=recovered.send_input(args)
        self.assertEqual(result['input_receipt']['state'],'delivered')
        self.assertFalse(recovered.processes)
    def test_secret_redaction_and_web_policy(self):
        self.assertNotIn(self.b.token,self.b.safe('token='+self.b.token))
        self.assertNotIn('a'*30,self.b.safe('Authorization: Bearer '+'a'*30))
        c=http.client.HTTPConnection('127.0.0.1',self.b.port);c.request('GET','/');r=c.getresponse();html=r.read().decode()
        self.assertIn("script-src 'self'",r.headers['Content-Security-Policy']);self.assertNotIn('onclick=',html);c.close()
    def test_storage_failure_blocks_new_execution(self):
        with patch.object(self.b,'atomic',side_effect=OSError('disk full')):
            with self.assertRaises(OSError):self.b.save()
        self.assertTrue(self.b.snapshot()['storage_error'])
        with self.assertRaises(ValueError):self.b.submit({})
        with self.assertRaises(ValueError):self.b.send_input({})
    def test_legacy_state_migration_and_invalid_identity(self):
        ident='b'*32
        self.b.atomic(self.b.root/'tasks.json',{'projects':[],'jobs':[dict(id=ident,state='failed',interactive=True,detail='Claude Code 退出且未返回成功结果（3221225786）')]})
        recovered=bridge.Bridge(self.b.root,'unused',show_terminal=False)
        self.assertEqual(recovered.jobs[ident]['state'],'stopped')
        self.assertEqual(json.loads((self.b.root/'tasks.json').read_text(encoding='utf-8'))['schema_version'],2)
        self.b.atomic(self.b.root/'tasks.json',{'projects':[],'jobs':[dict(id='../escape',state='done')]})
        with self.assertRaises(ValueError):bridge.Bridge(self.b.root,'unused',show_terminal=False)


if __name__=='__main__':unittest.main(verbosity=2)
