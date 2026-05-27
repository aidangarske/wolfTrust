/* test_vnet.h
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

#ifndef TEST_VNET_H
#define TEST_VNET_H

#include <stdio.h>

extern int g_test_pass;
extern int g_test_fail;

#define T_CHECK(cond) do {                                              \
    if (!(cond)) {                                                      \
        fprintf(stderr, "FAIL: %s:%d %s: %s\n",                         \
                __FILE__, __LINE__, __func__, #cond);                   \
        g_test_fail++;                                                  \
        return -1;                                                      \
    }                                                                   \
    g_test_pass++;                                                      \
} while (0)

#define T_EQ_INT(a, b) do {                                             \
    long _a = (long)(a);                                                \
    long _b = (long)(b);                                                \
    if (_a != _b) {                                                     \
        fprintf(stderr, "FAIL: %s:%d %s: %s (got %ld, expected %ld)\n", \
                __FILE__, __LINE__, __func__, #a " == " #b, _a, _b);    \
        g_test_fail++;                                                  \
        return -1;                                                      \
    }                                                                   \
    g_test_pass++;                                                      \
} while (0)

int run_mac_tests(void);
int run_pool_tests(void);
int run_ring_tests(void);
int run_fdb_tests(void);
int run_switch_tests(void);

#endif
