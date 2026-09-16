"""Create an isolated ESP-DL build; never mutate the flashed camera baseline."""
from pathlib import Path
import os
import re
import shutil
import subprocess

ws = Path(__file__).resolve().parent.parent
old = Path('/tmp/v3-desktop-camera-usb-20260914')
root = Path('/tmp/v3-desktop-espdl-20260915')
delivery = ws / '04-v3-20260913/espdl-quickapp'
overlay = delivery / 'overlay'
fresh = not root.exists()
if fresh:
    subprocess.run(['cp', '-a', str(old), str(root)], check=True)
    for path in root.rglob('*'):
        if path.is_symlink():
            link = str(path.readlink())
            match = re.match(r'/tmp/v3-desktop-[^/]+/(.*)', link)
            if match:
                destination = root / match.group(1)
                assert Path(os.path.abspath(destination)).is_relative_to(root)
                path.unlink()
                path.symlink_to(os.path.relpath(destination, path.parent))
    for path in root.rglob('Kconfig'):
        if path.is_file() and path.resolve().is_relative_to(root):
            path.write_text(path.read_text().replace(str(old) + '/', str(root) + '/'))
    # All target objects must be rebuilt when changing ILP32 to ILP32F.
    # Preserve downloaded dependencies, source, and the original build tree.
    removed = 0
    for path in root.rglob('*'):
        if path.is_symlink() or not path.is_file():
            continue
        if path.suffix in ['.o', '.a'] or path.name in ['Make.dep', '.depend', '.built']:
            assert path.resolve().is_relative_to(root)
            path.unlink()
            removed += 1
    print('Invalidated target objects and dependencies:', removed, flush=True)

(delivery / 'evidence').mkdir(parents=True, exist_ok=True)
if not overlay.exists():
    shutil.copytree(ws / '04-v3-20260913/camera-usb-fix/overlay', overlay)

for name in ['nuttx/arch/risc-v/Kconfig',
             'nuttx/arch/risc-v/src/common/espressif/esp_start.c',
             'nuttx/arch/risc-v/src/esp32p4/esp32p4_smp.c']:
    target = overlay / name
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(old / name, target)

path = overlay / 'nuttx/arch/risc-v/Kconfig'
text = path.read_text()
begin = text.index('config ARCH_CHIP_ESP32P4\n')
end = text.index('\nconfig ', begin + 1)
block = text[begin:end]
if 'select ARCH_HAVE_FPU' not in block:
    block = block.replace('\tselect ARCH_RV32\n', '\tselect ARCH_RV32\n\tselect ARCH_HAVE_FPU\n')
    text = text[:begin] + block + text[end:]
    path.write_text(text)

config = (root / 'nuttx/.config').read_text()
config = re.sub(r'^(?:# CONFIG_HAVE_CXX is not set|CONFIG_HAVE_CXX=.*)\n', '', config, flags=re.M)
config = re.sub(r'^(?:# CONFIG_HAVE_CXXINITIALIZE is not set|CONFIG_HAVE_CXXINITIALIZE=.*)\n', '', config, flags=re.M)
options = {'CONFIG_ARCH_FPU': 'y', 'CONFIG_ARCH_LAZYFPU': 'n',
           'CONFIG_HAVE_CXX': 'y', 'CONFIG_HAVE_CXXINITIALIZE': 'y',
           'CONFIG_LIBCXXTOOLCHAIN': 'y', 'CONFIG_LIBSUPCXX_TOOLCHAIN': 'y',
           'CONFIG_LIBCXXNONE': 'n', 'CONFIG_LIBMINIABI': 'n',
           'CONFIG_CXX_STANDARD': '"gnu++20"',
           'CONFIG_TLS_NELEM': '8',
           'CONFIG_ESPRESSIF_STORAGE_MTD_OFFSET': '0xc00000',
           'CONFIG_ESPRESSIF_STORAGE_MTD_SIZE': '0x400000',
           'CONFIG_ARCH_RISCV_INTXCPT_EXTENSIONS': 'y',
           'CONFIG_ARCH_RISCV_INTXCPT_EXTREGS': '74',
           # Keep the clock right after the Wi-Fi link comes up: HTTPS needs a
           # valid wall clock and the desktop shows the date once it is set.
           'CONFIG_NETUTILS_NTPCLIENT': 'y',
           # DNS/socket calls plus the P4 extended exception context exceed
           # the default 2 KiB. The captured panic overwrote NTP argv below
           # its 1896-byte usable stack (evidence/panic-ntp-45.txt).
           'CONFIG_NETUTILS_NTPCLIENT_STACKSIZE': '8192',
           'CONFIG_STACK_COLORATION': 'y',
           # Standalone TF-PSA-Crypto uses /dev/urandom. On P4 this is the
           # same hardware RNG driver as /dev/random, not a software PRNG.
           'CONFIG_DEV_URANDOM': 'y',
           'CONFIG_NETUTILS_NTPCLIENT_STAY_ON': 'y',
           'CONFIG_NETUTILS_NTPCLIENT_POLLDELAYSEC': '60',
           'CONFIG_NETUTILS_NTPCLIENT_SERVER':
               '"ntp.aliyun.com;ntp.tencent.com;cn.pool.ntp.org"',
           'CONFIG_CXX_EXCEPTION': 'n', 'CONFIG_CXX_RTTI': 'n'}
for key, value in options.items():
    line = f'# {key} is not set' if value == 'n' else f'{key}={value}'
    pattern = rf'^(?:# {key} is not set|{key}=.*)$'
    if re.search(pattern, config, re.M):
        config = re.sub(pattern, line, config, flags=re.M)
    else:
        config += '\n' + line
(root / 'nuttx/.config').write_text(config.rstrip() + '\n')
for source in overlay.rglob('*'):
    if source.is_file() and source.name != '.config':
        target = root / source.relative_to(overlay)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != source.read_bytes():
            shutil.copyfile(source, target)

for path in root.rglob('Kconfig*'):
    if path.is_file() and path.resolve().is_relative_to(root):
        data = path.read_bytes()
        if b'\r\n' in data:
            path.write_bytes(data.replace(b'\r\n', b'\n'))

env = os.environ.copy()
env['PATH'] = '/home/streetartist/.local/bin:' + env['PATH']
env['PATH'] = '/home/streetartist/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/bin:' + env['PATH']
log = delivery / 'evidence/prepare.log'
with log.open('w') as out:
    ret = subprocess.run(['make', 'olddefconfig', 'context'], cwd=root / 'nuttx', env=env,
                         stdout=out, stderr=subprocess.STDOUT)
if ret.returncode:
    print('\n'.join(log.read_text(errors='replace').splitlines()[-40:]))
    raise SystemExit(ret.returncode)
shutil.copyfile(root / 'nuttx/.config', delivery / 'resolved.config')
shutil.copyfile(root / 'nuttx/.config', overlay / 'nuttx/.config')

# Read the exact FPU implementation used by this tree for the port review.
for name in ['nuttx/arch/risc-v/src/common/riscv_fpu.S',
             'nuttx/arch/risc-v/src/common/riscv_dispatch_irq.c',
             'nuttx/arch/risc-v/src/common/riscv_initialize.c',
             'nuttx/arch/risc-v/include/asm.h']:
    source = root / name
    if source.is_file():
        target = ws / 'diagnostics/espdl-reference/nuttx-interfaces' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
print('ESP-DL workspace ready:', root)
assert 'CONFIG_HAVE_CXX=y' in (root / 'nuttx/.config').read_text()
for name in ['nuttx/boards/risc-v/esp32p4/common/scripts/esp32p4_sections.ld',
             'nuttx/boards/risc-v/esp32p4/common/scripts/esp32p4_sections.rev3.ld']:
    target=overlay/name
    if not target.exists():
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(root/name,target)
    text=target.read_text()
    if '_sinit' not in text:
        text=text.replace('__init_priority_array_start = ABSOLUTE(.);',
                          '_sinit = ABSOLUTE(.);\n __init_priority_array_start = ABSOLUTE(.);')
        text=text.replace('__init_array_end = ABSOLUTE(.);',
                          '__init_array_end = ABSOLUTE(.);\n _einit = ABSOLUTE(.);')
        text=text.replace('KEEP (*(EXCLUDE_FILE (*crtend.* *crtbegin.*) .init_array.*))',
                          'KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))')
        target.write_text(text)
