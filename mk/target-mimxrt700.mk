# NXP MIMXRT700 (MIMXRT798S compute Cortex-M33, cpu0) target inputs for the
# secure image build. Included first, before mk/arch-<arch>.mk and
# mk/common.mk; repository paths come from the Makefile.
WT_CPU ?= cortex-m33
PORT_DIR := $(ROOT)/port/mimxrt700
PORT_HEADERS := $(wildcard $(PORT_DIR)/*.h)
TARGET_CONF_DIR := $(PORT_DIR)/conformance

ifeq ($(WT_CONFORMANCE),1)
MANIFEST_INPUT := $(PORT_DIR)/manifest-conformance.json
else ifeq ($(CONFIG_VNET),y)
MANIFEST_INPUT := $(PORT_DIR)/manifest-vnet.json
else
MANIFEST_INPUT := $(PORT_DIR)/manifest.json
endif

# Guests share LPUART0 (LP_FLEXCOMM0), the EVK MCU-Link VCOM console.
WT_SHARED_UART ?= 0
# Clocks left as the first loader configured them; verify against the RM
# before a guest derives a baud divider from these.
WT_GUEST_CORE_CLOCK_HZ ?= 237500000
WT_GUEST_UART_CLOCK_HZ ?= 24000000

# XSPI0 NOR through its Secure alias. The default is the wolfBoot handoff
# layout (wolfBoot at flash+0 owns the FCB and boot header).
WT_SECURE_FLASH_BASE ?= 0x38040000
WT_SECURE_FLASH_SIZE ?= 0x00040000
WT_SECURE_IMAGE_HEADER_SIZE ?= 0
WT_GUEST0_FLASH_BASE ?= 0x28080000
WT_GUEST1_FLASH_BASE ?= 0x28100000
WT_GUEST0_FLASH_SIZE ?= 0x00080000
WT_GUEST1_FLASH_SIZE ?= 0x00040000

TARGET_CFLAGS := \
    -DWT_SHARED_UART=$(WT_SHARED_UART) \
    -DWT_GUEST_CORE_CLOCK_HZ=$(WT_GUEST_CORE_CLOCK_HZ) \
    -DWT_GUEST_UART_CLOCK_HZ=$(WT_GUEST_UART_CLOCK_HZ) \
    -DWT_SECURE_FLASH_BASE=$(WT_SECURE_FLASH_BASE) \
    -DWT_SECURE_FLASH_SIZE=$(WT_SECURE_FLASH_SIZE) \
    -DWT_SECURE_IMAGE_HEADER_SIZE=$(WT_SECURE_IMAGE_HEADER_SIZE) \
    -DWT_GUEST0_FLASH_BASE=$(WT_GUEST0_FLASH_BASE) \
    -DWT_GUEST1_FLASH_BASE=$(WT_GUEST1_FLASH_BASE) \
    -DWT_GUEST0_FLASH_SIZE=$(WT_GUEST0_FLASH_SIZE) \
    -DWT_GUEST1_FLASH_SIZE=$(WT_GUEST1_FLASH_SIZE)
TARGET_LDFLAGS := \
    -Wl,--defsym=WT_SECURE_FLASH_ORIGIN=$(WT_SECURE_FLASH_BASE) \
    -Wl,--defsym=WT_SECURE_FLASH_SIZE=$(WT_SECURE_FLASH_SIZE) \
    -Wl,--defsym=WT_SECURE_IMAGE_HEADER_SIZE=$(WT_SECURE_IMAGE_HEADER_SIZE)
SECURE_LD := $(PORT_DIR)/secure.ld

TARGET_PLATFORM_SRC := $(PORT_DIR)/platform_mimxrt700.c
TARGET_PARTITIONS_SRC := $(PORT_DIR)/partitions.c
TARGET_EXTRA_SRCS := \
    $(wildcard $(PORT_DIR)/rng_entropy.c) \
    $(wildcard $(PORT_DIR)/hsm_flash.c) \
    $(wildcard $(PORT_DIR)/xspi_nor.c)
