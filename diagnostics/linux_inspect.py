"""Read build prerequisites and existing symbol-file locations."""

from pathlib import Path
import subprocess

root = Path('/tmp/v3-desktop-black-screen-20260914')
for name in ['nuttx/Make.defs', 'nuttx/arch/risc-v/src/esp32p4/Make.defs',
             'nuttx/arch/risc-v/src/esp32p4/Toolchain.defs',
             'joint-tls-2056361b9a5e45c9/CMakeCache.txt']:
    path = root / name
    if path.exists():
        print(f'FILE {name}')
        for index, line in enumerate(path.read_text().splitlines(), 1):
            if any(key in line for key in ['CROSSDEV', 'flash/', 'TLS', 'LIB', 'TOOLCHAIN', 'C_COMPILER']):
                print(f'{index}: {line}')
for name in ['claw-tls-source', 'claw-tls-source-bundled', 'joint-tls-2056361b9a5e45c9']:
    path = root / name
    print(f'{name}:', list(path.glob('*'))[:20])
for base in ['/home/streetartist/openvela-p4-reproduce-v3',
             '/home/streetartist/openvela-p4-reproduce',
             '/home/streetartist/.local', '/home/streetartist/.venvs']:
    path = Path(base)
    if path.is_dir():
        for pattern in ['nuttx', 'nuttx.elf', 'kconfiglib.py', 'esptool.py']:
            for match in path.rglob(pattern):
                if match.is_file():
                    print(f'EXISTING {match}')
subprocess.run(['/home/streetartist/toolchains/riscv-none-elf-gcc/bin/riscv-none-elf-gcc', '--version'], check=True)
print('TLS roots:', [str(p) for p in root.glob('*')])
