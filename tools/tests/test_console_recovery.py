"""A dropped prompt/terminator must never cause a control command replay."""
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from music_device_console import Console

class RecoveryTest(unittest.TestCase):
    def test_partial_prompt_with_terminal_clear(self):
        console=Console.__new__(Console);console.log=io.StringIO()
        class Serial:
            def write(self,data):pass
            def flush(self):pass
        console.serial=Serial();console.collect=lambda seconds:'sh> \x1b[K'
        with patch('music_device_console.time.sleep',lambda seconds:None):
            self.assertEqual(console._command_once('desktop speech stop',.3),'sh> \x1b[K')
        self.assertNotIn('RECOVERY',console.log.getvalue())

    def test_cr_only_recovery(self):
        writes=[];clock=[0.0]
        class Serial:
            def write(self,data):writes.append(data)
            def flush(self):pass
        console=Console.__new__(Console);console.serial=Serial();console.log=io.StringIO()
        def collect(seconds):
            clock[0]+=seconds
            return 'nsh> ' if writes and writes[-1]==b'\r' else ''
        console.collect=collect
        def sleep(seconds):clock[0]+=seconds
        with patch('music_device_console.time.monotonic',lambda:clock[0]),patch('music_device_console.time.sleep',sleep):
            self.assertIn('nsh>',console._command_once('desktop speech stop',.3))
        self.assertEqual(writes,[b'\x15',b'desktop speech stop\r',b'\r'])
        self.assertIn('no command replay',console.log.getvalue())

if __name__=='__main__':unittest.main()
