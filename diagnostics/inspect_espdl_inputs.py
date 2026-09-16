"""Inspect the existing NuttX build interfaces needed by ESP-DL."""
from pathlib import Path
import shutil

ws = Path(__file__).resolve().parent.parent
root = Path('/tmp/v3-desktop-camera-usb-20260914')
out = ws / 'diagnostics/espdl-reference/nuttx-interfaces'
names = [
    'nuttx/arch/risc-v/src/common/riscv_internal.h',
    'nuttx/arch/risc-v/src/common/riscv_fpucmp.c',
    'nuttx/arch/risc-v/src/common/riscv_fpu.c',
    'nuttx/arch/risc-v/src/common/riscv_exception.S',
    'nuttx/arch/risc-v/src/common/riscv_arch.h',
    'nuttx/arch/risc-v/src/common/riscv_asm.h',
    'nuttx/arch/risc-v/src/common/riscv_getnewintctx.c',
    'nuttx/arch/risc-v/src/common/riscv_set_idleintctx.c',
    'nuttx/arch/Kconfig', 'nuttx/arch/risc-v/Kconfig',
    'nuttx/arch/risc-v/src/esp32p4/Kconfig', 'nuttx/arch/risc-v/Make.defs',
    'nuttx/arch/risc-v/src/common/Toolchain.defs',
    'nuttx/arch/risc-v/src/common/riscv_savefpu.S',
    'nuttx/arch/risc-v/src/common/riscv_restorefpu.S',
    'nuttx/arch/risc-v/src/common/riscv_initialstate.c',
    'nuttx/arch/risc-v/src/common/riscv_exception_common.S',
    'nuttx/arch/risc-v/src/common/riscv_switchcontext.S',
    'nuttx/arch/risc-v/src/common/riscv_swint.c',
    'nuttx/arch/risc-v/include/irq.h',
    'nuttx/arch/risc-v/src/esp32p4/esp32p4_smp.c',
    'nuttx/arch/risc-v/src/common/espressif/esp_start.c',
    'nuttx/arch/risc-v/src/common/espressif/esp_spiflash.c',
    'nuttx/arch/risc-v/src/common/espressif/esp_spiflash.h',
    'nuttx/libs/libxx/libstdc++/Make.defs',
    'nuttx/libs/libxx/Kconfig', 'nuttx/libs/libcxx/Kconfig', 'nuttx/libs/libcxx/Makefile',
    'nuttx/libs/libxx/Makefile', 'nuttx/arch/risc-v/src/esp32p4/Make.defs',
    'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/scripts/Make.defs',
    'apps/system/desktop/Kconfig', 'apps/system/desktop/Makefile',
    'apps/system/desktop/desktop_main.c', 'apps/system/desktop/camera_resource.h',
    'apps/system/desktop/desktop_font.c', 'nuttx/include/nuttx/lib/lib.h',
    'nuttx/include/nuttx/kmalloc.h', 'nuttx/include/nuttx/mm/mm.h',
]
print('Build exists:', root.exists())
print('Other builds:', ', '.join(path.name for path in Path('/tmp').glob('v3-desktop-*')))
for name in names:
    source = root / name
    if source.is_file():
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        print(name, source.stat().st_size)
    else:
        print('Missing:', name)
print('C++ directories:', [str(path.relative_to(root)) for path in (root / 'nuttx/libs').glob('*xx*')])
print('C++ dependencies:', [str(path.relative_to(root)) for path in (root / 'nuttx/libs/libcxx').glob('*')])
print('Common context files:', [p.name for p in (root / 'nuttx/arch/risc-v/src/common').iterdir()
                                if any(s in p.name for s in ['fpu', 'ctx', 'context', 'asm'])])
print('Python ML tools:')
import importlib.util
for module in ['torch', 'numpy', 'onnx', 'esp_ppq', 'ppq', 'sklearn']:
    print(module, bool(importlib.util.find_spec(module)))
print('Libraries:', [str(p.relative_to(root)) for p in root.rglob('*.a')
                      if not any(part.startswith('tls-') for part in p.relative_to(root).parts)])
