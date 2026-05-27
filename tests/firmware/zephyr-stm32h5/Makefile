SHELL := /bin/sh

ROOT := ../../..
SCRIPT_DIR := $(CURDIR)/scripts
TOP_BUILD_DIR := $(abspath $(ROOT)/build)

ZEPHYR_TAG ?= v4.2.0
ZEPHYR_WORKSPACE ?= $(CURDIR)/.workspace/zephyrproject
WEST_PYTHON ?= $(CURDIR)/.venv/bin/python3
WEST_BIN ?= $(CURDIR)/.venv/bin/west
WEST_PROJECTS ?= cmsis cmsis_6 hal_stm32
ZEPHYR_BOARD ?= nucleo_h563zi/stm32h563xx/ns

EMU_TIMEOUT ?= 300
M33MU ?= m33mu

WT_GUEST0_EMU_OFFSET ?= 0x20000
WT_GUEST1_EMU_OFFSET ?= 0x40000
WT_SHARED_UART ?= 3

# Dual-guest demo: Zephyr drives the TEE client as guest-a, a baremetal
# heartbeat companion runs as guest-b. Override WT_MAX_GUESTS=1 to drop the
# companion and rebuild the secure side for the single-TEE configuration.
WT_MAX_GUESTS ?= 2

# Baremetal guest-b memory map — matches WT_GUEST1_FLASH_BASE / WT_GUEST1_RAM_BASE
# in src/port/stm32h563/memory_map.h. Kept here as overridable variables so
# this Makefile is self-contained.
WT_GUEST1_FLASH_BASE ?= 0x08040000
WT_GUEST1_RAM_BASE   ?= 0x20008000
WT_GUEST_FLASH_SIZE  ?= 0x00020000
WT_GUEST_RAM_SIZE    ?= 0x00008000
WT_GUEST_CORE_CLOCK_HZ ?= 240000000
WT_GUEST_UART_CLOCK_HZ ?= 120000000

GUEST_CC      ?= arm-none-eabi-gcc
GUEST_OBJCOPY ?= arm-none-eabi-objcopy

SECURE_BIN         := $(TOP_BUILD_DIR)/wolftrust.bin
SECURE_CMSE_IMPLIB := $(TOP_BUILD_DIR)/secure_cmse_implib.o
GUEST0_BIN         := $(CURDIR)/build/guest0/zephyr/zephyr.bin
GUEST1_DIR         := $(CURDIR)/apps/guest1
GUEST1_BUILD_DIR   := $(CURDIR)/build/guest1
GUEST1_ELF         := $(GUEST1_BUILD_DIR)/guest1.elf
GUEST1_BIN         := $(GUEST1_BUILD_DIR)/guest1.bin
PATCH_STAMP        := $(CURDIR)/.workspace/wolftrust-zephyr.patch.stamp

GUEST1_CFLAGS := -mcpu=cortex-m33 -mthumb -mgeneral-regs-only \
    -ffreestanding -fno-builtin -nostdlib -Os -g -Wall -Wextra \
    -ffunction-sections -fdata-sections \
    -DWT_GUEST_CORE_CLOCK_HZ=$(WT_GUEST_CORE_CLOCK_HZ) \
    -DWT_GUEST_UART_CLOCK_HZ=$(WT_GUEST_UART_CLOCK_HZ)

GUEST1_LDFLAGS := \
    -Wl,-T$(GUEST1_DIR)/guest1.ld \
    -Wl,--defsym=GUEST_FLASH_ORIGIN=$(WT_GUEST1_FLASH_BASE) \
    -Wl,--defsym=GUEST_FLASH_LENGTH=$(WT_GUEST_FLASH_SIZE) \
    -Wl,--defsym=GUEST_RAM_ORIGIN=$(WT_GUEST1_RAM_BASE) \
    -Wl,--defsym=GUEST_RAM_LENGTH=$(WT_GUEST_RAM_SIZE) \
    -Wl,--gc-sections

.PHONY: all clone workspace patch secure build-guest0 build-guest1 \
        run run-uarts run-tui clean

all: build-guest0 build-guest1

clone workspace:
	ZEPHYR_TAG="$(ZEPHYR_TAG)" WEST_PYTHON="$(WEST_PYTHON)" WEST_BIN="$(WEST_BIN)" \
	ZEPHYR_WORKSPACE="$(ZEPHYR_WORKSPACE)" WEST_PROJECTS="$(WEST_PROJECTS)" \
	"$(SCRIPT_DIR)/setup_venv.sh"
	ZEPHYR_TAG="$(ZEPHYR_TAG)" WEST_PYTHON="$(WEST_PYTHON)" WEST_BIN="$(WEST_BIN)" \
	ZEPHYR_WORKSPACE="$(ZEPHYR_WORKSPACE)" WEST_PROJECTS="$(WEST_PROJECTS)" \
	"$(SCRIPT_DIR)/clone_workspace.sh"

patch: $(PATCH_STAMP)

$(PATCH_STAMP): $(wildcard $(CURDIR)/patches/zephyr/*.patch) $(SCRIPT_DIR)/apply_zephyr_patches.sh | workspace
	ZEPHYR_TAG="$(ZEPHYR_TAG)" ZEPHYR_WORKSPACE="$(ZEPHYR_WORKSPACE)" \
	"$(SCRIPT_DIR)/apply_zephyr_patches.sh"
	@mkdir -p "$(dir $@)"
	@touch "$@"

# Force-rebuild wolfTrust with this demo's guest count. We delete the root
# build dir directly instead of `$(MAKE) -C $(ROOT) clean`, because that
# clean target also wipes tests/firmware/stm32h563/build/ — a peer baremetal
# test suite with its own generated headers that we have no business touching.
secure:
	rm -rf "$(TOP_BUILD_DIR)"
	WT_MAX_GUESTS="$(WT_MAX_GUESTS)" $(MAKE) -C "$(ROOT)" build/wolftrust.bin build/secure_cmse_implib.o

build-guest0: patch secure
	ZEPHYR_TAG="$(ZEPHYR_TAG)" ZEPHYR_WORKSPACE="$(ZEPHYR_WORKSPACE)" \
	WEST_PYTHON="$(WEST_PYTHON)" WEST_BIN="$(WEST_BIN)" \
	ZEPHYR_BOARD="$(ZEPHYR_BOARD)" \
	SECURE_BIN="$(SECURE_BIN)" SECURE_CMSE_IMPLIB="$(SECURE_CMSE_IMPLIB)" \
	WT_SHARED_UART="$(WT_SHARED_UART)" \
	"$(SCRIPT_DIR)/build_guest.sh" guest0

build-guest1: $(GUEST1_BIN)

$(GUEST1_BUILD_DIR):
	mkdir -p "$@"

$(GUEST1_ELF): $(GUEST1_DIR)/guest1.c $(GUEST1_DIR)/guest1.ld | $(GUEST1_BUILD_DIR)
	$(GUEST_CC) $(GUEST1_CFLAGS) $(GUEST1_LDFLAGS) \
	    -o $@ $(GUEST1_DIR)/guest1.c -lgcc

$(GUEST1_BIN): $(GUEST1_ELF)
	$(GUEST_OBJCOPY) -O binary $< $@

run: all
	EMU_TIMEOUT="$(EMU_TIMEOUT)" M33MU="$(M33MU)" \
	SECURE_BIN="$(SECURE_BIN)" \
	GUEST0_BIN="$(GUEST0_BIN)" GUEST1_BIN="$(GUEST1_BIN)" \
	WT_GUEST0_EMU_OFFSET="$(WT_GUEST0_EMU_OFFSET)" \
	WT_GUEST1_EMU_OFFSET="$(WT_GUEST1_EMU_OFFSET)" \
	WT_SHARED_UART="$(WT_SHARED_UART)" \
	"$(SCRIPT_DIR)/run_m33mu.sh"

run-uarts: all
	EMU_TIMEOUT="$(EMU_TIMEOUT)" M33MU="$(M33MU)" \
	SECURE_BIN="$(SECURE_BIN)" \
	GUEST0_BIN="$(GUEST0_BIN)" GUEST1_BIN="$(GUEST1_BIN)" \
	WT_GUEST0_EMU_OFFSET="$(WT_GUEST0_EMU_OFFSET)" \
	WT_GUEST1_EMU_OFFSET="$(WT_GUEST1_EMU_OFFSET)" \
	WT_SHARED_UART="$(WT_SHARED_UART)" \
	"$(SCRIPT_DIR)/run_m33mu.sh" --uarts

run-tui: all
	EMU_TIMEOUT="$(EMU_TIMEOUT)" M33MU="$(M33MU)" \
	SECURE_BIN="$(SECURE_BIN)" \
	GUEST0_BIN="$(GUEST0_BIN)" GUEST1_BIN="$(GUEST1_BIN)" \
	WT_GUEST0_EMU_OFFSET="$(WT_GUEST0_EMU_OFFSET)" \
	WT_GUEST1_EMU_OFFSET="$(WT_GUEST1_EMU_OFFSET)" \
	WT_SHARED_UART="$(WT_SHARED_UART)" \
	"$(SCRIPT_DIR)/run_m33mu.sh" --tui

clean:
	rm -rf build
