"""Copy the exact context and storage interfaces for acceleration review."""
from pathlib import Path
import shutil
import subprocess
import re
ws=Path(__file__).resolve().parent.parent
root=Path('/tmp/v3-desktop-espdl-20260915/nuttx')
out=ws/'diagnostics/espdl-reference/context'
names=['arch/risc-v/src/common/riscv_macros.S','arch/risc-v/src/common/riscv_exception_common.S',
       'arch/risc-v/src/common/riscv_saveusercontext.S','arch/risc-v/src/common/riscv_doirq.c',
       'arch/risc-v/src/common/riscv_internal.h','arch/risc-v/include/irq.h',
       'arch/risc-v/src/esp32p4/Make.defs','arch/risc-v/src/esp32p4/Kconfig']
for name in names:
    p=root/name
    if p.is_file():
        dest=out/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
        print(name,p.stat().st_size)
for pattern in ['save_ctx|load_ctx|riscv_extctx|INTXCPT_EXT|riscv_savecontext|riscv_restorecontext|riscv_dispatch_irq','STORAGE_MTD_OFFSET|mkfs|mksmartfs']:
    lines=[]
    for directory in ['arch/risc-v','boards/risc-v/esp32p4']:
        for p in (root/directory).rglob('*'):
            if not p.is_file() or p.suffix not in ['.c','.S','.h','.defs'] and not p.name.startswith('Kconfig'): continue
            if 'esp-hal-3rdparty' in p.parts: continue
            for i,line in enumerate(p.read_text(errors='replace').splitlines(),1):
                if re.search(pattern,line): lines.append(f'{p.relative_to(root)}:{i}:{line}')
    (out/('context-uses.txt' if pattern.startswith('save') else 'storage-uses.txt')).write_text('\n'.join(lines))
