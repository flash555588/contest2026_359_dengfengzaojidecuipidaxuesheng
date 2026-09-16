/****************************************************************************
 * arch/risc-v/src/common/espressif/platform_include/sys/reent.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub so Espressif HAL headers that include <sys/reent.h> compile on NuttX.
 * NuttX does not use newlib's reentrancy struct; CONFIG_LIBC_NEWLIB is off.
 *
 ****************************************************************************/

#ifndef __ARCH_RISCV_SRC_COMMON_ESPRESSIF_PLATFORM_INCLUDE_SYS_REENT_H
#define __ARCH_RISCV_SRC_COMMON_ESPRESSIF_PLATFORM_INCLUDE_SYS_REENT_H

struct _reent;

#ifndef _REENT
#  define _REENT ((struct _reent *)0)
#endif

#ifndef _GLOBAL_REENT
#  define _GLOBAL_REENT _REENT
#endif

#endif /* __ARCH_RISCV_SRC_COMMON_ESPRESSIF_PLATFORM_INCLUDE_SYS_REENT_H */
