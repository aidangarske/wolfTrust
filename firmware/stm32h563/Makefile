TOOLPREFIX ?= arm-none-eabi-
CC := $(TOOLPREFIX)gcc
OBJCOPY := $(TOOLPREFIX)objcopy
SIZE := $(TOOLPREFIX)size
M33MU ?= m33mu

CPU_FLAGS := -mcpu=cortex-m33 -mthumb -mgeneral-regs-only
CFLAGS_COMMON := $(CPU_FLAGS) -ffreestanding -fno-builtin -nostdlib -O0 -g -Wall -Wextra
CFLAGS_COMMON += -I../../include -I./common
WT_TIMESLICE_MS ?= 100
EMU_TIMEOUT ?= 120
CFLAGS_COMMON += -DWT_TIMESLICE_MS=$(WT_TIMESLICE_MS)
WT_SHARED_UART ?= 0
CFLAGS_COMMON += -DWT_SHARED_UART=$(WT_SHARED_UART)
WT_GUEST_CORE_CLOCK_HZ ?= 240000000
WT_GUEST_UART_CLOCK_HZ ?= 120000000
CFLAGS_COMMON += -DWT_GUEST_CORE_CLOCK_HZ=$(WT_GUEST_CORE_CLOCK_HZ)
CFLAGS_COMMON += -DWT_GUEST_UART_CLOCK_HZ=$(WT_GUEST_UART_CLOCK_HZ)

SECURE_CFLAGS := $(CFLAGS_COMMON) -mcmse
NONSECURE_CFLAGS := $(CFLAGS_COMMON)

BUILD_DIR := build

SECURE_ELF := $(BUILD_DIR)/secure.elf
SECURE_BIN := $(BUILD_DIR)/secure.bin

GUEST0_ELF := $(BUILD_DIR)/guest0.elf
GUEST0_BIN := $(BUILD_DIR)/guest0.bin

GUEST1_ELF := $(BUILD_DIR)/guest1.elf
GUEST1_BIN := $(BUILD_DIR)/guest1.bin

SECURE_SRCS := \
	secure/ivt.c \
	secure/platform_stm32h563.c \
	secure/runtime.c \
	../../src/monitor.c \
	../../src/partitions.c

NONSECURE_SRCS := nonsecure/guest.c

# ---------------------------------------------------------------------------
# Default goal — declared first so it wins regardless of ifeq blocks below
# ---------------------------------------------------------------------------
.PHONY: all clean run run-uarts run-hsm run-hsm-uarts
all: $(SECURE_BIN) $(GUEST0_BIN) $(GUEST1_BIN)
	@$(SIZE) $(SECURE_ELF) $(GUEST0_ELF) $(GUEST1_ELF)

# ---------------------------------------------------------------------------
# wolfHSM / wolfCrypt / wolftrust HSM demo (activated by WT_HSM_DEMO=1)
# ---------------------------------------------------------------------------
ifeq ($(WT_HSM_DEMO),1)

WOLFHSM_DIR := ../../lib/wolfHSM
WOLFSSL_DIR := ../../lib/wolfSSL

# wolfhsm/wh_settings.h picks up our settings via:
#   #ifdef WOLFHSM_CFG
#   #include "wolfhsm_cfg.h"
#   #endif
# We must provide a file named exactly "wolfhsm_cfg.h" on the include path.
# We generate trampolines in $(BUILD_DIR) and add -I$(BUILD_DIR).
WOLFHSM_CFG_H       := $(BUILD_DIR)/wolfhsm_cfg.h
WOLFHSM_GUEST_CFG_H := $(BUILD_DIR)/wolfhsm_guest_cfg.h

HSM_INCLUDES        := -I$(WOLFHSM_DIR) -I$(WOLFSSL_DIR) -I$(BUILD_DIR)
HSM_INCLUDES_SECURE := $(HSM_INCLUDES) -I$(abspath secure)
HSM_INCLUDES_GUEST  := $(HSM_INCLUDES) -I$(abspath nonsecure)
# WOLF_CRYPTO_CB: required by wolfHSM (wh_settings.h asserts it).
# -UNO_CODING: user_settings.h defines NO_CODING but we compile coding.c.
# WC_RESEED_INTERVAL: wolfssl/wolfcrypt/random.h only defines it when
#   CUSTOM_RAND_GENERATE_BLOCK is not set; since user_settings.h sets it
#   we must provide the interval ourselves.
HSM_DEFS_SECURE  := -DWOLFSSL_USER_SETTINGS -DWOLFHSM_CFG \
    -DWOLF_CRYPTO_CB -UNO_CODING \
    -DWC_RESEED_INTERVAL=1000000 -DWT_HSM_DEMO=1
HSM_DEFS_GUEST   := -DWOLFSSL_USER_SETTINGS -DWOLFHSM_CFG \
    -DWOLF_CRYPTO_CB -UNO_CODING \
    -DWC_RESEED_INTERVAL=1000000 -DWT_HSM_DEMO=1 \
    -DWOLFHSM_CFG_ENABLE_CLIENT \
    -DNO_ERROR_STRINGS

# Switch secure ELF to -Os: wolfCrypt is too slow at -O0 for embedded use.
SECURE_CFLAGS := $(CPU_FLAGS) -ffreestanding -fno-builtin -nostdlib -Os -g \
    -Wall -Wextra \
    -I../../include -I./common \
    -DWT_TIMESLICE_MS=$(WT_TIMESLICE_MS) \
    -DWT_SHARED_UART=$(WT_SHARED_UART) \
    -DWT_GUEST_CORE_CLOCK_HZ=$(WT_GUEST_CORE_CLOCK_HZ) \
    -DWT_GUEST_UART_CLOCK_HZ=$(WT_GUEST_UART_CLOCK_HZ) \
    -mcmse \
    $(HSM_INCLUDES_SECURE) $(HSM_DEFS_SECURE)
NONSECURE_CFLAGS := $(CPU_FLAGS) -ffreestanding -fno-builtin -nostdlib -Os -g \
    -Wall -Wextra \
    -ffunction-sections -fdata-sections \
    -I../../include -I./common \
    -DWT_TIMESLICE_MS=$(WT_TIMESLICE_MS) \
    -DWT_SHARED_UART=$(WT_SHARED_UART) \
    -DWT_GUEST_CORE_CLOCK_HZ=$(WT_GUEST_CORE_CLOCK_HZ) \
    -DWT_GUEST_UART_CLOCK_HZ=$(WT_GUEST_UART_CLOCK_HZ) \
    $(HSM_INCLUDES_GUEST) $(HSM_DEFS_GUEST)

# Flags for third-party wolfHSM/wolfCrypt sources: suppress their noisy warnings.
# wolftrust's own sources are compiled with the base flags (no suppression).
HSM_LIB_CFLAGS   := $(SECURE_CFLAGS) \
    -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter
GUEST_LIB_CFLAGS := $(NONSECURE_CFLAGS) \
    -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter

# ---- secure-side source lists -----------------------------------------------

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
    $(WOLFHSM_DIR)/src/wh_flash_ramsim.c \
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
    $(WOLFSSL_DIR)/wolfcrypt/src/asn.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/coding.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/cryptocb.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/ecc.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/error.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/hash.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/hmac.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/logging.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/memory.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/misc.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/random.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/sha256.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/tfm.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/wolfmath.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/wc_port.c

# wolftrust sources always present for the HSM build
WT_SECURE_EXTRA_SRCS := \
    ../../src/arch/armv8m/cmse.c \
    ../../src/arch/armv8m/coroutine_armv8m.c \
    ../../src/sched/coroutine.c \
    ../../src/sync/mutex.c

# Wave 2G: rng_entropy.c — include only when the file exists
WT_SECURE_EXTRA_SRCS += $(wildcard secure/rng_entropy.c)

# libc stubs (memcpy/memset/memmove/memcmp/strlen/etc) — wolfCrypt needs them
# and we build -nostdlib -fno-builtin.
WT_SECURE_EXTRA_SRCS += $(wildcard secure/libc_stubs.c)

# Wave 3/4: services + cmse_transport — include only when present
WT_SECURE_EXTRA_SRCS += \
    $(wildcard ../../src/services/wolfhsm/*.c) \
    $(wildcard ../../src/arch/armv8m/cmse_transport.c)

# ---- guest-side source lists ------------------------------------------------

WOLFHSM_GUEST_SRCS := \
    $(WOLFHSM_DIR)/src/wh_client.c \
    $(WOLFHSM_DIR)/src/wh_client_crypto.c \
    $(WOLFHSM_DIR)/src/wh_client_cryptocb.c \
    $(WOLFHSM_DIR)/src/wh_comm.c \
    $(WOLFHSM_DIR)/src/wh_crypto.c \
    $(WOLFHSM_DIR)/src/wh_keyid.c \
    $(WOLFHSM_DIR)/src/wh_message_comm.c \
    $(WOLFHSM_DIR)/src/wh_message_crypto.c \
    $(WOLFHSM_DIR)/src/wh_message_keystore.c \
    $(WOLFHSM_DIR)/src/wh_message_nvm.c \
    $(WOLFHSM_DIR)/src/wh_message_customcb.c \
    $(WOLFHSM_DIR)/src/wh_message_counter.c \
    $(WOLFHSM_DIR)/src/wh_utils.c

WOLFCRYPT_GUEST_SRCS := \
    $(WOLFSSL_DIR)/wolfcrypt/src/cryptocb.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/ecc.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/random.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/sha256.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/asn.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/coding.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/error.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/hash.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/memory.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/misc.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/sha.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/tfm.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/wolfmath.c \
    $(WOLFSSL_DIR)/wolfcrypt/src/wc_port.c

# Wave 5: guest glue — include only when present
WOLFHSM_GUEST_SRCS += $(wildcard nonsecure/wolfhsm_client_glue.c)

# Guest-side libc stubs (memcpy/memset already inlined by gcc for guest.c,
# but wolfCrypt sources need the full set).
NONSECURE_SRCS += $(wildcard nonsecure/libc_stubs_guest.c)

# ---- object lists -----------------------------------------------------------

HSM_SECURE_BASE_OBJS    := $(patsubst %.c,$(BUILD_DIR)/sec_%.o,$(notdir $(SECURE_SRCS)))
HSM_WOLFHSM_SEC_OBJS    := $(patsubst %.c,$(BUILD_DIR)/wh_sec_%.o,$(notdir $(WOLFHSM_SECURE_SRCS)))
HSM_WOLFCRYPT_SEC_OBJS  := $(patsubst %.c,$(BUILD_DIR)/wc_sec_%.o,$(notdir $(WOLFCRYPT_SECURE_SRCS)))
HSM_WT_EXTRA_OBJS       := $(patsubst %.c,$(BUILD_DIR)/wt_sec_%.o,$(notdir $(WT_SECURE_EXTRA_SRCS)))

HSM_NONSECURE_BASE_OBJS  := $(patsubst %.c,$(BUILD_DIR)/ns_%.o,$(notdir $(NONSECURE_SRCS)))
HSM_WOLFHSM_GUEST_OBJS   := $(patsubst %.c,$(BUILD_DIR)/wh_guest_%.o,$(notdir $(WOLFHSM_GUEST_SRCS)))
HSM_WOLFCRYPT_GUEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/wc_guest_%.o,$(notdir $(WOLFCRYPT_GUEST_SRCS)))

ALL_SECURE_OBJS := \
    $(HSM_SECURE_BASE_OBJS) \
    $(HSM_WOLFHSM_SEC_OBJS) \
    $(HSM_WOLFCRYPT_SEC_OBJS) \
    $(HSM_WT_EXTRA_OBJS)

ALL_GUEST_OBJS := \
    $(HSM_NONSECURE_BASE_OBJS) \
    $(HSM_WOLFHSM_GUEST_OBJS) \
    $(HSM_WOLFCRYPT_GUEST_OBJS)

# ---- wolfhsm_cfg.h trampolines ----------------------------------------------
# wh_settings.h does: #ifdef WOLFHSM_CFG / #include "wolfhsm_cfg.h" / #endif
# We must provide a file of that exact name on the include path.
# The trampoline simply includes our local settings; the include guard in
# the local file prevents double-processing when the compiler also sees
# the local file via other paths.

$(WOLFHSM_CFG_H): | $(BUILD_DIR)
	printf '#include "%s"\n' "$(abspath secure/wh_settings_local.h)" > $@

# Wave 5 guest settings: if wh_settings_guest.h doesn't exist yet, generate
# a placeholder with the minimum defines to satisfy the wolfHSM headers.
$(WOLFHSM_GUEST_CFG_H): | $(BUILD_DIR)
	if [ -f "$(abspath nonsecure/wh_settings_guest.h)" ]; then \
	    printf '#include "%s"\n' "$(abspath nonsecure/wh_settings_guest.h)" > $@; \
	else \
	    { printf '/* placeholder: Wave 5 will provide wh_settings_guest.h */\n'; \
	      printf '#define WOLFHSM_CFG_NO_SYS_TIME\n'; \
	      printf '#define WOLFHSM_CFG_COMM_DATA_LEN 256\n'; \
	      printf '#define WOLFHSM_CFG_SERVER_KEYCACHE_COUNT 4\n'; \
	      printf '#define WOLFHSM_CFG_SERVER_KEYCACHE_BUFSIZE 256\n'; \
	      printf '#define WOLFHSM_CFG_SERVER_CUSTOMCB_COUNT 0\n'; \
	      printf '#define WOLFHSM_CFG_NVM_OBJECT_COUNT 8\n'; \
	    } > $@; \
	fi

# ---- per-group compile rules ------------------------------------------------

# wolfHSM sources — compiled with warning suppression
$(BUILD_DIR)/wh_sec_%.o: $(WOLFHSM_DIR)/src/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(HSM_LIB_CFLAGS) -c -o $@ $<

# wolfCrypt sources — compiled with warning suppression
$(BUILD_DIR)/wc_sec_%.o: $(WOLFSSL_DIR)/wolfcrypt/src/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(HSM_LIB_CFLAGS) -c -o $@ $<

# wolftrust arch/sched/sync/secure sources — no warning suppression
$(BUILD_DIR)/wt_sec_%.o: ../../src/arch/armv8m/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: ../../src/sched/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: ../../src/sync/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wt_sec_%.o: secure/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

# Wave 3/4 services sources
$(BUILD_DIR)/wt_sec_%.o: ../../src/services/wolfhsm/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

# Secure base sources (SECURE_SRCS)
$(BUILD_DIR)/sec_%.o: secure/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sec_%.o: ../../src/%.c $(WOLFHSM_CFG_H) | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) -c -o $@ $<

# Guest wolfHSM — warning suppression
$(BUILD_DIR)/wh_guest_%.o: $(WOLFHSM_DIR)/src/%.c $(WOLFHSM_GUEST_CFG_H) | $(BUILD_DIR)
	$(CC) $(GUEST_LIB_CFLAGS) -c -o $@ $<

# Guest wolfCrypt — warning suppression
$(BUILD_DIR)/wc_guest_%.o: $(WOLFSSL_DIR)/wolfcrypt/src/%.c $(WOLFHSM_GUEST_CFG_H) | $(BUILD_DIR)
	$(CC) $(GUEST_LIB_CFLAGS) -c -o $@ $<

# Guest base sources
$(BUILD_DIR)/ns_%.o: nonsecure/%.c $(WOLFHSM_GUEST_CFG_H) | $(BUILD_DIR)
	$(CC) $(NONSECURE_CFLAGS) -c -o $@ $<

# Wave 5: wolfhsm_client_glue.c lives in nonsecure/ but is listed under
# WOLFHSM_GUEST_SRCS so it gets the wh_guest_ prefix and the guest lib flags.
$(BUILD_DIR)/wh_guest_wolfhsm_client_glue.o: nonsecure/wolfhsm_client_glue.c $(WOLFHSM_GUEST_CFG_H) | $(BUILD_DIR)
	$(CC) $(GUEST_LIB_CFLAGS) -c -o $@ $<

endif  # WT_HSM_DEMO=1

# ---------------------------------------------------------------------------
# Per-mode ELF link rules
# ---------------------------------------------------------------------------

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

ifeq ($(WT_HSM_DEMO),1)

# CMSE import library: secure link emits an object file containing weak
# symbols at the resolved addresses of every cmse_nonsecure_entry. Guests
# link against it instead of hardcoding NSC addresses (wolfBoot pattern).
SECURE_CMSE_IMPLIB := $(BUILD_DIR)/secure_cmse_implib.o

$(SECURE_ELF) $(SECURE_CMSE_IMPLIB) &: $(ALL_SECURE_OBJS) secure/secure.ld | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) \
		-Wl,-Tsecure/secure.ld \
		-Wl,--cmse-implib \
		-Wl,--out-implib=$(SECURE_CMSE_IMPLIB) \
		-o $(SECURE_ELF) $(ALL_SECURE_OBJS) -lgcc

$(GUEST0_ELF): $(ALL_GUEST_OBJS) $(SECURE_CMSE_IMPLIB) nonsecure/guest.ld | $(BUILD_DIR)
	$(CC) $(NONSECURE_CFLAGS) \
		-DWT_GUEST_ID=0u \
		-Wl,-Tnonsecure/guest.ld \
		-Wl,--defsym=GUEST_FLASH_ORIGIN=0x08018000 \
		-Wl,--defsym=GUEST_RAM_ORIGIN=0x20000000 \
		-Wl,--gc-sections \
		-o $@ $(ALL_GUEST_OBJS) $(SECURE_CMSE_IMPLIB) -lgcc

$(GUEST1_ELF): $(ALL_GUEST_OBJS) $(SECURE_CMSE_IMPLIB) nonsecure/guest.ld | $(BUILD_DIR)
	$(CC) $(NONSECURE_CFLAGS) \
		-DWT_GUEST_ID=1u \
		-Wl,-Tnonsecure/guest.ld \
		-Wl,--defsym=GUEST_FLASH_ORIGIN=0x08028000 \
		-Wl,--defsym=GUEST_RAM_ORIGIN=0x20004000 \
		-Wl,--gc-sections \
		-o $@ $(ALL_GUEST_OBJS) $(SECURE_CMSE_IMPLIB) -lgcc

else

$(SECURE_ELF): $(SECURE_SRCS) secure/secure.ld | $(BUILD_DIR)
	$(CC) $(SECURE_CFLAGS) \
		-Wl,-Tsecure/secure.ld \
		-o $@ $(SECURE_SRCS) -lgcc

$(GUEST0_ELF): $(NONSECURE_SRCS) nonsecure/guest.ld | $(BUILD_DIR)
	$(CC) $(NONSECURE_CFLAGS) \
		-DWT_GUEST_ID=0u \
		-Wl,-Tnonsecure/guest.ld \
		-Wl,--defsym=GUEST_FLASH_ORIGIN=0x08018000 \
		-Wl,--defsym=GUEST_RAM_ORIGIN=0x20000000 \
		-o $@ $(NONSECURE_SRCS) -lgcc

$(GUEST1_ELF): $(NONSECURE_SRCS) nonsecure/guest.ld | $(BUILD_DIR)
	$(CC) $(NONSECURE_CFLAGS) \
		-DWT_GUEST_ID=1u \
		-Wl,-Tnonsecure/guest.ld \
		-Wl,--defsym=GUEST_FLASH_ORIGIN=0x08028000 \
		-Wl,--defsym=GUEST_RAM_ORIGIN=0x20004000 \
		-o $@ $(NONSECURE_SRCS) -lgcc

endif  # WT_HSM_DEMO=1

$(SECURE_BIN): $(SECURE_ELF)
	$(OBJCOPY) -O binary $< $@

$(GUEST0_BIN): $(GUEST0_ELF)
	$(OBJCOPY) -O binary $< $@

$(GUEST1_BIN): $(GUEST1_ELF)
	$(OBJCOPY) -O binary $< $@

run: all
	$(M33MU) --cpu stm32h563 --expect-bkpt 0x7f --timeout $(EMU_TIMEOUT) --quit-on-faults \
		$(SECURE_BIN) $(GUEST0_BIN):0x18000 $(GUEST1_BIN):0x28000

run-uarts:
	WT_HSM_DEMO=$(WT_HSM_DEMO) WT_SHARED_UART=$(WT_SHARED_UART) EMU_TIMEOUT=$(EMU_TIMEOUT) M33MU="$(M33MU)" ./run_dual_uart_demo.sh

run-hsm:
	$(MAKE) WT_HSM_DEMO=1 run

run-hsm-uarts:
	$(MAKE) WT_HSM_DEMO=1 run-uarts

clean:
	rm -rf $(BUILD_DIR)
