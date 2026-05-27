/* vnet_mac.c
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
#include "wolftrust/vnet/vnet_mac.h"

bool vnet_mac_is_zero(const vnet_mac_t *m)
{
    if (m == NULL) return false;
    return (m->b[0] | m->b[1] | m->b[2] | m->b[3] | m->b[4] | m->b[5]) == 0U;
}

bool vnet_mac_is_broadcast(const vnet_mac_t *m)
{
    if (m == NULL) return false;
    return (m->b[0] & m->b[1] & m->b[2] & m->b[3] & m->b[4] & m->b[5]) == 0xFFU;
}

bool vnet_mac_is_multicast(const vnet_mac_t *m)
{
    if (m == NULL) return false;
    return (m->b[0] & 0x01U) != 0U;
}

bool vnet_mac_is_locally_administered(const vnet_mac_t *m)
{
    if (m == NULL) return false;
    return (m->b[0] & 0x02U) != 0U;
}

bool vnet_mac_is_locally_administered_unicast(const vnet_mac_t *m)
{
    if (m == NULL) return false;
    if (vnet_mac_is_zero(m)) return false;
    if (vnet_mac_is_multicast(m)) return false;
    return vnet_mac_is_locally_administered(m);
}

bool vnet_mac_equals(const vnet_mac_t *a, const vnet_mac_t *b)
{
    uint8_t diff;
    if (a == NULL || b == NULL) return false;
    diff  = (uint8_t)(a->b[0] ^ b->b[0]);
    diff |= (uint8_t)(a->b[1] ^ b->b[1]);
    diff |= (uint8_t)(a->b[2] ^ b->b[2]);
    diff |= (uint8_t)(a->b[3] ^ b->b[3]);
    diff |= (uint8_t)(a->b[4] ^ b->b[4]);
    diff |= (uint8_t)(a->b[5] ^ b->b[5]);
    return diff == 0U;
}

void vnet_mac_copy(vnet_mac_t *dst, const vnet_mac_t *src)
{
    if (dst == NULL || src == NULL) return;
    dst->b[0] = src->b[0];
    dst->b[1] = src->b[1];
    dst->b[2] = src->b[2];
    dst->b[3] = src->b[3];
    dst->b[4] = src->b[4];
    dst->b[5] = src->b[5];
}
