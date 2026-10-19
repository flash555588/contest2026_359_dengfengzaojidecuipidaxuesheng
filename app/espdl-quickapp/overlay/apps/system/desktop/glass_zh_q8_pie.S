/* SPDX-License-Identifier: Apache-2.0 */
/* Exact int16 dot. Both pointers 16-byte aligned, n a positive multiple of 8.
 * Caller bounds the sum to signed int32, including the 15-bit post layer. */
    .section .text.glass_zh_q8_dot_pie,"ax",@progbits
    .option arch, +xespv
    .align 2
    .global glass_zh_q8_dot_pie
    .type glass_zh_q8_dot_pie,@function
glass_zh_q8_dot_pie:
    esp.zero.xacc
1:
    esp.vld.128.ip q0, a0, 16
    esp.vld.128.ip q1, a1, 16
    esp.vmulas.s16.xacc q0, q1
    addi a2, a2, -8
    bgtz a2, 1b
    li a2, 0
    esp.srs.s.xacc a0, a2
    ret
    .size glass_zh_q8_dot_pie,.-glass_zh_q8_dot_pie
