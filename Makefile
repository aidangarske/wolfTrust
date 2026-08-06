ARCH ?= armv8m
TARGET ?= stm32h563

ifeq ($(ARCH)-$(TARGET),armv8m-stm32h563)
include mk/secure-armv8m-stm32h563.mk
else
$(error unsupported secure build tuple ARCH=$(ARCH) TARGET=$(TARGET))
endif

.DEFAULT_GOAL := all

.PHONY: all clean firmware-stm32h563 run-stm32h563 run-stm32h563-tui run-stm32h563-uarts \
		test-domain-host test-domain-compilers test-domain-sanitize \
		test-domain-valgrind test-manifest-host test-manifest-compilers \
		test-manifest-sanitize test-manifest-valgrind test-vnet-host \
		test-lifecycle-host test-lifecycle-compilers test-lifecycle-sanitize \
		test-lifecycle-valgrind \
		test-ipc-host test-ipc-compilers test-ipc-sanitize \
		test-ipc-valgrind test-spm-host test-spm-compilers \
		test-spm-sanitize test-spm-valgrind \
		test-wolfcose-host \
		run-stm32h563-vnet

all: $(SECURE_BIN) $(SECURE_ELF)
	@$(SIZE) $(SECURE_ELF)

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C tests/firmware/stm32h563 clean
	$(MAKE) -C tests/firmware/stm32h563-vnet clean
	$(MAKE) -C tests/host/domain clean
	$(MAKE) -C tests/host/manifest clean
	$(MAKE) -C tests/host/lifecycle clean
	$(MAKE) -C tests/host/ipc clean
	$(MAKE) -C tests/host/spm clean
	$(MAKE) -C tests/host/vnet clean
	$(MAKE) -C tests/host/wolfcose clean

test-domain-host:
	$(MAKE) -C tests/host/domain run

test-domain-compilers:
	$(MAKE) -C tests/host/domain compilers

test-domain-sanitize:
	$(MAKE) -C tests/host/domain sanitize

test-domain-valgrind:
	$(MAKE) -C tests/host/domain valgrind

test-manifest-host:
	$(MAKE) -C tests/host/manifest run

test-manifest-compilers:
	$(MAKE) -C tests/host/manifest compilers

test-manifest-sanitize:
	$(MAKE) -C tests/host/manifest sanitize

test-manifest-valgrind:
	$(MAKE) -C tests/host/manifest valgrind

test-lifecycle-host:
	$(MAKE) -C tests/host/lifecycle run

test-lifecycle-compilers:
	$(MAKE) -C tests/host/lifecycle compilers

test-lifecycle-sanitize:
	$(MAKE) -C tests/host/lifecycle sanitize

test-lifecycle-valgrind:
	$(MAKE) -C tests/host/lifecycle valgrind

test-ipc-host:
	$(MAKE) -C tests/host/ipc run

test-ipc-compilers:
	$(MAKE) -C tests/host/ipc compilers

test-ipc-sanitize:
	$(MAKE) -C tests/host/ipc sanitize

test-ipc-valgrind:
	$(MAKE) -C tests/host/ipc valgrind

test-spm-host:
	$(MAKE) -C tests/host/spm run

test-spm-compilers:
	$(MAKE) -C tests/host/spm compilers

test-spm-sanitize:
	$(MAKE) -C tests/host/spm sanitize

test-spm-valgrind:
	$(MAKE) -C tests/host/spm valgrind

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
