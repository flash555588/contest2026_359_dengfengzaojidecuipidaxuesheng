/* SPDX-License-Identifier: Apache-2.0 */
/* No C calls with live test FP registers: only interrupts and task switching
 * may touch them. Preserve the caller's ABI registers and rounding mode. */
.section .text.qpk_dl_fpu_roundtrip,"ax",@progbits
.global qpk_dl_fpu_roundtrip
.type qpk_dl_fpu_roundtrip,@function
.balign 4
qpk_dl_fpu_roundtrip:
  addi sp,sp,-144
  .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
  fsw f\n,(4*\n)(sp)
  flw f\n,(4*\n)(a0)
  .endr
  frcsr t0
  sw t0,128(sp)
  lw t0,128(a0)
  fscsr t0
  li t0,20000000
1:
  addi t0,t0,-1
  bnez t0,1b
  .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
  fsw f\n,(4*\n)(a1)
  flw f\n,(4*\n)(sp)
  .endr
  frcsr t0
  sw t0,128(a1)
  lw t0,128(sp)
  fscsr t0
  addi sp,sp,144
  ret
.size qpk_dl_fpu_roundtrip,.-qpk_dl_fpu_roundtrip
