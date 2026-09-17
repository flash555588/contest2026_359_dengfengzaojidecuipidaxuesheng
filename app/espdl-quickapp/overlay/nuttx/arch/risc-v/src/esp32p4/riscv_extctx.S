/* SPDX-License-Identifier: Apache-2.0 */
/* P4 PIE and hardware-loop context for NuttX exception/saveusercontext hooks.
 * Q0-Q7: 128 bytes; QACC:64; UA:16; XACC:8; SAR/SAR_BYTES/FFT/CFG:16;
 * PIE state:4; loop CSRs:24; HWLP state:4. The buffer is 16-byte aligned.
 * Only a1/a2 are scratch, so this also works with an a0 context pointer. */
#define P4_EXT_BASE (((REG_INT_CTX_NDX + 1) * 4 + 15) & ~15)
#if INT_XCPT_SIZE < P4_EXT_BASE + 264
#  error "P4 extended context allocation is too small"
#endif
#if (XCPTCONTEXT_SIZE & 15) != 0
#  error "P4 exception context must preserve 16-byte stack alignment"
#endif

.option arch, +xespv
.macro save_extctx in
  addi a1, \in, P4_EXT_BASE
  csrr a2, 0x7f2
  sw a2, 232(a1)
  andi a2, a2, 3
  beqz a2, .Lpie_saved\@
  esp.vst.128.ip q0, a1, 16
  esp.vst.128.ip q1, a1, 16
  esp.vst.128.ip q2, a1, 16
  esp.vst.128.ip q3, a1, 16
  esp.vst.128.ip q4, a1, 16
  esp.vst.128.ip q5, a1, 16
  esp.vst.128.ip q6, a1, 16
  esp.vst.128.ip q7, a1, 16
  esp.st.qacc.l.l.128.ip a1, 16
  esp.st.qacc.l.h.128.ip a1, 16
  esp.st.qacc.h.l.128.ip a1, 16
  esp.st.qacc.h.h.128.ip a1, 16
  esp.st.ua.state.ip a1, 16
  esp.st.u.xacc.ip a1, 8
  esp.movx.r.sar a2
  sw a2, 0(a1)
  esp.movx.r.sar.bytes a2
  sw a2, 4(a1)
  esp.movx.r.fft.bit.width a2
  sw a2, 8(a1)
  esp.movx.r.cfg a2
  sw a2, 12(a1)
.Lpie_saved\@:
  /* Disable PIE in handlers; accidental vector use traps instead of silently
   * corrupting the interrupted task. All C/ROM IRQ code is scalar. */
#ifndef P4_EXT_KEEP_ENABLED
  csrwi 0x7f2, 0
#endif
  addi a1, \in, P4_EXT_BASE
  csrr a2, 0x7f1
  sw a2, 260(a1)
  andi a2, a2, 3
  beqz a2, .Lhwlp_saved\@
  csrr a2, 0x7c6
  sw a2, 236(a1)
  csrr a2, 0x7c7
  sw a2, 240(a1)
  csrr a2, 0x7c8
  sw a2, 244(a1)
  csrr a2, 0x7c9
  sw a2, 248(a1)
  csrr a2, 0x7ca
  sw a2, 252(a1)
  csrr a2, 0x7cb
  sw a2, 256(a1)
.Lhwlp_saved\@:
#ifndef P4_EXT_KEEP_ENABLED
  csrwi 0x7f1, 0
#endif
.endm

.macro load_extctx out
  addi a1, \out, P4_EXT_BASE
  lw a2, 232(a1)
  andi a2, a2, 3
  beqz a2, .Lpie_off\@
  csrwi 0x7f2, 1
  esp.vld.128.ip q0, a1, 16
  esp.vld.128.ip q1, a1, 16
  esp.vld.128.ip q2, a1, 16
  esp.vld.128.ip q3, a1, 16
  esp.vld.128.ip q4, a1, 16
  esp.vld.128.ip q5, a1, 16
  esp.vld.128.ip q6, a1, 16
  esp.vld.128.ip q7, a1, 16
  esp.ld.qacc.l.l.128.ip a1, 16
  esp.ld.qacc.l.h.128.ip a1, 16
  esp.ld.qacc.h.l.128.ip a1, 16
  esp.ld.qacc.h.h.128.ip a1, 16
  esp.ld.ua.state.ip a1, 16
  esp.ld.xacc.ip a1, 8
  lw a2, 0(a1)
  esp.movx.w.sar a2
  lw a2, 4(a1)
  esp.movx.w.sar.bytes a2
  lw a2, 8(a1)
  esp.movx.w.fft.bit.width a2
  lw a2, 12(a1)
  esp.movx.w.cfg a2
  li a2, 3
.Lpie_off\@:
  csrw 0x7f2, a2
  addi a1, \out, P4_EXT_BASE
  lw a2, 260(a1)
  andi a2, a2, 3
  beqz a2, .Lhwlp_off\@
  csrwi 0x7f1, 1
  lw a2, 236(a1)
  csrw 0x7c6, a2
  lw a2, 240(a1)
  csrw 0x7c7, a2
  lw a2, 248(a1)
  csrw 0x7c9, a2
  lw a2, 252(a1)
  csrw 0x7ca, a2
  lw a2, 244(a1)
  csrw 0x7c8, a2
  lw a2, 256(a1)
  csrw 0x7cb, a2
  li a2, 3
.Lhwlp_off\@:
  csrw 0x7f1, a2
.endm
