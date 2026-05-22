TOOLPREFIX ?= arm-none-eabi-
CC := $(TOOLPREFIX)gcc
OBJCOPY := $(TOOLPREFIX)objcopy
SIZE := $(TOOLPREFIX)size

CPU_FLAGS := -mcpu=cortex-m33 -mthumb -mgeneral-regs-only
CFLAGS_COMMON := $(CPU_FLAGS) -ffreestanding -fno-builtin -nostdlib -O0 -g -Wall -Wextra
CFLAGS_COMMON += -I../../include -I./common
WT_TIMESLICE_MS ?= 100
EMU_TIMEOUT ?= 120
CFLAGS_COMMON += -DWT_TIMESLICE_MS=$(WT_TIMESLICE_MS)

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

.PHONY: all clean run run-uarts

all: $(SECURE_BIN) $(GUEST0_BIN) $(GUEST1_BIN)
	@$(SIZE) $(SECURE_ELF) $(GUEST0_ELF) $(GUEST1_ELF)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(SECURE_ELF): $(BUILD_DIR) $(SECURE_SRCS) secure/secure.ld
	$(CC) $(SECURE_CFLAGS) \
		-Wl,-Tsecure/secure.ld \
		-o $@ $(SECURE_SRCS) -lgcc

$(SECURE_BIN): $(SECURE_ELF)
	$(OBJCOPY) -O binary $< $@

$(GUEST0_ELF): $(BUILD_DIR) $(NONSECURE_SRCS) nonsecure/guest.ld
	$(CC) $(NONSECURE_CFLAGS) \
		-DWT_GUEST_ID=0u \
		-Wl,-Tnonsecure/guest.ld \
		-Wl,--defsym=GUEST_FLASH_ORIGIN=0x08002000 \
		-Wl,--defsym=GUEST_RAM_ORIGIN=0x20000000 \
		-o $@ $(NONSECURE_SRCS) -lgcc

$(GUEST0_BIN): $(GUEST0_ELF)
	$(OBJCOPY) -O binary $< $@

$(GUEST1_ELF): $(BUILD_DIR) $(NONSECURE_SRCS) nonsecure/guest.ld
	$(CC) $(NONSECURE_CFLAGS) \
		-DWT_GUEST_ID=1u \
		-Wl,-Tnonsecure/guest.ld \
		-Wl,--defsym=GUEST_FLASH_ORIGIN=0x08012000 \
		-Wl,--defsym=GUEST_RAM_ORIGIN=0x20004000 \
		-o $@ $(NONSECURE_SRCS) -lgcc

$(GUEST1_BIN): $(GUEST1_ELF)
	$(OBJCOPY) -O binary $< $@

run: all
	/home/dan/src/m33mu/build/m33mu --cpu stm32h563 --expect-bkpt 0x7f --timeout $(EMU_TIMEOUT) --quit-on-faults \
		$(SECURE_BIN) $(GUEST0_BIN):0x2000 $(GUEST1_BIN):0x12000

run-uarts:
	EMU_TIMEOUT=$(EMU_TIMEOUT) ./run_dual_uart_demo.sh

clean:
	rm -rf $(BUILD_DIR)
