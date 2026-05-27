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

int main(void)
{
    int rc = 0;
    rc |= run_mac_tests();
    rc |= run_pool_tests();
    rc |= run_ring_tests();
    rc |= run_fdb_tests();
    rc |= run_switch_tests();

    fprintf(stderr, "\nvnet host tests: %d checks passed, %d failed\n",
            g_test_pass, g_test_fail);

    return (g_test_fail == 0) ? 0 : 1;
}
