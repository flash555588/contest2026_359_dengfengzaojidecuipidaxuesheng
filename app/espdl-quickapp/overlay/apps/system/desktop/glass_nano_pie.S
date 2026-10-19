/* SPDX-License-Identifier: Apache-2.0 */
/* Exact signed int8 dot, 16-byte aligned inputs and len multiple of 16.
 * Length is bounded by the caller so the signed int32 result cannot overflow. */
    .section .text.glass_nano_dot_pie,"ax",@progbits
    .option arch, +xespv
    .align 2
    .global glass_nano_dot_pie
    .type glass_nano_dot_pie,@function
glass_nano_dot_pie:
    esp.zero.xacc
    blez a2, 2f
1:
    esp.vld.128.ip q0, a0, 16
    esp.vld.128.ip q1, a1, 16
    esp.vmulas.s8.xacc q0, q1
    addi a2, a2, -16
    bgtz a2, 1b
2:
    li a2, 0
    esp.srs.s.xacc a0, a2
    ret
    .size glass_nano_dot_pie,.-glass_nano_dot_pie
