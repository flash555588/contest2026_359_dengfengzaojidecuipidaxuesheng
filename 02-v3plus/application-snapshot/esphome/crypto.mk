# SPDX-License-Identifier: MIT
# Include from the desktop Makefile before $(APPDIR)/Application.mk.
# Paths are relative to that Makefile, not to this fragment.
ESPHOME_CRYPTO_DIR ?= esphome
ESPHOME_NOISE_DIR := $(ESPHOME_CRYPTO_DIR)/third_party/noise-c
ESPHOME_NOISE_SRCS := \
  src/protocol/cipherstate.c \
  src/protocol/dhstate.c \
  src/protocol/handshakestate.c \
  src/protocol/hashstate.c \
  src/protocol/names.c \
  src/protocol/patterns.c \
  src/protocol/symmetricstate.c \
  src/protocol/util.c \
  src/backend/ref/cipher-chachapoly.c \
  src/backend/ref/dh-curve25519.c \
  src/backend/ref/hash-sha256.c \
  src/crypto/chacha/chacha.c \
  src/crypto/donna/poly1305-donna.c \
  src/crypto/sha2/sha256.c \
  src/crypto/x25519/x25519.c

ESPHOME_CRYPTO_SRCS := $(ESPHOME_CRYPTO_DIR)/secure_transport.c \
  $(ESPHOME_CRYPTO_DIR)/third_party/noise_port.c \
  $(addprefix $(ESPHOME_NOISE_DIR)/,$(ESPHOME_NOISE_SRCS))

ESPHOME_CRYPTO_CFLAGS := \
  -I$(ESPHOME_NOISE_DIR)/include -I$(ESPHOME_NOISE_DIR)/src \
  -I$(ESPHOME_CRYPTO_DIR)/third_party \
  -DNOISE_USE_REFERENCE_BACKEND=1 -DNOISE_USE_LIBSODIUM=0 \
  -DNOISE_USE_OPENSSL=0 -DNOISE_USE_CUSTOM_RAND=1 \
  -DNOISE_USE_REFERENCE_DONNA_CURVE25519=0 \
  -DNOISE_USE_REFERENCE_STROBE_CURVE25519=1 -DNOISE_USE_PTHREAD=0

CSRCS += $(ESPHOME_CRYPTO_SRCS)
CFLAGS += $(ESPHOME_CRYPTO_CFLAGS)
