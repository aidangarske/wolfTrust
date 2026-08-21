/* psa_store_stub.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 *
 * wolfTrust is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfTrust is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* Minimal psa_store backend: keys can't be persisted in this Zephyr port.
 *
 * The wolfPSA upstream Makefile ships psa_store_posix.c which uses open(2)
 * / read(2) / write(2). Zephyr's NS image has no filesystem, so every PSA key
 * is volatile. wolfPSA still calls these when a volatile lookup misses, to
 * check for a persisted copy — it maps the -4 "not available" return to
 * PSA_ERROR_INVALID_HANDLE and any other non-zero to PSA_ERROR_STORAGE_FAILURE.
 * A volatile-only store has no such copy, so open/remove return -4 (not the
 * generic -1) to yield the spec-correct INVALID_HANDLE for a missing key. */

#include <stddef.h>

#include <wolfpsa/psa_store.h>

int wolfPSA_Store_Open(int type, unsigned long id1, unsigned long id2,
                       int read, void **out)
{
    (void)type; (void)id1; (void)id2; (void)read;
    if (out != NULL) {
        *out = NULL;
    }
    return -4;
}

int wolfPSA_Store_OpenSz(int type, unsigned long id1, unsigned long id2,
                         int read, int variableSz, void **out)
{
    (void)type; (void)id1; (void)id2; (void)read; (void)variableSz;
    if (out != NULL) {
        *out = NULL;
    }
    return -4;
}

int wolfPSA_Store_Remove(int type, unsigned long id1, unsigned long id2)
{
    (void)type; (void)id1; (void)id2;
    return -4;
}

void wolfPSA_Store_Close(void *store)
{
    (void)store;
}

int wolfPSA_Store_Read(void *store, unsigned char *buffer, int len)
{
    (void)store; (void)buffer; (void)len;
    return -1;
}

int wolfPSA_Store_Write(void *store, unsigned char *buffer, int len)
{
    (void)store; (void)buffer; (void)len;
    return -1;
}
