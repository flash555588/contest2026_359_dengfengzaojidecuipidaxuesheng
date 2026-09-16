"""Copy the active scalar FPU/signal/scheduler interfaces for crash analysis."""
from pathlib import Path
import shutil
root=Path('/tmp/v3-desktop-espdl-20260915/nuttx')
dest=Path(__file__).resolve().parent/'espdl-reference/fpu'
names=['arch/risc-v/src/common/riscv_fpu.S','arch/risc-v/src/common/riscv_initialstate.c',
       'arch/risc-v/src/common/riscv_sigdeliver.c','arch/risc-v/src/common/riscv_schedulesigaction.c',
       'arch/risc-v/src/common/espressif/esp_smp.c','arch/risc-v/src/common/riscv_switchcontext.c',
       'arch/risc-v/src/common/riscv_percpu.h']
for name in names:
    src=root/name
    if src.exists():
        dst=dest/src.name;dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(src,dst);print(name)
for src in (root/'arch/risc-v/src/common').glob('*fpu*'):
    shutil.copyfile(src,dest/src.name)
