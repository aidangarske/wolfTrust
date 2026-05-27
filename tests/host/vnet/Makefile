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

CC := gcc
CFLAGS := -O0 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    -I. \
    -I$(ROOT)/include

VNET_SRCS := \
    $(ROOT)/src/vnet/vnet_mac.c   \
    $(ROOT)/src/vnet/vnet_pool.c  \
    $(ROOT)/src/vnet/vnet_ring.c  \
    $(ROOT)/src/vnet/vnet_fdb.c

TEST_SRCS := \
    main.c       \
    test_mac.c   \
    test_pool.c  \
    test_ring.c  \
    test_fdb.c

VNET_OBJS := $(patsubst %.c,vnet_%.o,$(notdir $(VNET_SRCS)))
TEST_OBJS := $(patsubst %.c,%.o,$(notdir $(TEST_SRCS)))

.PHONY: all run clean

all: test_vnet

vnet_%.o: $(ROOT)/src/vnet/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test_vnet: $(VNET_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(VNET_OBJS) $(TEST_OBJS)

run: test_vnet
	./test_vnet

clean:
	rm -f test_vnet $(VNET_OBJS) $(TEST_OBJS)
