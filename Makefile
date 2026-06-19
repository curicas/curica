# Makefile for Curica JS Runtime and Bundler

CURICA_VERSION := $(shell cat version)
CC := ./cosmocc/bin/cosmocc
ifeq ($(filter verbose,$(MAKECMDGOALS)),verbose)
    WARNING_FLAGS := -Wall -Wextra
    THIRD_PARTY_WARNINGS := 
else
    WARNING_FLAGS := -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-format-truncation -Wno-unused-variable -Wno-misleading-indentation -Wno-unused-function -Wno-sign-compare
    THIRD_PARTY_WARNINGS := -w
endif

CFLAGS ?= -g -O0 $(WARNING_FLAGS) -std=gnu99 -Isrc -Ithird_party/mbedtls/include -Ithird_party/sqlite -Ithird_party/wamr/core/iwasm/include -DMBEDTLS_ALLOW_PRIVATE_ACCESS -DCURICA_VERSION=\"$(CURICA_VERSION)\"
SRCS := src/alloc.c src/compiler.c src/vm.c src/builtins.c src/slre.c src/event_loop.c src/napi.c src/fs_module.c src/vfs_module.c src/thread_pool.c src/net_module.c src/dgram_module.c src/zlib_module.c src/os_module.c src/crypto_module.c src/child_process_module.c src/worker_module.c src/http_module.c src/websocket_module.c src/sqlite_module.c src/wasm_module.c src/wasi_module.c src/ts_stripper.c src/formatter.c src/repl.c src/main.c src/kv_store.c src/worker_threads_module.c src/webview_module.c src/ffi_module.c src/atomics.c
JS_SRCS := $(wildcard src/js/*.js)
BUILD_DIR := build
OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))

MBEDTLS_DIR := third_party/mbedtls
MBEDTLS_SRCS := $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBEDTLS_OBJS := $(patsubst $(MBEDTLS_DIR)/library/%.c,$(BUILD_DIR)/mbedtls_%.o,$(MBEDTLS_SRCS))

SQLITE_DIR := third_party/sqlite
SQLITE_SRCS := $(SQLITE_DIR)/sqlite3.c
SQLITE_OBJS := $(BUILD_DIR)/sqlite3.o

WAMR_LIB := build/libwamr.a

TARGET := $(BUILD_DIR)/curica
VM_BASE := $(BUILD_DIR)/vm_base

.PHONY: all clean verbose

all: $(TARGET) $(VM_BASE)

verbose: all

./cosmocc/bin/cosmocc:
	./tools/download_deps.sh

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/scripts.h: $(JS_SRCS) | $(BUILD_DIR)
	@rm -f $@
	@if [ -n "$(JS_SRCS)" ]; then \
		for f in $(JS_SRCS); do \
			xxd -i $$f >> $@; \
		done \
	else \
		touch $@; \
	fi

$(TARGET): $(OBJS) $(MBEDTLS_OBJS) $(SQLITE_OBJS) $(WAMR_LIB) | $(BUILD_DIR)
	@echo "Linking $@"
	@$(CC) $(CFLAGS) -Wl,--export-dynamic -o $@ $(OBJS) $(MBEDTLS_OBJS) $(SQLITE_OBJS) $(WAMR_LIB) -lm -lpthread
	@cp $@ $(BUILD_DIR)/vm_base
	

$(VM_BASE): $(TARGET)
	@cp $(TARGET) $(VM_BASE)

$(BUILD_DIR)/builtins.o: src/builtins.c $(BUILD_DIR)/scripts.h | $(BUILD_DIR)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) -I$(BUILD_DIR) -c -o $@ $<

$(BUILD_DIR)/mbedtls_%.o: $(MBEDTLS_DIR)/library/%.c | $(BUILD_DIR)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(THIRD_PARTY_WARNINGS) -c -o $@ $<

$(BUILD_DIR)/sqlite3.o: $(SQLITE_SRCS) | $(BUILD_DIR)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(THIRD_PARTY_WARNINGS) -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -c -o $@ $<

$(WAMR_LIB):
	@WARNING_FLAGS="$(THIRD_PARTY_WARNINGS)" ./tools/build_wamr_static.sh

$(BUILD_DIR)/%.o: src/%.c ./cosmocc/bin/cosmocc | $(BUILD_DIR)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
	rm -rf third_party/wamr/product-mini/platforms/linux/build
	rm -f *.aarch64.elf *.com.dbg *.cbc
