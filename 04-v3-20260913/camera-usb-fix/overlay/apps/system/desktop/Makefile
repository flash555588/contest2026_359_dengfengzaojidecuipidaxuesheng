############################################################################
# apps/system/desktop/Makefile
############################################################################

include $(APPDIR)/Make.defs
ifeq ($(CONFIG_SYSTEM_C6_DESKTOP),y)
CFLAGS += -I$(APPDIR)/system/c6probe
endif


PROGNAME  = $(CONFIG_SYSTEM_DESKTOP_PROGNAME)
PRIORITY  = $(CONFIG_SYSTEM_DESKTOP_PRIORITY)
STACKSIZE = $(CONFIG_SYSTEM_DESKTOP_STACKSIZE)

CFLAGS += -I$(APPDIR)/interpreters/quickjs/quickjs

CSRCS   = desktop_font.c qpk_runtime.c qpk_homeassistant.c ouo_resource.c \
          camera_resource.c homeassistant_resource.c
MAINSRC = desktop_main.c

CSRCS += glass_file_service.c qpk_storage.c qpk_storage_selftest.c

CSRCS += glass_system_monitor.c
CSRCS += glass_system_platform.c

CSRCS += glass_settings_service.c

CSRCS += qpk_tjpgd.c qpk_mjpeg.c qpk_camera_probe.c

include $(APPDIR)/Application.mk
