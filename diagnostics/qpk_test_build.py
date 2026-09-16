"""Build the firmware's real QuickJS for host integration tests, with sanitizers."""
from pathlib import Path
import fcntl
import hashlib
import subprocess


def qpk_test_inputs(build, overlay):
    quickjs = build / 'apps/interpreters/quickjs/quickjs'
    flags = ['-D_GNU_SOURCE', '-DCONFIG_BIGNUM', '-DCONFIG_VERSION="qpk-host-test"',
             '-fwrapv', '-fno-strict-aliasing', '-I' + str(quickjs)]
    digest = hashlib.sha256('\n'.join(flags).encode())
    for path in sorted(quickjs.glob('*')):
        if path.suffix in ('.c', '.h'):
            digest.update(path.name.encode()); digest.update(path.read_bytes())
    cache = Path('/tmp/p4-qpk-host-cache') / digest.hexdigest()[:16]
    cache.mkdir(parents=True, exist_ok=True)
    library = cache / 'libquickjs.a'
    with (cache / 'build.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not library.exists():
            objects = []
            for name in ('quickjs.c', 'libregexp.c', 'libunicode.c', 'cutils.c', 'libbf.c'):
                obj = cache / (name + '.o')
                subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer', '-pthread', *flags,
                                '-c', str(quickjs / name), '-o', str(obj)], check=True)
                objects.append(str(obj))
            subprocess.run(['ar', 'rcs', str(library), *objects], check=True)
    sources = [overlay / 'desktop/glass_qpk_builder.c', overlay / 'desktop/qpk_storage.c', overlay / 'desktop/qpk_espdl_sha256.c',
               overlay / 'espclaw/port/espclaw_qpk.c', overlay / 'espclaw/port/claw_stream.c']
    return flags, sources, library
