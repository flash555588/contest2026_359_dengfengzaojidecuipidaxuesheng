# SPDX-License-Identifier: Apache-2.0
include $(APPDIR)/Make.defs
CSRCS = live_platform.c bili_api.c bili_flv.c bili_sps.c bili_http.c bili_live.c bili_av.c
CSRCS += local_video_media.c local_video_ui.c
CSRCS += $(filter-out $(wildcard local_vendor/helix/sbr*.c),$(wildcard local_vendor/helix/*.c))
CXXEXT = .cpp
CXXSRCS = bili_decoder.cpp
H264 = vendor/openh264/codec
COMMON = $(wildcard $(H264)/common/src/*.cpp)
CXXSRCS += $(COMMON) $(wildcard $(H264)/decoder/core/src/*.cpp) \
           $(wildcard $(H264)/decoder/plus/src/*.cpp)
INCS = -I$(CURDIR)/local_vendor -I$(CURDIR)/local_vendor/helix -I$(CURDIR) -I$(CURDIR)/$(H264)/api/wels \
       -I$(CURDIR)/$(H264)/common/inc -I$(CURDIR)/$(H264)/decoder/core/inc \
       -I$(CURDIR)/$(H264)/decoder/plus/inc \
       -I$(APPDIR)/netutils/cjson/cJSON \
       -I$(APPDIR)/system/desktop
CFLAGS += -DUSE_DEFAULT_STDLIB $(ESPCLAW_TLS_CFLAGS) $(INCS) -std=gnu11 -O3
CXXFLAGS += $(INCS) -std=gnu++11 -fno-exceptions -fno-rtti -O3
# No x86/ARM assembly on P4 RISC-V. Decoder-only, no encoder/processing.
context::
	@test -f $(CURDIR)/vendor/openh264/codec/api/wels/codec_api.h || \
          (echo "Run tools/install_bilibili_live.py --apps $(APPDIR)"; false)
include $(APPDIR)/Application.mk
