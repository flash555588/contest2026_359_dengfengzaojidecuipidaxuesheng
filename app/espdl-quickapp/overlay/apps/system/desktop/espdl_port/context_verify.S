/* SPDX-License-Identifier: Apache-2.0 */
/* Suspend with all Q registers, QACC and UA state live, then compare in C. */
.section .text.qpk_dl_context_roundtrip,"ax",@progbits
.global qpk_dl_context_roundtrip
.type qpk_dl_context_roundtrip,@function
.balign 4
qpk_dl_context_roundtrip:
  addi sp,sp,-16
  sw ra,12(sp)
  sw a1,8(sp)
  sw a2,4(sp)
  esp.vld.128.ip q0,a0,16
  esp.vld.128.ip q1,a0,16
  esp.vld.128.ip q2,a0,16
  esp.vld.128.ip q3,a0,16
  esp.vld.128.ip q4,a0,16
  esp.vld.128.ip q5,a0,16
  esp.vld.128.ip q6,a0,16
  esp.vld.128.ip q7,a0,16
  esp.ld.qacc.l.l.128.ip a0,16
  esp.ld.qacc.l.h.128.ip a0,16
  esp.ld.qacc.h.l.128.ip a0,16
  esp.ld.qacc.h.h.128.ip a0,16
  esp.ld.ua.state.ip a0,16
  /* Exercise IRQ entry at every word alignment used by vendor leaf kernels.
   * Do not call C until the ABI stack alignment is restored. */
  sub sp,sp,a2
  li t0,1000000
1:
  addi t0,t0,-1
  bnez t0,1b
  add sp,sp,a2
  li a0,1000
  call usleep
  lw a0,8(sp)
  esp.vst.128.ip q0,a0,16
  esp.vst.128.ip q1,a0,16
  esp.vst.128.ip q2,a0,16
  esp.vst.128.ip q3,a0,16
  esp.vst.128.ip q4,a0,16
  esp.vst.128.ip q5,a0,16
  esp.vst.128.ip q6,a0,16
  esp.vst.128.ip q7,a0,16
  esp.st.qacc.l.l.128.ip a0,16
  esp.st.qacc.l.h.128.ip a0,16
  esp.st.qacc.h.l.128.ip a0,16
  esp.st.qacc.h.h.128.ip a0,16
  esp.st.ua.state.ip a0,16
  lw ra,12(sp)
  addi sp,sp,16
  ret
.size qpk_dl_context_roundtrip,.-qpk_dl_context_roundtrip
