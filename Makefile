ARCH ?= armv8m
TARGET ?= stm32h563

ifeq ($(ARCH)-$(TARGET),armv8m-stm32h563)
include mk/secure-armv8m-stm32h563.mk
else
$(error unsupported secure build tuple ARCH=$(ARCH) TARGET=$(TARGET))
endif

.DEFAULT_GOAL := all

.PHONY: all clean firmware-stm32h563 run-stm32h563 run-stm32h563-tui run-stm32h563-uarts \
        test-vnet-host test-wolfcose-host run-stm32h563-vnet

all: $(SECURE_BIN) $(SECURE_ELF)
	@$(SIZE) $(SECURE_ELF)

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C tests/firmware/stm32h563 clean
	$(MAKE) -C tests/firmware/stm32h563-vnet clean
	$(MAKE) -C tests/host/vnet clean
	$(MAKE) -C tests/host/wolfcose clean

test-vnet-host:
	$(MAKE) -C tests/host/vnet run

test-wolfcose-host:
	$(MAKE) -C tests/host/wolfcose run

run-stm32h563-vnet:
	$(MAKE) -C tests/firmware/stm32h563-vnet ARCH=$(ARCH) TARGET=$(TARGET) run

firmware-stm32h563:
	$(MAKE) -C tests/firmware/stm32h563 ARCH=$(ARCH) TARGET=$(TARGET) all

run-stm32h563:
	$(MAKE) -C tests/firmware/stm32h563 ARCH=$(ARCH) TARGET=$(TARGET) run

run-stm32h563-tui:
	$(MAKE) -C tests/firmware/stm32h563 ARCH=$(ARCH) TARGET=$(TARGET) run-tui

run-stm32h563-uarts:
	$(MAKE) -C tests/firmware/stm32h563 ARCH=$(ARCH) TARGET=$(TARGET) run-uarts
