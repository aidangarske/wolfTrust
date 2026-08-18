ARCH ?= armv8m
TARGET ?= stm32h563

ifeq ($(ARCH)-$(TARGET),armv8m-stm32h563)
include mk/secure-armv8m-stm32h563.mk
else
$(error unsupported secure build tuple ARCH=$(ARCH) TARGET=$(TARGET))
endif

.DEFAULT_GOAL := all

.PHONY: all test test-conformance test-target fetch-psa-ff-tests \
		clean firmware-stm32h563 run-stm32h563 run-stm32h563-tui run-stm32h563-uarts \
		test-domain-host test-domain-compilers test-domain-sanitize \
		test-domain-valgrind test-manifest-host test-manifest-compilers \
		test-manifest-sanitize test-manifest-valgrind test-vnet-host \
		test-lifecycle-host test-lifecycle-compilers test-lifecycle-sanitize \
		test-lifecycle-valgrind \
		test-ipc-host test-ipc-compilers test-ipc-sanitize \
		test-ipc-valgrind test-ffm-host test-ffm-compilers \
		test-ffm-sanitize test-ffm-valgrind \
		test-spm-host test-spm-compilers \
		test-spm-sanitize test-spm-valgrind \
		test-wolfcose-host \
		run-stm32h563-vnet

all: $(SECURE_BIN) $(SECURE_ELF)
	@$(SIZE) $(SECURE_ELF)

test:
	@$(MAKE) --no-print-directory -C tests/host test

# FF-M target-only scenarios (partition restart, cross-domain isolation) that
# need a real Cortex-M execution model. Separate from `make test` (host-only),
# like `make test-conformance`. Auto-detect an M33MU emulator (or set
# WT_TARGET_SCENARIOS=1); skip explicitly otherwise so it never silently passes.
# Runs inside the wolfboot-ci-m33mu container, never bare-metal.
test-target:
	@if ! tests/target/detect_m33mu.sh >/dev/null 2>&1; then \
		echo "SKIP: FF-M target scenarios ($$(tests/target/detect_m33mu.sh 2>&1))"; \
	else \
		mkdir -p $(BUILD_DIR)/logs; rc=0; \
		for s in positive restart crossdomain confboot; do \
			printf 'RUN: target/%s ... ' "$$s"; \
			if tests/target/run_m33mu_scenario.sh $$s \
					> $(BUILD_DIR)/logs/target-$$s.log 2>&1; then \
				echo "PASS"; \
			else \
				echo "FAIL"; rc=1; \
			fi; \
			echo "LOG: $(BUILD_DIR)/logs/target-$$s.log"; \
		done; \
		if [ $$rc -eq 0 ]; then \
			echo "PASS: target/all"; \
		else \
			echo "FAIL: target/all — tail of the failing log(s):"; \
			for s in positive restart crossdomain confboot; do \
				grep -Eq 'PASS: target/'"$$s"'$$' \
					$(BUILD_DIR)/logs/target-$$s.log 2>/dev/null || { \
					echo "----- target/$$s -----"; \
					tail -20 $(BUILD_DIR)/logs/target-$$s.log; }; \
			done; \
			exit 1; \
		fi; \
	fi

test-compilers:
	@$(MAKE) --no-print-directory -C tests/host test-compilers

test-sanitize:
	@$(MAKE) --no-print-directory -C tests/host test-sanitize

test-valgrind:
	@$(MAKE) --no-print-directory -C tests/host test-valgrind

fetch-psa-ff-tests:
	@tests/upstream/fetch_psa_arch_tests.sh \
		$(BUILD_DIR)/upstream/psa-arch-tests

test-manifest-ingest: fetch-psa-ff-tests
	@echo "RUN: conformance/manifest_ingest"
	@python3 tests/host/manifest_ingest/run.py \
		$(abspath $(BUILD_DIR))/upstream/psa-arch-tests

# One conformance entry point. With an M33MU emulator (or WT_TARGET_SCENARIOS=1)
# it runs the full FF-M IPC suite on the target — the real conformance evidence
# (85 passed / 4 skipped). Without one it falls back to the host subset (20
# client-side IPC/policy tests) and prints an explicit non-hardware warning, so
# a host-only run is never mistaken for the full suite. Detection is shared with
# test-target via tests/target/detect_m33mu.sh. CI runs the same target suite
# through the confboot leg of the wolfboot-wolftrust-m33mu-scenarios matrix.
test-conformance: fetch-psa-ff-tests test-manifest-ingest
	@if tests/target/detect_m33mu.sh >/dev/null 2>&1; then \
		mkdir -p $(BUILD_DIR)/logs; \
		printf 'RUN: conformance/target (full FF-M IPC suite on M33MU) ... '; \
		if tests/target/run_m33mu_scenario.sh confboot \
				> $(BUILD_DIR)/logs/conformance-target.log 2>&1; then \
			echo "PASS"; \
			echo "PASS: conformance/target"; \
			echo "LOG: $(BUILD_DIR)/logs/conformance-target.log"; \
		else \
			echo "FAIL"; \
			echo "FAIL: conformance/target — tail of the log:"; \
			tail -25 $(BUILD_DIR)/logs/conformance-target.log; \
			echo "LOG: $(BUILD_DIR)/logs/conformance-target.log"; \
			exit 1; \
		fi; \
	else \
		echo "RUN: conformance/host-subset"; \
		$(MAKE) --no-print-directory -C tests/host/psa_ff_upstream run \
			BUILD_DIR=$(abspath $(BUILD_DIR))/psa-ff-upstream \
			PSA_ARCH_TESTS_DIR=$(abspath $(BUILD_DIR))/upstream/psa-arch-tests; \
		echo "WARNING: host-only conformance subset (20 client-side IPC/policy tests) — NOT emulator/hardware evidence."; \
		echo "WARNING: isolation, panic-reset, IRQ, and cross-domain tests need M33MU ($$(tests/target/detect_m33mu.sh 2>&1))."; \
		echo "WARNING: run the full 85/4 suite with an emulator, or: make test-target."; \
		echo "PASS: conformance/host-subset (partial — see warnings above)"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C tests/firmware/stm32h563 clean
	$(MAKE) -C tests/firmware/stm32h563-vnet clean
	$(MAKE) -C tests/host/domain clean
	$(MAKE) -C tests/host/manifest clean
	$(MAKE) -C tests/host/lifecycle clean
	$(MAKE) -C tests/host/ipc clean
	$(MAKE) -C tests/host/ffm clean
	$(MAKE) -C tests/host/ffm_domain clean
	$(MAKE) -C tests/host/sp_layout clean
	$(MAKE) -C tests/host/spm_gate clean
	$(MAKE) -C tests/host/psa_ff_upstream clean
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

test-ffm-host:
	$(MAKE) -C tests/host/ffm run

test-ffm-compilers:
	$(MAKE) -C tests/host/ffm compilers

test-ffm-sanitize:
	$(MAKE) -C tests/host/ffm sanitize

test-ffm-valgrind:
	$(MAKE) -C tests/host/ffm valgrind

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
