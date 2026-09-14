"""Build the isolated desktop and record output in the shared workspace."""

from pathlib import Path
import os
import subprocess
import sys
import shutil
from prepare_tls import prepare_tls

root = Path(os.environ.get('V3_BUILD_ROOT', '/tmp/v3-desktop-black-screen-20260914'))
workspace = Path(__file__).resolve().parent.parent
toolchain = Path('/home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin')
hal = root / 'nuttx/arch/risc-v/src/esp32p4/esp-hal-3rdparty'
tls = prepare_tls(root, workspace / 'diagnostics/downloads')
env = os.environ.copy()
env['PATH'] = f'{toolchain}:/home/streetartist/.local/bin:{env["PATH"]}'
flags = (f'-I{tls}/include -I{tls}/tf-psa-crypto/include '
         f'-I{tls}/tf-psa-crypto/drivers/builtin/include')
log = Path(os.environ.get('V3_BUILD_LOG', str(workspace / 'diagnostics/build-latest.log')))
overlay = Path(os.environ.get('V3_BUILD_OVERLAY', str(workspace / '04-v3-20260913/black-screen-fix/overlay')))
for source in overlay.rglob('*'):
    if source.is_file() and '__pycache__' not in source.parts:
        target = root / source.relative_to(overlay)
        if not target.exists() or target.read_bytes() != source.read_bytes():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
# The export already contains patched dependencies.  Its directory mtimes
# must be newer than the bundled downloads, or Make unpacks them again.
for dependency in ['apps/graphics/lvgl/lvgl',
                   'apps/wireless/bluetooth/nimble/mynewt-nimble',
                   'apps/netutils/cjson/cJSON',
                   'apps/interpreters/quickjs/quickjs']:
    directory = root / dependency
    if directory.is_dir():
        directory.touch()
print(f'Building in {root}; log: {log}', flush=True)
with log.open('w') as output:
    tls_build = root / 'tls-build-official'
    if not (tls_build / 'library/libmbedtls.a').exists():
        tls_flags = (f'-march=rv32imac -mabi=ilp32 -D__NuttX__ -Dunix '
                     f'-isystem {root}/nuttx/include -ffunction-sections -fdata-sections')
        commands = [
            ['cmake', '-S', str(tls), '-B', str(tls_build),
             '-DCMAKE_SYSTEM_NAME=Generic',
             f'-DCMAKE_C_COMPILER={toolchain}/riscv32-esp-elf-gcc',
             '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
             f'-DCMAKE_C_FLAGS={tls_flags}', '-DENABLE_TESTING=OFF',
             '-DENABLE_PROGRAMS=OFF', '-DMBEDTLS_FATAL_WARNINGS=OFF',
             '-DGEN_FILES=ON', '-DDISABLE_PACKAGE_CONFIG_AND_INSTALL=ON'],
            ['cmake', '--build', str(tls_build), '-j8'],
        ]
        for command in commands:
            result = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT)
            if result.returncode:
                output.flush()
                print('\n'.join(log.read_text(errors='replace').splitlines()[-50:]))
                raise SystemExit(result.returncode)
    libraries = [tls_build / 'library/libmbedtls.a',
                 tls_build / 'library/libmbedx509.a',
                 tls_build / 'tf-psa-crypto/core/libtfpsacrypto.a']
    # A command-line EXTRA_LIBS overrides Make's +=, including libgcc.
    # Espressif's GCC 14 soft-float runtime accesses FCSR (frrm), which
    # traps with CONFIG_ARCH_FPU disabled. Use the ABI-compatible xPack
    # RV32IMAC/ILP32 runtime, whose conversions use integer instructions.
    libraries.append(Path(subprocess.check_output(
        ['/home/streetartist/toolchains/riscv-none-elf-gcc/bin/riscv-none-elf-gcc', '-march=rv32imac',
         '-mabi=ilp32', '--print-libgcc-file-name'], text=True).strip()))
    result = subprocess.run(['make', '-j8', f'CROSSDEV={toolchain}/riscv32-esp-elf-',
                             f'ESPCLAW_TLS_CFLAGS={flags}',
                             'EXTRA_LIBS=' + ' '.join(map(str, libraries)), *sys.argv[1:]],
                            cwd=root / 'nuttx', env=env,
                            stdout=output, stderr=subprocess.STDOUT)
print('\n'.join(log.read_text(errors='replace').splitlines()[-65:]))
raise SystemExit(result.returncode)
