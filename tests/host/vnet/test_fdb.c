/* test_fdb.c
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

#include "test_vnet.h"
#include "wolftrust/vnet/vnet_fdb.h"
#include "wolftrust/vnet/vnet_errors.h"

#define FDB_N 4

static vnet_fdb_entry_t g_entries[FDB_N];

static const vnet_mac_t MAC_A = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x0A}};
static const vnet_mac_t MAC_B = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x0B}};
static const vnet_mac_t MAC_C = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x0C}};
static const vnet_mac_t MAC_D = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x0D}};
static const vnet_mac_t MAC_E = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x0E}};
static const vnet_mac_t MAC_UAA = {{0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E}};
static const vnet_mac_t MAC_BCAST = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

static int test_fdb_assign_basic(void)
{
    vnet_fdb_t fdb;
    vnet_mac_t out;
    vnet_fdb_init(&fdb, g_entries, FDB_N);

    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), 1);
    T_CHECK(vnet_fdb_get_assigned(&fdb, 1, &out));
    T_CHECK(vnet_mac_equals(&out, &MAC_A));
    T_CHECK(!vnet_fdb_get_assigned(&fdb, 2, NULL));

    /* Idempotent: re-assigning the same (mac, guest) succeeds. */
    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_A), WT_VNET_OK);
    return 0;
}

static int test_fdb_reject_invalid_assign(void)
{
    vnet_fdb_t fdb;
    vnet_fdb_init(&fdb, g_entries, FDB_N);

    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_BCAST), WT_VNET_E_INVAL_MAC);
    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_UAA), WT_VNET_E_INVAL_MAC);
    T_EQ_INT(vnet_fdb_assign(&fdb, VNET_FDB_NO_VM, &MAC_A), WT_VNET_E_BADARG);
    T_EQ_INT(vnet_fdb_assign(NULL, 1, &MAC_A), WT_VNET_E_BADARG);
    T_EQ_INT(vnet_fdb_assign(&fdb, 1, NULL), WT_VNET_E_BADARG);
    return 0;
}

static int test_fdb_reject_duplicate(void)
{
    vnet_fdb_t fdb;
    vnet_fdb_init(&fdb, g_entries, FDB_N);

    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_A), WT_VNET_OK);
    /* Another guest tries to grab the same MAC. */
    T_EQ_INT(vnet_fdb_assign(&fdb, 2, &MAC_A), WT_VNET_E_DUP_MAC);
    /* Guest 1 tries to assign a second MAC to itself — also rejected. */
    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_B), WT_VNET_E_DUP_MAC);
    return 0;
}

static int test_fdb_release(void)
{
    vnet_fdb_t fdb;
    vnet_fdb_init(&fdb, g_entries, FDB_N);

    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_assign(&fdb, 2, &MAC_B), WT_VNET_OK);

    vnet_fdb_release(&fdb, 1);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), VNET_FDB_NO_VM);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_B), 2);

    /* MAC_A is now free for reassignment. */
    T_EQ_INT(vnet_fdb_assign(&fdb, 3, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), 3);
    return 0;
}

static int test_fdb_learn_basic(void)
{
    vnet_fdb_t fdb;
    vnet_fdb_init(&fdb, g_entries, FDB_N);

    T_EQ_INT(vnet_fdb_learn(&fdb, 5, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), 5);

    /* Same MAC seen later from a different guest — switch updates the
     * LEARNED entry (source moved). */
    T_EQ_INT(vnet_fdb_learn(&fdb, 6, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), 6);

    /* Multicast/zero — learning rejected; lookup returns NO_VM. */
    T_EQ_INT(vnet_fdb_learn(&fdb, 5, &MAC_BCAST), WT_VNET_E_BADARG);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_BCAST), VNET_FDB_NO_VM);
    return 0;
}

static int test_fdb_learn_vs_assigned(void)
{
    vnet_fdb_t fdb;
    vnet_fdb_init(&fdb, g_entries, FDB_N);

    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_A), WT_VNET_OK);

    /* Guest 1 sends a frame with its own MAC — learn is a no-op success. */
    T_EQ_INT(vnet_fdb_learn(&fdb, 1, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), 1);

    /* Guest 2 tries to spoof guest 1's assigned MAC — rejected, table
     * unchanged. */
    T_EQ_INT(vnet_fdb_learn(&fdb, 2, &MAC_A), WT_VNET_E_SPOOF);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), 1);
    return 0;
}

static int test_fdb_capacity_eviction(void)
{
    vnet_fdb_t fdb;
    vnet_fdb_init(&fdb, g_entries, FDB_N);

    /* Fill with 4 LEARNED entries. */
    T_EQ_INT(vnet_fdb_learn(&fdb, 1, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_learn(&fdb, 2, &MAC_B), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_learn(&fdb, 3, &MAC_C), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_learn(&fdb, 4, &MAC_D), WT_VNET_OK);

    /* 5th LEARNED entry — evicts the oldest LEARNED (MAC_A). */
    T_EQ_INT(vnet_fdb_learn(&fdb, 5, &MAC_E), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_E), 5);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), VNET_FDB_NO_VM);

    /* An ASSIGNED entry survives across LEARNED eviction. */
    vnet_fdb_init(&fdb, g_entries, FDB_N);
    T_EQ_INT(vnet_fdb_assign(&fdb, 1, &MAC_A), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_learn(&fdb, 2, &MAC_B), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_learn(&fdb, 3, &MAC_C), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_learn(&fdb, 4, &MAC_D), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_learn(&fdb, 5, &MAC_E), WT_VNET_OK);
    T_EQ_INT(vnet_fdb_lookup(&fdb, &MAC_A), 1);
    return 0;
}

int run_fdb_tests(void)
{
    int rc = 0;
    rc |= test_fdb_assign_basic();
    rc |= test_fdb_reject_invalid_assign();
    rc |= test_fdb_reject_duplicate();
    rc |= test_fdb_release();
    rc |= test_fdb_learn_basic();
    rc |= test_fdb_learn_vs_assigned();
    rc |= test_fdb_capacity_eviction();
    return rc;
}
