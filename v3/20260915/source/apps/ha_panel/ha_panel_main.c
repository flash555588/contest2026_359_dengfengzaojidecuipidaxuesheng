/****************************************************************************
 * apps/ha_panel/ha_panel_main.c
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <nuttx/sched.h>
#include <lvgl/lvgl.h>

#ifdef CONFIG_NETUTILS_NETINIT
#  include <netutils/netinit.h>
#endif

#include "ha_config.h"
#include "ha_entities.h"
#include "ha_client.h"
#include "ha_ui.h"

#ifdef CONFIG_ESP32P4_FUNCTION_EV_BOARD_TOUCHSCREEN
extern int board_touch_initialize(void);
#endif

#ifdef CONFIG_SYSTEM_NSH
extern int nsh_main(int argc, FAR char *argv[]);
#endif

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
extern int esp32p4_lcd_prime_pageflip(void);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ha_config_s g_cfg;
static struct ha_store_s g_store;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void pump_events(void)
{
  struct ha_evt_s evt;

  while (ha_client_poll_evt(&evt) == 0)
    {
      ha_ui_apply_evt(&evt);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  int ret;

  ha_config_init(&g_cfg);
  ha_config_load(&g_cfg);
  ret = ha_config_apply_args(&g_cfg, argc, argv);
  if (ret == -EINTR)
    {
      return 0;
    }

  if (ret < 0)
    {
      return 1;
    }

  if (ha_store_init(&g_store) < 0)
    {
      printf("ha_panel: store init failed\n");
      return 1;
    }

  if (lv_is_initialized())
    {
      printf("ha_panel: LVGL already initialized\n");
      return 1;
    }

#ifdef CONFIG_ESP32P4_FUNCTION_EV_BOARD_TOUCHSCREEN
  if (access("/dev/input0", F_OK) != 0)
    {
      if (board_touch_initialize() < 0)
        {
          printf("ha_panel: touch init failed, UI continues without input\n");
        }
      else
        {
          printf("ha_panel: touch registered on /dev/input0\n");
        }
    }
#endif

  lv_init();
  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/fb0";
#ifdef CONFIG_ESP32P4_FUNCTION_EV_BOARD_TOUCHSCREEN
  info.input_path = (access("/dev/input0", F_OK) == 0) ? "/dev/input0" : NULL;
#else
  info.input_path = NULL;
#endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      printf("ha_panel: display init failed\n");
      lv_deinit();
      return 1;
    }

  if (info.input_path != NULL && result.indev == NULL)
    {
      printf("ha_panel: opened display, but touch indev is NULL\n");
    }

  ha_ui_create(&g_store, &g_cfg);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(result.disp);

#if defined(CONFIG_ESP32P4_SELECTS_REV_LESS_V3) && \
    !defined(CONFIG_LV_NUTTX_FBDEV_PARTIAL)
  /* Publish a complete frame to both LCD pages before input polling. */

  lv_refr_now(result.disp);
  (void)esp32p4_lcd_prime_pageflip();
#endif

  ret = ha_client_start(&g_cfg, &g_store);
  if (ret < 0)
    {
      printf("ha_panel: net worker failed: %d\n", ret);
    }

#ifdef CONFIG_SYSTEM_NSH
  if (task_create("nsh", 100, 4096, nsh_main, NULL) < 0)
    {
      printf("ha_panel: failed to start NSH\n");
#ifdef CONFIG_NETUTILS_NETINIT
      if (netinit_bringup() < 0)
        {
          printf("ha_panel: network initialization failed\n");
        }
#endif
    }
#elif defined(CONFIG_NETUTILS_NETINIT)
  if (netinit_bringup() < 0)
    {
      printf("ha_panel: network initialization failed\n");
    }
#endif

  printf("ha_panel: %s:%u  (token %s, input %s)\n", g_cfg.server,
         (unsigned)g_cfg.port,
         g_cfg.token[0] != '\0' ? "set" : "empty/demo",
         info.input_path != NULL ? info.input_path : "none");

  while (1)
    {
      uint32_t idle;

      pump_events();
      idle = lv_timer_handler();
      idle = idle ? idle : 1;
      usleep(idle * 1000);
    }
}
