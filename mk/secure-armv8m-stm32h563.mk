TOOLPREFIX ?= arm-none-eabi-
CC := $(TOOLPREFIX)gcc
OBJCOPY := $(TOOLPREFIX)objcopy
SIZE := $(TOOLPREFIX)size

ROOT := .
PORT_DIR := $(ROOT)/port/stm32h563
WOLFHSM_RUNNER_DIR := $(ROOT)/src/services/wolfhsm/runner
WOLFHSM_DIR := $(ROOT)/lib/wolfHSM
WOLFSSL_DIR := $(ROOT)/lib/wolfSSL
WOLFHAL_DIR := $(ROOT)/lib/wolfhal
WOLFCOSE_DIR := $(ROOT)/lib/wolfCOSE

BUILD_DIR ?= build
MANIFEST_INPUT := $(PORT_DIR)/manifest.json
MANIFEST_DIR := $(BUILD_DIR)/manifest
MANIFEST_STAMP := $(MANIFEST_DIR)/.stamp
MANIFEST_GEN_C := $(MANIFEST_DIR)/wolftrust_manifest_generated.c
MANIFEST_GEN_H := $(MANIFEST_DIR)/wolftrust_manifest_generated.h
SECURE_ELF := $(BUILD_DIR)/wolftrust.elf
SECURE_BIN := $(BUILD_DIR)/wolftrust.bin
SECURE_CMSE_IMPLIB := $(BUILD_DIR)/secure_cmse_implib.o
BUILD_MODE_STAMP := $(BUILD_DIR)/secure_build_mode.stamp
WOLFHSM_CFG_H := $(BUILD_DIR)/wolfhsm_cfg.h

PORT_HEADERS := $(wildcard $(PORT_DIR)/*.h)

CPU_FLAGS := -mcpu=cortex-m33 -mthumb -mgeneral-regs-only
WT_TIMESLICE_MS ?= 2
WT_MAX_GUESTS ?= 2
WT_CO_STACK_SIZE ?= 24576
WT_SHARED_UART ?= 3
WT_GUEST_CORE_CLOCK_HZ ?= 240000000
WT_GUEST_UART_CLOCK_HZ ?= 120000000
WT_WOLFCRYPT_SP_ASM ?= 1
WT_WOLFCRYPT_ARMASM ?= 1
WT_WOLFCRYPT_STM32_HASH ?= 0
WT_ENGINE_HSM ?= 1
WT_ATTEST_COSE ?= 1

# wolfHSM resumes SHA-256 operations from the portable digest and length
# fields carried by its wire protocol. STM32 HASH uses opaque peripheral CSR
# state instead, so enabling it here would remove the wolfHSM SHA handler and
# make a client fallback operate on a partially modified context.
ifneq ($(WT_WOLFCRYPT_STM32_HASH),0)
$(error WT_WOLFCRYPT_STM32_HASH is incompatible with the wolfHSM SHA service)
endif

# Secure runtime placement. The default preserves the standalone image;
# the wolfBoot handoff build relocates it to 0x0C020000.
WT_SECURE_FLASH_BASE ?= 0x0C000000
WT_SECURE_FLASH_SIZE ?= 0x00020000
WT_SECURE_IMAGE_HEADER_SIZE ?= 0
WT_GUEST0_FLASH_BASE ?= 0x08020000
WT_GUEST1_FLASH_BASE ?= 0x08040000

# Virtual-Ethernet (VNET) subsystem. Off until Wave 2 lands a working
# core. Host-side unit tests under tests/host/vnet/ build regardless;
# this switch only gates linking the dataplane and NSC veneers into
# the secure image.
CONFIG_VNET ?= n
WT_VNET_POOL_SLOTS ?= 8
WT_VNET_FRAME_MAX ?= 1536
WT_VNET_RX_QUEUE_DEPTH ?= 8
WT_VNET_RX_IRQ ?= 130
WT_VNET_TIMEOUT_TICKS ?= 500
WT_VNET_UNKNOWN_UCAST_FLOOD ?= 0

HSM_INCLUDES := -I$(WOLFHSM_DIR) -I$(WOLFSSL_DIR) -I$(BUILD_DIR)
HSM_INCLUDES_SECURE := $(HSM_INCLUDES) -I$(WOLFHAL_DIR) -I$(abspath $(WOLFHSM_RUNNER_DIR))
HSM_DEFS_SECURE := -DWOLFSSL_USER_SETTINGS -DWOLFHSM_CFG \
    -DWOLF_CRYPTO_CB -UNO_CODING \
    -DWC_RESEED_INTERVAL=1000000 -DWT_ENGINE_HSM=$(WT_ENGINE_HSM)

ifeq ($(WT_ATTEST_COSE),1)
SECURE_CFLAGS_COSE := -I$(WOLFCOSE_DIR)/include \
    -DWT_ATTEST_COSE=1 \
    -DWOLFCOSE_LEAN -DWOLFCOSE_ENABLE_EXT_SIGN \
    -DWOLFCOSE_NO_SIGN1_VERIFY -DWOLFCOSE_NO_ENCRYPT0 \
    -DWOLFCOSE_NO_MAC0 -DWOLFCOSE_NO_KEY_ENCODE \
    -DWOLFCOSE_NO_KEY_DECODE
endif

ifeq ($(WT_WOLFCRYPT_SP_ASM),1)
HSM_DEFS_SECURE += -DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM \
    -DWOLFSSL_ARM_ARCH=8
endif
ifeq ($(WT_WOLFCRYPT_ARMASM),1)
HSM_DEFS_SECURE += -DWOLFSSL_ARMASM -DWOLFSSL_ARMASM_NO_HW_CRYPTO \
    -DWOLFSSL_ARMASM_INLINE -DWOLFSSL_ARMASM_NO_NEON \
    -DWOLFSSL_ARMASM_THUMB2
endif
SECURE_CFLAGS := $(CPU_FLAGS) -ffreestanding -fno-builtin -nostdlib -Os -g \
    -ffunction-sections -fdata-sections -Wall -Wextra \
    -I$(ROOT)/include -I$(PORT_DIR) \
    -DWT_TIMESLICE_MS=$(WT_TIMESLICE_MS) \
    -DWT_MAX_GUESTS=$(WT_MAX_GUESTS) \
    -DWT_CO_STACK_SIZE=$(WT_CO_STACK_SIZE) \
    -DWT_SHARED_UART=$(WT_SHARED_UART) \
    -DWT_GUEST_CORE_CLOCK_HZ=$(WT_GUEST_CORE_CLOCK_HZ) \
    -DWT_GUEST_UART_CLOCK_HZ=$(WT_GUEST_UART_CLOCK_HZ) \
    -DWT_SECURE_FLASH_BASE=$(WT_SECURE_FLASH_BASE) \
    -DWT_SECURE_FLASH_SIZE=$(WT_SECURE_FLASH_SIZE) \
    -DWT_SECURE_IMAGE_HEADER_SIZE=$(WT_SECURE_IMAGE_HEADER_SIZE) \
    -DWT_GUEST0_FLASH_BASE=$(WT_GUEST0_FLASH_BASE) \
    -DWT_GUEST1_FLASH_BASE=$(WT_GUEST1_FLASH_BASE) \
    -DWHAL_CFG_STM32H5_RNG_DIRECT_API_MAPPING \
    -mcmse \
    $(HSM_INCLUDES_SECURE) $(HSM_DEFS_SECURE) $(SECURE_CFLAGS_COSE) \
    -I$(MANIFEST_DIR)

ifeq ($(CONFIG_VNET),y)
SECURE_CFLAGS += -DCONFIG_VNET=1 \
    -DWT_VNET_POOL_SLOTS=$(WT_VNET_POOL_SLOTS) \
    -DWT_VNET_FRAME_MAX=$(WT_VNET_FRAME_MAX) \
    -DWT_VNET_RX_QUEUE_DEPTH=$(WT_VNET_RX_QUEUE_DEPTH) \
    -DWT_VNET_RX_IRQ=$(WT_VNET_RX_IRQ) \
    -DWT_VNET_TIMEOUT_TICKS=$(WT_VNET_TIMEOUT_TICKS) \
    -DWT_VNET_UNKNOWN_UCAST_FLOOD=$(WT_VNET_UNKNOWN_UCAST_FLOOD)
endif

HSM_LIB_CFLAGS := $(SECURE_CFLAGS) \
    -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter \
    -Wno-type-limits
HSM_WOLFHSM_CFLAGS := $(HSM_LIB_CFLAGS)

SECURE_SRCS := \
    $(WOLFHSM_RUNNER_DIR)/ivt.c \
    $(WOLFHSM_RUNNER_DIR)/runtime.c \
    $(PORT_DIR)/platform_stm32h563.c \
    $(ROOT)/src/domain.c \
    $(ROOT)/src/ffm.c \
    $(ROOT)/src/ffm_api.c \
    $(ROOT)/src/ffm_boot.c \
    $(ROOT)/src/ipc.c \
    $(ROOT)/src/lifecycle.c \
    $(ROOT)/src/manifest.c \
    $(ROOT)/src/monitor.c \
    $(ROOT)/src/spm.c \
    $(PORT_DIR)/partitions.c

WOLFHSM_SECURE_SRCS := \
    $(WOLFHSM_DIR)/src/wh_client.c \
    $(WOLFHSM_DIR)/src/wh_comm.c \
    $(WOLFHSM_DIR)/src/wh_message_comm.c \
    $(WOLFHSM_DIR)/src/wh_message_crypto.c \
    $(WOLFHSM_DIR)/src/wh_message_keystore.c \
    $(WOLFHSM_DIR)/src/wh_message_nvm.c \
    $(WOLFHSM_DIR)/src/wh_message_customcb.c \
    $(WOLFHSM_DIR)/src/wh_message_counter.c \
    $(WOLFHSM_DIR)/src/wh_nvm.c \
    $(WOLFHSM_DIR)/src/wh_nvm_flash.c \
    $(WOLFHSM_DIR)/src/wh_flash_unit.c \
    $(WOLFHSM_DIR)/src/wh_server.c \
    $(WOLFHSM_DIR)/src/wh_server_crypto.c \
    $(WOLFHSM_DIR)/src/wh_server_keystore.c \
    $(WOLFHSM_DIR)/src/wh_server_nvm.c \
    $(WOLFHSM_DIR)/src/wh_server_customcb.c \
    $(WOLFHSM_DIR)/src/wh_server_counter.c \
    $(WOLFHSM_DIR)/src/wh_transport_mem.c \
    $(WOLFHSM_DIR)/src/wh_lock.c \
    $(WOLFHSM_DIR)/src/wh_utils.c \
    $(WOLFHSM_DIR)/src/wh_crypto.c \
    $(WOLFHSM_DIR)/src/wh_keyid.c \
    $(WOLFHSM_DIR)/src/wh_log.c

WOLFCRYPT_SECURE_SRCS := \
    $(WOLFSSL_DIR)/wolfcrypt/src/aes.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/asn.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/coding.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/cryptocb.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/ecc.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/error.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/hash.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/hmac.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/logging.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/random.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/sha256.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/sp_cortexm.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/sp_int.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/wolfmath.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/wc_port.c

ifeq ($(WT_WOLFCRYPT_ARMASM),1)
WOLFCRYPT_SECURE_SRCS += \
    $(WOLFSSL_DIR)/wolfcrypt/src/port/arm/thumb2-aes-asm_c.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/port/arm/thumb2-sha256-asm_c.c
endif

WT_SECURE_EXTRA_SRCS := \
    $(ROOT)/src/arch/armv8m/cmse.c \
    $(ROOT)/src/arch/armv8m/coroutine_armv8m.c \
    $(ROOT)/src/sched/coroutine.c \
    $(ROOT)/src/sync/mutex.c \
    $(wildcard $(PORT_DIR)/rng_entropy.c) \
    $(wildcard $(PORT_DIR)/hsm_flash.c) \
    $(WOLFHAL_DIR)/src/reg.c \
    $(WOLFHAL_DIR)/src/rng/stm32h5_rng.c \
    $(wildcard $(WOLFHSM_RUNNER_DIR)/libc_stubs.c) \
    $(wildcard $(ROOT)/src/services/wolfhsm/*.c) \
    $(ROOT)/src/services/boot_handoff.c \
    $(wildcard $(ROOT)/src/arch/armv8m/cmse_transport.c)

ifeq ($(WT_ATTEST_COSE),1)
WT_SECURE_EXTRA_SRCS += \
    $(ROOT)/src/services/attestation_cose.c \
    $(ROOT)/src/services/initial_attestation.c \
    $(WOLFCOSE_DIR)/src/wolfcose.c \
    $(WOLFCOSE_DIR)/src/wolfcose_cbor.c
endif

ifeq ($(CONFIG_VNET),y)
WT_SECURE_EXTRA_SRCS += \
    $(ROOT)/src/vnet/vnet_mac.c    \
    $(ROOT)/src/vnet/vnet_pool.c   \
    $(ROOT)/src/vnet/vnet_ring.c   \
    $(ROOT)/src/vnet/vnet_fdb.c    \
    $(ROOT)/src/vnet/vnet_switch.c \
    $(ROOT)/src/services/vnet/vnet_service.c
endif

HSM_SECURE_BASE_OBJS := $(patsubst %.c,$(BUILD_DIR)/sec_%.o,$(notdir $(SECURE_SRCS)))
HSM_WOLFHSM_SEC_OBJS := $(patsubst %.c,$(BUILD_DIR)/wh_sec_%.o,$(notdir $(WOLFHSM_SECURE_SRCS)))
HSM_WOLFCRYPT_SEC_OBJS := $(patsubst %.c,$(BUILD_DIR)/wc_sec_%.o,$(notdir $(WOLFCRYPT_SECURE_SRCS)))
HSM_WT_EXTRA_OBJS := $(patsubst %.c,$(BUILD_DIR)/wt_sec_%.o,$(notdir $(WT_SECURE_EXTRA_SRCS)))
MANIFEST_OBJ := $(BUILD_DIR)/wt_sec_wolftrust_manifest_generated.o

ALL_SECURE_OBJS := \
    $(HSM_SECURE_BASE_OBJS) \
    $(HSM_WOLFHSM_SEC_OBJS) \
    $(HSM_WOLFCRYPT_SEC_OBJS) \
    $(HSM_WT_EXTRA_OBJS) \
    $(MANIFEST_OBJ)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(MANIFEST_DIR):
	@mkdir -p $@

$(MANIFEST_STAMP): $(ROOT)/tools/manifest/generate.py $(MANIFEST_INPUT) | $(MANIFEST_DIR)
	python3 $(ROOT)/tools/manifest/generate.py $(MANIFEST_INPUT) \
		$(MANIFEST_DIR) --supported-features 0x1 --address-bits 32
	touch $@

$(MANIFEST_GEN_C) $(MANIFEST_GEN_H): $(MANIFEST_STAMP)

$(MANIFEST_OBJ): $(MANIFEST_GEN_C) $(MANIFEST_GEN_H) \
		$(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $(MANIFEST_GEN_C)

$(WOLFHSM_CFG_H): | $(BUILD_DIR)
	printf '#include "%s"\n' "$(abspath $(WOLFHSM_RUNNER_DIR)/wh_settings_local.h)" > $@

.PHONY: FORCE
FORCE:

$(BUILD_MODE_STAMP): FORCE | $(BUILD_DIR)
	@tmp="$@.tmp"; \
	printf '%s\n' \
		'ARCH=$(ARCH)' \
		'TARGET=$(TARGET)' \
		'WT_SECURE_FLASH_BASE=$(WT_SECURE_FLASH_BASE)' \
		'WT_SECURE_FLASH_SIZE=$(WT_SECURE_FLASH_SIZE)' \
		'WT_SECURE_IMAGE_HEADER_SIZE=$(WT_SECURE_IMAGE_HEADER_SIZE)' \
		'WT_GUEST0_FLASH_BASE=$(WT_GUEST0_FLASH_BASE)' \
		'WT_GUEST1_FLASH_BASE=$(WT_GUEST1_FLASH_BASE)' \
		'WT_ENGINE_HSM=$(WT_ENGINE_HSM)' \
		'WT_ATTEST_COSE=$(WT_ATTEST_COSE)' \
		'WT_MAX_GUESTS=$(WT_MAX_GUESTS)' \
		'WT_CO_STACK_SIZE=$(WT_CO_STACK_SIZE)' \
		'WT_WOLFCRYPT_SP_ASM=$(WT_WOLFCRYPT_SP_ASM)' \
		'WT_WOLFCRYPT_ARMASM=$(WT_WOLFCRYPT_ARMASM)' \
		'WT_WOLFCRYPT_STM32_HASH=$(WT_WOLFCRYPT_STM32_HASH)' \
		'WT_SHARED_UART=$(WT_SHARED_UART)' \
		'WT_TIMESLICE_MS=$(WT_TIMESLICE_MS)' \
		'WT_GUEST_CORE_CLOCK_HZ=$(WT_GUEST_CORE_CLOCK_HZ)' \
		'WT_GUEST_UART_CLOCK_HZ=$(WT_GUEST_UART_CLOCK_HZ)' > "$$tmp"; \
	if test -f "$@" && cmp -s "$$tmp" "$@"; then \
		rm -f "$$tmp"; \
	else \
		mv "$$tmp" "$@"; \
	fi

$(BUILD_DIR)/wh_sec_%.o: $(WOLFHSM_DIR)/src/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(HSM_WOLFHSM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wc_sec_%.o: $(WOLFSSL_DIR)/wolfcrypt/src/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(HSM_LIB_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wc_sec_%.o: $(WOLFSSL_DIR)/wolfcrypt/src/port/arm/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(HSM_LIB_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wc_sec_%.o: $(WOLFSSL_DIR)/wolfcrypt/src/port/st/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(HSM_LIB_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(ROOT)/src/arch/armv8m/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(ROOT)/src/sched/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(ROOT)/src/sync/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(WOLFHSM_RUNNER_DIR)/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(PORT_DIR)/%.c $(PORT_HEADERS) $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(WOLFHAL_DIR)/src/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(WOLFHAL_DIR)/src/rng/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(ROOT)/src/services/wolfhsm/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(ROOT)/src/vnet/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(ROOT)/src/services/vnet/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(ROOT)/src/services/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: $(WOLFCOSE_DIR)/src/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sec_monitor.o: $(ROOT)/src/monitor.c $(MANIFEST_GEN_H) \
		$(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sec_ffm_boot.o: $(ROOT)/src/ffm_boot.c $(MANIFEST_GEN_H) \
		$(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sec_%.o: $(WOLFHSM_RUNNER_DIR)/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sec_%.o: $(PORT_DIR)/%.c $(PORT_HEADERS) $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sec_%.o: $(ROOT)/src/%.c $(WOLFHSM_CFG_H) $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(SECURE_ELF) $(SECURE_CMSE_IMPLIB) &: $(ALL_SECURE_OBJS) $(WOLFHSM_RUNNER_DIR)/secure.ld $(BUILD_MODE_STAMP) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) \
		-Wl,--defsym=WT_SECURE_FLASH_ORIGIN=$(WT_SECURE_FLASH_BASE) \
		-Wl,--defsym=WT_SECURE_FLASH_SIZE=$(WT_SECURE_FLASH_SIZE) \
		-Wl,--defsym=WT_SECURE_IMAGE_HEADER_SIZE=$(WT_SECURE_IMAGE_HEADER_SIZE) \
		-Wl,-T$(WOLFHSM_RUNNER_DIR)/secure.ld \
		-Wl,--gc-sections \
		-Wl,--cmse-implib \
		-Wl,--out-implib=$(SECURE_CMSE_IMPLIB) \
		-o $(SECURE_ELF) $(ALL_SECURE_OBJS) -lgcc

$(SECURE_BIN): $(SECURE_ELF)
	$(OBJCOPY) -O binary $< $@
