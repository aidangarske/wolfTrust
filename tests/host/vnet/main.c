/* main.c — host test runner for wolfTrust VNET dataplane primitives.
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

#include <stdio.h>
#include "test_vnet.h"

int g_test_pass = 0;
int g_test_fail = 0;

static int run_vnet_suite(const char* name, int (*test)(void))
{
    int ret = test();

    if (ret == 0) {
        (void)printf("PASS: unit/vnet/%s\n", name);
    }
    else {
        (void)printf("FAIL: unit/vnet/%s\n", name);
    }

    return ret;
}

int main(void)
{
    (void)run_vnet_suite("mac", run_mac_tests);
    (void)run_vnet_suite("pool", run_pool_tests);
    (void)run_vnet_suite("ring", run_ring_tests);
    (void)run_vnet_suite("fdb", run_fdb_tests);
    (void)run_vnet_suite("switch", run_switch_tests);

    fprintf(stderr, "\nvnet host tests: %d checks passed, %d failed\n",
            g_test_pass, g_test_fail);

    return (g_test_fail == 0) ? 0 : 1;
}
