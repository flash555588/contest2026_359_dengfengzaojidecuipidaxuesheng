#!/usr/bin/env python3
"""Test the production file classifier against the previous linear algorithm."""
import os,subprocess,tempfile
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='file-types-') as tmp:
 exe=Path(tmp)/'test'
 subprocess.run(['cc','-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(repo/'app/espdl-quickapp/overlay/apps/system/desktop'),str(repo/'app/espdl-quickapp/tests/test_file_types.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
