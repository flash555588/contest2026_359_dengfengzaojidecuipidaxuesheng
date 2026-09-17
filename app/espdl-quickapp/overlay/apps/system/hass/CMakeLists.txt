# SPDX-License-Identifier: Apache-2.0
if(CONFIG_SYSTEM_HASS)
  nuttx_add_application(
    NAME hass
    STACKSIZE 4096
    PRIORITY 100
    SRCS hass_main.c hass_service.c hass_transport.c)
endif()
