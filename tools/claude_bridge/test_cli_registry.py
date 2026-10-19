import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import bridge
import cli_registry


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.b = bridge.Bridge(self.root / 'state', 'fake-claude', show_terminal=False)

    def tearDown(self):
        for key in list(self.b.processes): self.b.stop(key)
        self.wait(lambda: not self.b.processes)
        self.tmp.cleanup()

    def wait(self, predicate):
        end = time.monotonic() + 10
        while not predicate() and time.monotonic() < end: time.sleep(.02)
        self.assertTrue(predicate())

    def test_text_input_and_stop(self):
        script = self.root / 'echo.py'
        script.write_text('import sys\nfor line in sys.stdin:\n print("echo:"+line.strip(),flush=True)\n', encoding='utf-8')
        config = {'echo': {'name': 'Any CLI', 'command': [sys.executable, str(script)], 'input': True}}
        (self.b.root / 'clis.json').write_text(json.dumps(config))
        self.b.clis = cli_registry.discover('fake-claude', self.b.root)
        snapshot = self.b.submit(dict(cli='echo', project=str(self.root), prompt='第一条', request_id='generic-0001'))
        key = snapshot['task']['id']; job = self.b.jobs[key]
        self.wait(lambda: '第一条' in job['text'])
        self.b.send_input(dict(id=key, text='second',request_id='generic-input-0001'))
        self.wait(lambda: 'second' in job['text'])
        self.b.stop(key); self.wait(lambda: job['state'] == 'stopped')

    def test_codex_json_resume(self):
        cli = {'name': 'Codex', 'command': [sys.executable,str(self.root/'codex.py')], 'protocol': 'codex', 'resume': True}
        self.b.clis['codex'] = cli
        script = self.root / 'codex.py'
        script.write_text('import json,sys\np=sys.stdin.read()\nfor e in [{"type":"thread.started","thread_id":"session-fixture"},{"type":"item.completed","item":{"type":"agent_message","text":p}},{"type":"turn.completed"}]: print(json.dumps(e),flush=True)\n')
        first = self.b.submit(dict(cli='codex', project=str(self.root), prompt='--option stays data', request_id='codex-0001'))['task']['id']
        self.wait(lambda: first not in self.b.processes and self.b.jobs[first]['state'] == 'done')
        self.assertEqual(self.b.jobs[first]['text'], '--option stays data')
        second = self.b.submit(dict(cli='codex', project=str(self.root), prompt='continue', request_id='codex-0002', continue_id=first))['task']['id']
        self.wait(lambda: self.b.jobs[second]['state'] == 'done')
        command = cli_registry.codex_command(cli, self.b.jobs[second])
        self.assertEqual(command[-3:], ['resume', 'session-fixture', '-'])
        with self.assertRaises(ValueError): self.b.submit(dict(cli='claude', project=str(self.root), prompt='x', request_id='wrong-cli-001', continue_id=second))

    def test_unknown_cli_and_shell_shim_rejected(self):
        with self.assertRaises(ValueError): self.b.submit(dict(cli='missing', project=str(self.root), prompt='x', request_id='unknown-0001'))
        shim = self.root / 'bad.cmd'; shim.write_text('echo bad')
        (self.b.root / 'clis.json').write_text(json.dumps({'bad': {'command': [str(shim)]}}))
        with self.assertRaises(ValueError): cli_registry.discover('fake-claude', self.b.root)


if __name__ == '__main__': unittest.main(verbosity=2)
