/* test_mac.c
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
#include "wolftrust/vnet/vnet_mac.h"

static const vnet_mac_t MAC_ZERO       = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
static const vnet_mac_t MAC_BCAST      = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
static const vnet_mac_t MAC_MCAST_ARB  = {{0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}};
static const vnet_mac_t MAC_UAA_UNI    = {{0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E}};
static const vnet_mac_t MAC_LAA_UNI_A  = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}};
static const vnet_mac_t MAC_LAA_UNI_B  = {{0x06, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE}};
static const vnet_mac_t MAC_LAA_MULTI  = {{0x03, 0x00, 0x00, 0x00, 0x00, 0x01}};

static int test_mac_predicates(void)
{
    T_CHECK(vnet_mac_is_zero(&MAC_ZERO));
    T_CHECK(!vnet_mac_is_zero(&MAC_BCAST));
    T_CHECK(!vnet_mac_is_zero(&MAC_LAA_UNI_A));

    T_CHECK(vnet_mac_is_broadcast(&MAC_BCAST));
    T_CHECK(!vnet_mac_is_broadcast(&MAC_ZERO));
    T_CHECK(!vnet_mac_is_broadcast(&MAC_LAA_UNI_A));

    T_CHECK(vnet_mac_is_multicast(&MAC_BCAST));
    T_CHECK(vnet_mac_is_multicast(&MAC_MCAST_ARB));
    T_CHECK(vnet_mac_is_multicast(&MAC_LAA_MULTI));
    T_CHECK(!vnet_mac_is_multicast(&MAC_LAA_UNI_A));
    T_CHECK(!vnet_mac_is_multicast(&MAC_UAA_UNI));

    T_CHECK(vnet_mac_is_locally_administered(&MAC_LAA_UNI_A));
    T_CHECK(vnet_mac_is_locally_administered(&MAC_LAA_UNI_B));
    T_CHECK(!vnet_mac_is_locally_administered(&MAC_UAA_UNI));
    return 0;
}

static int test_mac_laa_unicast(void)
{
    T_CHECK(vnet_mac_is_locally_administered_unicast(&MAC_LAA_UNI_A));
    T_CHECK(vnet_mac_is_locally_administered_unicast(&MAC_LAA_UNI_B));
    T_CHECK(!vnet_mac_is_locally_administered_unicast(&MAC_UAA_UNI));
    T_CHECK(!vnet_mac_is_locally_administered_unicast(&MAC_BCAST));
    T_CHECK(!vnet_mac_is_locally_administered_unicast(&MAC_LAA_MULTI));
    T_CHECK(!vnet_mac_is_locally_administered_unicast(&MAC_ZERO));
    return 0;
}

static int test_mac_equals_copy(void)
{
    vnet_mac_t buf;
    T_CHECK(vnet_mac_equals(&MAC_LAA_UNI_A, &MAC_LAA_UNI_A));
    T_CHECK(!vnet_mac_equals(&MAC_LAA_UNI_A, &MAC_LAA_UNI_B));

    vnet_mac_copy(&buf, &MAC_LAA_UNI_B);
    T_CHECK(vnet_mac_equals(&buf, &MAC_LAA_UNI_B));
    T_CHECK(!vnet_mac_equals(&buf, &MAC_LAA_UNI_A));
    return 0;
}

static int test_mac_null_safety(void)
{
    T_CHECK(!vnet_mac_is_zero(NULL));
    T_CHECK(!vnet_mac_is_broadcast(NULL));
    T_CHECK(!vnet_mac_is_multicast(NULL));
    T_CHECK(!vnet_mac_is_locally_administered(NULL));
    T_CHECK(!vnet_mac_is_locally_administered_unicast(NULL));
    T_CHECK(!vnet_mac_equals(NULL, &MAC_LAA_UNI_A));
    T_CHECK(!vnet_mac_equals(&MAC_LAA_UNI_A, NULL));
    vnet_mac_copy(NULL, &MAC_LAA_UNI_A);
    vnet_mac_copy(NULL, NULL);
    return 0;
}

int run_mac_tests(void)
{
    int rc = 0;
    rc |= test_mac_predicates();
    rc |= test_mac_laa_unicast();
    rc |= test_mac_equals_copy();
    rc |= test_mac_null_safety();
    return rc;
}
