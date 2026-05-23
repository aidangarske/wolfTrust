/* runtime.c
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <stddef.h>

void* memset(void* dst, int value, size_t size)
{
    unsigned char* out = (unsigned char*)dst;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = (unsigned char)value;
    }

    return dst;
}

void* memcpy(void* dst, const void* src, size_t size)
{
    unsigned char* out = (unsigned char*)dst;
    const unsigned char* in = (const unsigned char*)src;
    size_t i;

    for (i = 0; i < size; ++i) {
        out[i] = in[i];
    }

    return dst;
}
