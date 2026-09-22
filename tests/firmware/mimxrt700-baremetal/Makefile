# Bare-metal Non-secure guest0 for the MIMXRT700-EVK. Builds the wolfTrust
# Secure image (and its CMSE import library) through the top-level Makefile,
# then links guest0 against that import library so the WolfTrust_FFM_* veneers
# resolve. Flashing and signing are driven by tests/target/run_rt700_hardware.sh.
TOOLPREFIX ?= arm-none-eabi-
CC := $(TOOLPREFIX)gcc
OBJCOPY := $(TOOLPREFIX)objcopy
SIZE := $(TOOLPREFIX)size

ROOT := ../../..
TARGET ?= mimxrt700
ARCH ?= armv8m

BUILD ?= build
TOP_BUILD_DIR := $(ROOT)/build
SECURE_CMSE_IMPLIB := $(TOP_BUILD_DIR)/secure_cmse_implib.o

# guest0 links inside the Non-secure guest0 window (port/mimxrt700/memory_map.h).
GUEST0_FLASH_ORIGIN ?= 0x28080000
GUEST0_FLASH_LENGTH ?= 0x00080000
GUEST0_RAM_ORIGIN ?= 0x20100000
GUEST0_RAM_LENGTH ?= 0x00040000

CFLAGS := -mcpu=cortex-m33 -mthumb -mgeneral-regs-only -ffreestanding \
    -fno-builtin -nostdlib -Os -g -Wall -Wextra \
    -ffunction-sections -fdata-sections -mno-unaligned-access \
    -I$(ROOT)/include

OBJS := $(BUILD)/guest0.o $(BUILD)/psa_ffm_client.o

all: $(BUILD)/guest0.bin

$(SECURE_CMSE_IMPLIB): FORCE
	$(MAKE) -C $(ROOT) ARCH=$(ARCH) TARGET=$(TARGET) TOOLPREFIX=$(TOOLPREFIX) \
	    WT_ATTEST_COSE=0 build/secure_cmse_implib.o

$(BUILD)/guest0.o: guest0.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/psa_ffm_client.o: $(ROOT)/src/client/psa_ffm_client.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/guest0.elf: $(OBJS) $(SECURE_CMSE_IMPLIB) guest0.ld | $(BUILD)
	$(CC) $(CFLAGS) \
	    -Wl,-Tguest0.ld \
	    -Wl,--defsym=GUEST_FLASH_ORIGIN=$(GUEST0_FLASH_ORIGIN) \
	    -Wl,--defsym=GUEST_FLASH_LENGTH=$(GUEST0_FLASH_LENGTH) \
	    -Wl,--defsym=GUEST_RAM_ORIGIN=$(GUEST0_RAM_ORIGIN) \
	    -Wl,--defsym=GUEST_RAM_LENGTH=$(GUEST0_RAM_LENGTH) \
	    -Wl,--gc-sections \
	    -o $@ $(OBJS) $(SECURE_CMSE_IMPLIB) -lgcc
	$(SIZE) $@

$(BUILD)/guest0.bin: $(BUILD)/guest0.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean FORCE
FORCE:
