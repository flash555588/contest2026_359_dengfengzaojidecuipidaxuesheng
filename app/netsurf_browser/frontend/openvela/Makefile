#
# Makefile for the NetSurf openvela frontend
#
# Produces a relocatable ELF (native/module.elf) for openvela's generic
# native module loader. Driven by app/netsurf_browser/build.py, which
# supplies the NuttX/LVGL/QuickJS compile flags in OV_FIRMWARE_CFLAGS and
# the module link flags in OV_MODULE_LDFLAGS.
#

CFLAGS += -std=gnu99 -Dnsopenvela \
	  -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	  -D_BSD_SOURCE -D_DEFAULT_SOURCE

CFLAGS += $(OV_FIRMWARE_CFLAGS)

# relocatable partial link into one module object
LDFLAGS += $(OV_MODULE_LDFLAGS)

# Messages are embedded by build.py (Chinese with English fallback)
MESSAGES_FILTER=any
MESSAGES_TARGET=$(OBJROOT)/messages
MESSAGES_LANGUAGES=

S_FRONTEND := gui.c window.c plot.c layout.c bitmap.c fetch.c http.c \
	image.c schedule.c resources.c res_data.c

SOURCES = $(S_COMMON) $(S_IMAGE) $(S_BROWSER) $(S_FRONTEND)
EXETARGET := module.elf

install-openvela:

package-openvela:
