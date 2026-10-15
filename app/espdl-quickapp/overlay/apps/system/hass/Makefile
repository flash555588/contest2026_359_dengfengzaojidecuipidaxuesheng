# SPDX-License-Identifier: Apache-2.0
include $(APPDIR)/Make.defs
PROGNAME = hass
PRIORITY = 100
STACKSIZE = 4096
MODULE = $(CONFIG_SYSTEM_HASS)
CSRCS = hass_service.c hass_transport.c
CFLAGS += -I$(APPDIR)/netutils/cjson/cJSON
MAINSRC = hass_main.c
include $(APPDIR)/Application.mk
