/****************************************************************************
 * arch/risc-v/src/common/espressif/platform_include/sys/cdefs.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal sys/cdefs.h so Espressif HAL headers compile against NuttX libc
 * without pulling in newlib.
 *
 ****************************************************************************/

#ifndef __ARCH_RISCV_SRC_COMMON_ESPRESSIF_PLATFORM_INCLUDE_SYS_CDEFS_H
#define __ARCH_RISCV_SRC_COMMON_ESPRESSIF_PLATFORM_INCLUDE_SYS_CDEFS_H

#include <stddef.h>

#ifdef __cplusplus
#  define __BEGIN_DECLS  extern "C" {
#  define __END_DECLS    }
#else
#  define __BEGIN_DECLS
#  define __END_DECLS
#endif

#ifndef __unused
#  define __unused __attribute__((__unused__))
#endif

#ifndef __packed
#  define __packed __attribute__((__packed__))
#endif

#ifndef __always_inline
#  define __always_inline inline __attribute__((__always_inline__))
#endif

#ifndef __containerof
#  define __containerof(ptr, type, member) \
     ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#endif /* __ARCH_RISCV_SRC_COMMON_ESPRESSIF_PLATFORM_INCLUDE_SYS_CDEFS_H */
