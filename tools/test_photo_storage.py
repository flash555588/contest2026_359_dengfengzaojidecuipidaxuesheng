#!/usr/bin/env python3
"""Check production photo storage selection against real directory fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();repo=Path(__file__).resolve().parents[1]
out=a.output.resolve();(out/'nuttx').mkdir(parents=True,exist_ok=True)
(out/'sys').mkdir(exist_ok=True)
(out/'nuttx/config.h').write_text('#pragma once\n')
(out/'sys/statfs.h').write_text('''#pragma once
#include <stdint.h>
#define MSDOS_SUPER_MAGIC 0x4d44
#define FATFS_SUPER_MAGIC 0x4006
#define SMARTFS_MAGIC 0x4d53
struct statfs {unsigned f_type;uint64_t f_bavail;unsigned f_bsize;};
int statfs(const char *,struct statfs *);
''')
with tempfile.TemporaryDirectory(prefix='photo-store-') as fixture:
    exe=out/'photo-storage'
    flags=['-std=gnu11','-D_GNU_SOURCE','-D__NuttX__','-Wall','-Wextra','-Werror',
           '-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
           '-DTEST_ROOT="'+fixture+'"','-I'+str(out),
           '-I'+str(repo/'app/espdl-quickapp/overlay/apps/system/desktop')]
    subprocess.run(['gcc',*flags,str(repo/'app/espdl-quickapp/tests/photo_storage_host.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
