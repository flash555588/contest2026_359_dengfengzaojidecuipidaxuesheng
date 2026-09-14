# SPDX-License-Identifier: Apache-2.0
include $(APPDIR)/Make.defs

PROGNAME = espclaw
PRIORITY = $(CONFIG_SYSTEM_ESPCLAW_PRIORITY)
STACKSIZE = $(CONFIG_SYSTEM_ESPCLAW_STACKSIZE)
MODULE = $(CONFIG_SYSTEM_ESPCLAW)
MAINSRC = main.c
include sources.mk

CFLAGS += -std=gnu11 -I$(CURDIR)/include -I$(CURDIR)/core
CFLAGS += -I$(APPDIR)/netutils/cjson/cJSON
CFLAGS += -I$(TOPDIR)/arch/risc-v/src/esp32p4/esp-hal-3rdparty/components/esp_common/include
# The build owner must supply matching mbedTLS 4 headers and target libraries.
CFLAGS += $(ESPCLAW_TLS_CFLAGS)
port/espclaw_main.c_CFLAGS += -Despclaw_main=claw_command_main

include $(APPDIR)/Application.mk
