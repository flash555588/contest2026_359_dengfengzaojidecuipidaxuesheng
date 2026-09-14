ROOT ?= .
ESPHOME_CRYPTO_DIR := $(ROOT)
include $(ROOT)/crypto.mk
OUT ?= $(ROOT)/tests/build
CORE := $(ROOT)/native_transport.c $(ROOT)/protobuf.c $(ROOT)/esphome_model.c $(ROOT)/esphome_client.c
CFLAGS += -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -I$(ROOT) -g
.PHONY: check
.PHONY: p4-crypto
p4-crypto:
	$(CC) $(ESPHOME_CRYPTO_CFLAGS) -fsyntax-only -Wall -Wextra -Wno-unused-parameter $(P4FLAGS) $(ESPHOME_CRYPTO_SRCS)

check: $(OUT)/transport $(OUT)/model $(OUT)/libesphome.so $(OUT)/client
	python3 $(ROOT)/generate_protocol.py --check
	$(OUT)/transport
	$(OUT)/model
	timeout 15 $(OUT)/client

$(OUT):
	mkdir -p "$@"
$(OUT)/transport: $(ROOT)/native_transport.c $(ROOT)/test_transport.c | $(OUT)
	$(CC) $(CFLAGS) $^ -o "$@"
$(OUT)/model: $(ROOT)/protobuf.c $(ROOT)/esphome_model.c $(ROOT)/tests/test_model.c | $(OUT)
	$(CC) $(CFLAGS) $^ -lm -o "$@"
$(OUT)/libesphome.so: $(CORE) $(ESPHOME_CRYPTO_SRCS) | $(OUT)
	$(CC) $(CFLAGS) -shared -fPIC $^ -pthread -lm -Wl,--no-undefined -o "$@"
$(OUT)/client: $(ROOT)/tests/test_client.c $(OUT)/libesphome.so
	$(CC) $(CFLAGS) $< -L$(OUT) -lesphome -Wl,-rpath,$(OUT) -pthread -o "$@"
