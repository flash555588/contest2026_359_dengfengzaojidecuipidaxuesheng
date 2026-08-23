/****************************************************************************
 * boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_appinit.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <nuttx/board.h>

#include "esp32p4-function-ev-board.h"

#if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
extern int ets_printf(const char *fmt, ...);
#  define app_progress(s) ets_printf(s)
#else
#  define app_progress(s)
#endif

#ifdef CONFIG_BOARDCTL

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Perform application specific initialization.  This function is never
 *   called directly from application code, but only indirectly via the
 *   (non-standard) boardctl() interface using the command BOARDIOC_INIT.
 *
 * Input Parameters:
 *   arg - Passed through from boardctl() without interpretation.  Zero/NULL
 *         is the default configuration.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int board_app_initialize(uintptr_t arg)
{
  int ret;

  app_progress("A0\n");
#ifdef CONFIG_BOARD_LATE_INITIALIZE
  /* Board initialization already performed by board_late_initialize() */

  ret = OK;
#else
  ret = esp_bringup();
#endif

  app_progress("A1\n");

#if defined(CONFIG_ESPRESSIF_SIMPLE_BOOT) && \
    !defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3)
  /* Probe the NuttX upper-half console independently from the ROM UART.
   * O_NONBLOCK guarantees that a broken TX interrupt cannot stall board
   * bring-up while the diagnostic result is still printed by the ROM path.
   */

  {
    static const char probe[] = "NSH console probe\r\n";
    int fd;
    int errcode;
    ssize_t nwritten = -1;

    errno = 0;
    fd = open("/dev/console", O_WRONLY | O_NONBLOCK);
    errcode = errno;
    app_progress("C0\n");

    if (fd >= 0)
      {
        errno = 0;
        nwritten = write(fd, probe, sizeof(probe) - 1);
        errcode = errno;
        close(fd);
      }

    ets_printf("C1 fd=%d wr=%ld errno=%d\n", fd,
               (long)nwritten, errcode);
  }
#endif

  return ret;
}

#endif /* CONFIG_BOARDCTL */
