# Makefile
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfTrust.

ROOT := ../../..
WOLFCOSE := $(ROOT)/lib/wolfCOSE
WOLFSSL_PREFIX ?= /usr/local

CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
CFLAGS += -DHAVE_ANONYMOUS_INLINE_AGGREGATES=1
CFLAGS += $(EXTRA_CFLAGS)
CPPFLAGS += -I$(WOLFCOSE)/include -I$(WOLFSSL_PREFIX)/include
CPPFLAGS += -I$(ROOT)/include
CPPFLAGS += -DWOLFCOSE_LEAN -DWOLFCOSE_ENABLE_EXT_SIGN
CPPFLAGS += -DWOLFCOSE_NO_SIGN1_VERIFY -DWOLFCOSE_NO_ENCRYPT0
CPPFLAGS += -DWOLFCOSE_NO_MAC0 -DWOLFCOSE_NO_KEY_ENCODE
CPPFLAGS += -DWOLFCOSE_NO_KEY_DECODE
LDFLAGS += $(EXTRA_LDFLAGS)
LDLIBS += -L$(WOLFSSL_PREFIX)/lib -lwolfssl

TARGET := test_wolfcose_integration
SOURCES := test_wolfcose_integration.c \
    $(ROOT)/src/services/attestation_cose.c \
    $(WOLFCOSE)/src/wolfcose.c \
    $(WOLFCOSE)/src/wolfcose_cbor.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

run: $(TARGET)
	DYLD_LIBRARY_PATH="$(WOLFSSL_PREFIX)/lib:$${DYLD_LIBRARY_PATH}" \
	LD_LIBRARY_PATH="$(WOLFSSL_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	./$(TARGET)

clean:
	rm -f $(TARGET)
