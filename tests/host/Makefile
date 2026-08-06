# Makefile
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfTrust.
#
# wolfTrust is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTrust is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

SHELL := /bin/bash
CC ?= cc
BUILD_ROOT ?= build
TEST_KIND ?= unit
LOG_FILE ?= $(BUILD_ROOT)/logs/$(TEST_KIND).log
VERBOSE ?= 0

ifeq ($(VERBOSE),1)
SUBMAKE_FLAGS := --no-print-directory
SUBMAKE_OUTPUT = 2>&1 | tee -a "$(LOG_FILE)"
else
SUBMAKE_FLAGS := --no-print-directory -s
SUBMAKE_OUTPUT = >> "$(LOG_FILE)" 2>&1
endif

UNIT_SUITES := domain manifest lifecycle ipc spm vnet wolfhsm_loopback

.PHONY: all test test-compilers test-sanitize test-valgrind clean

all: test

test:
	@set -e -o pipefail; mkdir -p "$$(dirname "$(LOG_FILE)")"; : > "$(LOG_FILE)"; \
	for suite in $(UNIT_SUITES); do \
		echo "RUN: $(TEST_KIND)/$$suite"; \
		echo "RUN: $(TEST_KIND)/$$suite" >> "$(LOG_FILE)"; \
		if ! $(MAKE) $(SUBMAKE_FLAGS) -C $$suite clean run \
			CC="$(CC)" BUILD_DIR="$(BUILD_ROOT)/$$suite" \
			$(SUBMAKE_OUTPUT); then \
			echo "FAIL: $(TEST_KIND)/$$suite"; cat "$(LOG_FILE)"; exit 1; \
		fi; \
		grep '^PASS:' "$(LOG_FILE)" | tail -20 || true; \
		echo "PASS: $(TEST_KIND)/$$suite"; \
	done
	@echo "PASS: $(TEST_KIND)/all"; echo "LOG: $(LOG_FILE)"

test-compilers:
	$(MAKE) test TEST_KIND=compiler CC="$(CC)" \
		BUILD_ROOT="$(BUILD_ROOT)/compiler"

test-sanitize: TEST_KIND=sanitizer
test-sanitize: LOG_FILE=$(BUILD_ROOT)/logs/sanitizer.log

test-sanitize:
	@set -e -o pipefail; mkdir -p "$$(dirname "$(LOG_FILE)")"; : > "$(LOG_FILE)"; \
	for suite in $(UNIT_SUITES); do \
		echo "RUN: sanitizer/$$suite"; \
		echo "RUN: sanitizer/$$suite" >> "$(LOG_FILE)"; \
		if ! $(MAKE) $(SUBMAKE_FLAGS) -C $$suite clean sanitize \
			BUILD_DIR="$(BUILD_ROOT)/sanitize/$$suite" \
			$(SUBMAKE_OUTPUT); then \
			echo "FAIL: sanitizer/$$suite"; cat "$(LOG_FILE)"; exit 1; \
		fi; \
		echo "PASS: sanitizer/$$suite"; \
	done
	@echo "PASS: sanitizer/all"; echo "LOG: $(LOG_FILE)"

test-valgrind: TEST_KIND=valgrind
test-valgrind: LOG_FILE=$(BUILD_ROOT)/logs/valgrind.log

test-valgrind:
	@set -e -o pipefail; mkdir -p "$$(dirname "$(LOG_FILE)")"; : > "$(LOG_FILE)"; \
	for suite in $(UNIT_SUITES); do \
		echo "RUN: valgrind/$$suite"; \
		echo "RUN: valgrind/$$suite" >> "$(LOG_FILE)"; \
		if ! $(MAKE) $(SUBMAKE_FLAGS) -C $$suite clean valgrind \
			BUILD_DIR="$(BUILD_ROOT)/valgrind/$$suite" \
			$(SUBMAKE_OUTPUT); then \
			echo "FAIL: valgrind/$$suite"; cat "$(LOG_FILE)"; exit 1; \
		fi; \
		echo "PASS: valgrind/$$suite"; \
	done
	@echo "PASS: valgrind/all"; echo "LOG: $(LOG_FILE)"

clean:
	@for suite in $(UNIT_SUITES); do \
		$(MAKE) -C $$suite clean; \
	done
	rm -rf $(BUILD_ROOT)
