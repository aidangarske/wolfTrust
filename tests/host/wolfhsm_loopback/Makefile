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
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA

# ---------------------------------------------------------------------------
# Paths (relative to this directory)
# ---------------------------------------------------------------------------
ROOT      := ../../..
WOLFHSM   := $(ROOT)/lib/wolfHSM
WOLFSSL   := $(ROOT)/lib/wolfSSL

# ---------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------
CC := gcc

# ---------------------------------------------------------------------------
# Compile flags
# ---------------------------------------------------------------------------
CFLAGS := \
    -DWOLFSSL_USER_SETTINGS \
    -UHAVE_CONFIG_H \
    -DWOLFHSM_CFG_NO_SYS_TIME \
    -DWOLFHSM_CFG_ENABLE_CLIENT \
    -DWOLFHSM_CFG_ENABLE_SERVER \
    -I. \
    -I$(WOLFHSM) \
    -I$(WOLFSSL) \
    -O0 -g -Wall -Wno-unused-function -Wno-unused-parameter

# ---------------------------------------------------------------------------
# Source lists (full paths; flat object output with a prefix to avoid
# collisions between files that share a basename across directories)
# ---------------------------------------------------------------------------

# wolfHSM core
_WH_SRCS := \
    $(WOLFHSM)/src/wh_client.c          \
    $(WOLFHSM)/src/wh_client_crypto.c   \
    $(WOLFHSM)/src/wh_client_cryptocb.c \
    $(WOLFHSM)/src/wh_comm.c            \
    $(WOLFHSM)/src/wh_message_comm.c    \
    $(WOLFHSM)/src/wh_message_crypto.c  \
    $(WOLFHSM)/src/wh_message_keystore.c \
    $(WOLFHSM)/src/wh_message_nvm.c     \
    $(WOLFHSM)/src/wh_message_customcb.c \
    $(WOLFHSM)/src/wh_message_counter.c  \
    $(WOLFHSM)/src/wh_server.c          \
    $(WOLFHSM)/src/wh_server_crypto.c   \
    $(WOLFHSM)/src/wh_server_keystore.c \
    $(WOLFHSM)/src/wh_server_nvm.c      \
    $(WOLFHSM)/src/wh_server_customcb.c \
    $(WOLFHSM)/src/wh_server_counter.c  \
    $(WOLFHSM)/src/wh_transport_mem.c   \
    $(WOLFHSM)/src/wh_nvm.c             \
    $(WOLFHSM)/src/wh_nvm_flash.c       \
    $(WOLFHSM)/src/wh_flash_ramsim.c    \
    $(WOLFHSM)/src/wh_flash_unit.c      \
    $(WOLFHSM)/src/wh_lock.c            \
    $(WOLFHSM)/src/wh_utils.c           \
    $(WOLFHSM)/src/wh_crypto.c          \
    $(WOLFHSM)/src/wh_keyid.c           \
    $(WOLFHSM)/src/wh_log.c             \
    $(WOLFHSM)/src/wh_timeout.c

# wolfHSM port (posix time)
_WH_PORT_SRCS := \
    $(WOLFHSM)/port/posix/posix_time.c

# wolfCrypt minimal subset
_WC_SRCS := \
    $(WOLFSSL)/wolfcrypt/src/ecc.c      \
    $(WOLFSSL)/wolfcrypt/src/sha256.c   \
    $(WOLFSSL)/wolfcrypt/src/random.c   \
    $(WOLFSSL)/wolfcrypt/src/tfm.c      \
    $(WOLFSSL)/wolfcrypt/src/wolfmath.c \
    $(WOLFSSL)/wolfcrypt/src/memory.c   \
    $(WOLFSSL)/wolfcrypt/src/error.c    \
    $(WOLFSSL)/wolfcrypt/src/logging.c  \
    $(WOLFSSL)/wolfcrypt/src/hash.c     \
    $(WOLFSSL)/wolfcrypt/src/wc_port.c  \
    $(WOLFSSL)/wolfcrypt/src/cryptocb.c \
    $(WOLFSSL)/wolfcrypt/src/asn.c      \
    $(WOLFSSL)/wolfcrypt/src/coding.c  \
    $(WOLFSSL)/wolfcrypt/src/misc.c

# Test
_TEST_SRCS := test_loopback.c

# Prefix each group to avoid basename collisions
_WH_OBJS      := $(patsubst %.c,wh_%.o,$(notdir $(_WH_SRCS)))
_WH_PORT_OBJS := $(patsubst %.c,whport_%.o,$(notdir $(_WH_PORT_SRCS)))
_WC_OBJS      := $(patsubst %.c,wc_%.o,$(notdir $(_WC_SRCS)))
_TEST_OBJS    := $(patsubst %.c,%.o,$(notdir $(_TEST_SRCS)))

ALL_OBJS := $(_WH_OBJS) $(_WH_PORT_OBJS) $(_WC_OBJS) $(_TEST_OBJS)

# ---------------------------------------------------------------------------
# Compilation rules — explicit pattern per directory
# ---------------------------------------------------------------------------
wh_%.o: $(WOLFHSM)/src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

whport_%.o: $(WOLFHSM)/port/posix/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

wc_%.o: $(WOLFSSL)/wolfcrypt/src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all run clean

all: test_loopback

test_loopback: $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $(ALL_OBJS) -lm

run: test_loopback
	./test_loopback
	@exit $$?

clean:
	rm -f test_loopback $(ALL_OBJS)
