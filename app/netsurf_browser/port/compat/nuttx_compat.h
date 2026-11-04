/* SPDX-License-Identifier: MIT */
/* Forced include for NetSurf sources built against NuttX headers.
 *
 * NuttX defines CODE as an empty storage-class macro (<nuttx/compiler.h>);
 * libhubbub and libcss use CODE as an enum member. Include the NuttX headers
 * these sources use first, then release the identifier.
 *
 * With OV_COMPAT_RENAME_DIR (libhubbub only), NuttX's `DIR` typedef from
 * <dirent.h> is declared under a private name so hubbub's DIR element enum
 * compiles; hubbub never uses directory streams. Upstream stays unmodified.
 */
#ifndef OV_NUTTX_COMPAT_H
#define OV_NUTTX_COMPAT_H
#ifdef OV_COMPAT_RENAME_DIR
#define DIR ov_nuttx_DIR
#endif
#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <iconv.h>
#include <locale.h>
#include <signal.h>
#ifdef OV_COMPAT_RENAME_DIR
#undef DIR
#endif
#undef CODE
#endif
