"""Install P4 context adaptation and preserve normal saveusercontext behavior."""
from pathlib import Path
import shutil
ws=Path(__file__).resolve().parent.parent
d=ws/'04-v3-20260913/espdl-quickapp'
root=Path('/tmp/v3-desktop-espdl-20260915')
name='nuttx/arch/risc-v/src/common/riscv_saveusercontext.S'
s=(root/name).read_text()
if '#  define P4_EXT_KEEP_ENABLED' not in s:
    s=s.replace('#  include "riscv_extctx.S"','#  define P4_EXT_KEEP_ENABLED 1\n#  include "riscv_extctx.S"')
target=d/'overlay'/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(s)
# ESP-DL leaf assembly temporarily uses 4/8/12-byte stack alignment. Build
# aligned exception frames while preserving the exact interrupted SP. This
# also preserves a fixed extension layout when NuttX copies saved contexts.
name='nuttx/arch/risc-v/src/common/riscv_exception_common.S'
baseline=Path('/tmp/v3-desktop-camera-usb-20260914')
s=(baseline/name).read_text()
needle='  addi       sp, sp, -XCPTCONTEXT_SIZE\n  save_ctx   sp'
assert s.count(needle)==1
s=s.replace(needle,'''#if defined(CONFIG_ARCH_CHIP_ESP32P4) && defined(CONFIG_ARCH_RISCV_INTXCPT_EXTENSIONS)
  /* PIE loads/stores require a 16-byte aligned context, including interrupts
   * inside ESP-DL leaf assembly with a temporarily unaligned stack. Keep t0
   * above the new frame until the original SP has been recorded. */
  addi       sp, sp, -16
  REGSTORE   t0, 0(sp)
  mv         t0, sp
  andi       sp, sp, -16
  addi       sp, sp, -XCPTCONTEXT_SIZE
  addi       t0, t0, 16
  REGSTORE   t0, REG_SP(sp)
  REGLOAD    t0, -16(t0)
#else
  addi       sp, sp, -XCPTCONTEXT_SIZE
#endif
  save_ctx   sp
  .cfi_def_cfa sp, 0''')
needle='  addi       s3, sp, XCPTCONTEXT_SIZE'
assert s.count(needle)==1
s=s.replace(needle,'''#  if defined(CONFIG_ARCH_CHIP_ESP32P4) && defined(CONFIG_ARCH_RISCV_INTXCPT_EXTENSIONS)
  REGLOAD    s3, REG_SP(sp)
#  else
  addi       s3, sp, XCPTCONTEXT_SIZE
#  endif''')
# Non-lazy FPU registers live in each exception frame, not in persistent TCB
# storage. Capture them at every entry, including FS=CLEAN; a new stack frame
# contains no previously saved FP state. Restore at every exit so IRQ C code
# cannot change the interrupted task's FCSR or registers.
s=s.replace('  save_extctx sp\n#endif', '''  save_extctx sp
#endif
  .cfi_def_cfa sp, 0
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && defined(CONFIG_ARCH_FPU) && !defined(CONFIG_ARCH_LAZYFPU)
  addi       a1, sp, INT_XCPT_SIZE
  riscv_savefpu a1
#endif''')
s=s.replace('  load_ctx   sp\n', '''#if defined(CONFIG_ARCH_CHIP_ESP32P4) && defined(CONFIG_ARCH_FPU) && !defined(CONFIG_ARCH_LAZYFPU)
  addi       a1, sp, INT_XCPT_SIZE
  riscv_loadfpu a1
#endif
  load_ctx   sp
''')
(d/'overlay'/name).write_text(s)
name='nuttx/arch/risc-v/src/common/riscv_fpu.S'
s=(baseline/name).read_text()
for symbol in ['riscv_savefpu','riscv_restorefpu']:
    needle=symbol+':\n'
    assert s.count(needle)==1
    s=s.replace(needle,needle+'''
#if defined(CONFIG_ARCH_CHIP_ESP32P4) && !defined(CONFIG_ARCH_LAZYFPU)
  /* P4 eagerly saves/restores the complete FP frame at exception entry/exit.
   * Scheduler hooks must not overwrite it with the handler's working state. */
  ret
#endif
''')
(d/'overlay'/name).write_text(s)
out=ws/'diagnostics/espdl-reference/context'
for tree in ['nuttx/arch/risc-v/src/common/espressif','nuttx/arch/risc-v/src/esp32p4',
             'nuttx/boards/risc-v/esp32p4/common/src']:
    for p in (root/tree).glob('*.c'):
        txt=p.read_text(errors='replace')
        if any(x in txt for x in ['riscv_savecontext','riscv_restorecontext','riscv_savefpu','mksmartfs']):
            dest=out/p.relative_to(root);dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(p,dest)
            print(p.relative_to(root))
print('Normal saveusercontext preserves enabled coprocessors')
