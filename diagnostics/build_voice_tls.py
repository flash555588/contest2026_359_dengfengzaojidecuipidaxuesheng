"""Compile the enabled HTTPS backend with firmware size optimization."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path('/tmp/v3-desktop-voice-20260915')
ws = Path(__file__).resolve().parent.parent
tls = root / 'standalone-tls'
build = root / 'tls-build-voice'
toolchain = Path('/home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin')
flags = (f'-Os -march=rv32imac -mabi=ilp32 -D__NuttX__ -Dunix '
         f'-isystem {root}/nuttx/include -ffunction-sections -fdata-sections')
commands = [
    ['cmake', '-S', str(tls), '-B', str(build), '-DCMAKE_SYSTEM_NAME=Generic',
     f'-DCMAKE_C_COMPILER={toolchain}/riscv32-esp-elf-gcc',
     '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY', f'-DCMAKE_C_FLAGS={flags}',
     '-DENABLE_TESTING=OFF', '-DENABLE_PROGRAMS=OFF', '-DMBEDTLS_FATAL_WARNINGS=OFF',
     '-DGEN_FILES=ON', '-DDISABLE_PACKAGE_CONFIG_AND_INSTALL=ON'],
    ['cmake', '--build', str(build), '-j8']]
log = ws / '04-v3-20260913/espclaw-voice/evidence/tls-build.log'
with log.open('w') as f:
    for command in commands:
        p = subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
        if p.returncode:
            f.flush(); print('\n'.join(log.read_text(errors='replace').splitlines()[-40:])); raise SystemExit(p.returncode)
for name in ['library/libmbedtls.a','library/libmbedx509.a','tf-psa-crypto/core/libtfpsacrypto.a']:
    shutil.copyfile(build / name, root / 'tls-build-official' / name)
print('Optimized TLS libraries ready')
