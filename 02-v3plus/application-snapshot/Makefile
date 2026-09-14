############################################################################
# apps/system/desktop/Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_SYSTEM_DESKTOP_PROGNAME)
PRIORITY  = $(CONFIG_SYSTEM_DESKTOP_PRIORITY)
STACKSIZE = $(CONFIG_SYSTEM_DESKTOP_STACKSIZE)

CFLAGS += -I$(APPDIR)/interpreters/quickjs/quickjs

CSRCS   = desktop_font.c qpk_runtime.c qpk_homeassistant.c ouo_resource.c \
          camera_resource.c homeassistant_resource.c pomodoro_resource.c \
          pomodoro_engine.c qpk_pomodoro.c pomodoro_lvgl.c
MAINSRC = desktop_main.c

CSRCS += esphome/esphome_client.c esphome/esphome_model.c \
         esphome/protobuf.c esphome/native_transport.c esphome/esphome_lvgl.c
include esphome/crypto.mk

include $(APPDIR)/Application.mk
