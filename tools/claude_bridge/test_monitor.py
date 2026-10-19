"""Lifecycle observations never grant permissions or mistake an open TTY for work."""
import http.client
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
import bridge
from session_monitor import hook_settings
from protocol import finish


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.b=bridge.Bridge(Path(self.tmp.name),'unused',show_terminal=False)
        self.ident='a'*32;self.key='b'*48
        self.job=dict(id=self.ident,request_id='monitor-test-001',session_id='session-one',state='running',
                      project=self.tmp.name,prompt='test',text='',events=[],interactive=True,created=time.time(),approval=None)
        self.b.jobs[self.ident]=self.job;self.b.hook_keys[self.ident]=self.key
    def tearDown(self):self.tmp.cleanup()
    def event(self,event,**extra):
        return self.b.observe(self.ident,self.key,dict(session_id='session-one',hook_event_name=event,**extra))
    def test_settings_are_observations_with_separate_permission_hook(self):
        hooks=hook_settings(8787,self.ident,self.key)['hooks']
        self.assertNotIn('SessionStart',hooks)
        self.assertIn('/internal/observe/',hooks['Stop'][0]['hooks'][0]['url'])
        self.assertIn('/internal/permission/',hooks['PermissionRequest'][0]['hooks'][0]['url'])
        self.assertEqual(hooks['Stop'][0]['hooks'][0]['timeout'],2)
    def test_turn_result_survives_screen_and_normal_exit(self):
        self.event('UserPromptSubmit');self.event('PreToolUse',tool_name='Bash')
        self.assertEqual(self.job['activity'],'tool')
        self.assertEqual(self.event('Stop',last_assistant_message='4 checks passed'),{})
        self.b.handle_event(self.job,dict(type='console_screen',text='Resume this session with:'))
        self.b.handle_event(self.job,dict(type='console_exit',code=0));finish(self.job,0,'claude')
        self.assertEqual(self.job['text'],'4 checks passed');self.assertEqual(self.job['state'],'done')
    def test_error_is_not_reported_as_success_when_terminal_exits_zero(self):
        self.event('StopFailure',error='model_not_found',last_assistant_message='model unavailable')
        self.assertEqual(self.b.snapshot(self.ident)['tasks'][-1]['activity'],'error')
        self.b.handle_event(self.job,dict(type='console_screen',text='>'))
        self.b.handle_event(self.job,dict(type='console_exit',code=0));finish(self.job,0,'claude')
        self.assertEqual(self.job['state'],'failed');self.assertEqual(self.job['text'],'model unavailable')
    def test_new_turn_clears_previous_failure(self):
        self.event('StopFailure',error='authentication_failed')
        self.event('UserPromptSubmit');self.assertNotIn('result_state',self.job)
        self.b.handle_event(self.job,dict(type='console_screen',text='Working on next turn'))
        self.assertEqual(self.job['text'],'Working on next turn')
    def test_child_or_stopping_job_cannot_complete_main_turn(self):
        self.event('Stop',agent_id='child-one',last_assistant_message='not main result')
        self.assertNotIn('activity',self.job)
        self.job['state']='stopping';self.event('Stop',last_assistant_message='too late')
        self.assertEqual(self.job['text'],'')
    def test_background_work_and_stale_monitor_are_distinct(self):
        self.event('Stop',background_tasks=[{'id':'background'}])
        self.assertEqual(self.job['activity'],'background')
        self.job['monitor_at']=time.time()-20
        s=self.b.snapshot(self.ident)['task']
        self.assertEqual(s['monitor'],'stale');self.assertEqual(s['state'],'running')
        self.b.handle_event(self.job,dict(type='console_heartbeat',ok=True))
        self.assertEqual(self.b.snapshot(self.ident)['task']['monitor'],'connected')
    def test_auth_session_identity_redaction_and_no_permissions(self):
        with self.assertRaises(PermissionError):self.b.observe(self.ident,'wrong',{})
        with self.assertRaises(PermissionError):self.b.observe(self.ident,self.key,dict(session_id='other'))
        self.event('Stop',last_assistant_message=self.b.token)
        self.assertNotIn(self.b.token,self.job['text']);self.assertIsNone(self.job['approval'])
        with self.assertRaises(ValueError):self.b.permission(self.ident,self.key,{'tool_input':'invalid'})
        self.assertFalse(self.b.pending)
    def test_http_observations_accept_bounded_large_events_only_after_auth(self):
        server=bridge.ThreadingHTTPServer(('127.0.0.1',0),bridge.handler(self.b))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        def post(path,key,body):
            c=http.client.HTTPConnection('127.0.0.1',server.server_address[1])
            c.request('POST',path,json.dumps(body),{'Authorization':'Bearer '+key,'Content-Type':'application/json'})
            r=c.getresponse();r.read();c.close();return r.status
        try:
            body=dict(session_id='session-one',hook_event_name='Stop',last_assistant_message='result '*8000)
            self.assertEqual(post('/internal/observe/'+self.ident,self.b.token,body),403)
            self.assertEqual(post('/internal/observe/'+self.ident,self.key,body),200)
            self.assertLessEqual(len(self.job['text'].encode()),4096)
            self.assertEqual(post('/api/status',self.b.token,body),400)
        finally:server.shutdown();server.server_close()


if __name__=='__main__':unittest.main(verbosity=2)
