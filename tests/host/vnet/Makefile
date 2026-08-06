# Makefile — host unit tests for the wolfTrust VNET dataplane primitives.
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
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

ROOT := ../../..

CC ?= cc
BUILD_DIR ?= build
CFLAGS := -O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    -I. \
    -I$(ROOT)/include
CFLAGS += $(EXTRA_CFLAGS)

VNET_SRCS := \
    $(ROOT)/src/vnet/vnet_mac.c    \
    $(ROOT)/src/vnet/vnet_pool.c   \
    $(ROOT)/src/vnet/vnet_ring.c   \
    $(ROOT)/src/vnet/vnet_fdb.c    \
    $(ROOT)/src/vnet/vnet_switch.c

TEST_SRCS := \
    main.c        \
    test_mac.c    \
    test_pool.c   \
    test_ring.c   \
    test_fdb.c    \
    test_switch.c

VNET_OBJS := $(patsubst %.c,$(BUILD_DIR)/vnet_%.o,$(notdir $(VNET_SRCS)))
TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(TEST_SRCS)))
TEST_BIN := $(BUILD_DIR)/test_vnet

.PHONY: all run compilers sanitize valgrind clean

all: $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/vnet_%.o: $(ROOT)/src/vnet/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_BIN): $(VNET_OBJS) $(TEST_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(EXTRA_LDFLAGS) -o $@ $(VNET_OBJS) $(TEST_OBJS)

run: $(TEST_BIN)
	$(TEST_BIN)

compilers:
	$(MAKE) clean run CC=gcc BUILD_DIR=$(BUILD_DIR)/gcc
	$(MAKE) clean run CC=clang BUILD_DIR=$(BUILD_DIR)/clang

sanitize:
	$(MAKE) clean run CC=clang \
		BUILD_DIR=$(BUILD_DIR)/sanitize \
		EXTRA_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
		EXTRA_LDFLAGS="-fsanitize=address,undefined"

valgrind: clean $(TEST_BIN)
	valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=all \
		$(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)
