/* vnet_mac.h
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

#ifndef WOLFTRUST_VNET_MAC_H
#define WOLFTRUST_VNET_MAC_H

#include <stdbool.h>
#include <stdint.h>

#define VNET_MAC_LEN 6U

typedef struct vnet_mac {
    uint8_t b[VNET_MAC_LEN];
} vnet_mac_t;

bool vnet_mac_is_zero(const vnet_mac_t *m);
bool vnet_mac_is_broadcast(const vnet_mac_t *m);
bool vnet_mac_is_multicast(const vnet_mac_t *m);
bool vnet_mac_is_locally_administered(const vnet_mac_t *m);
bool vnet_mac_is_locally_administered_unicast(const vnet_mac_t *m);
bool vnet_mac_equals(const vnet_mac_t *a, const vnet_mac_t *b);
void vnet_mac_copy(vnet_mac_t *dst, const vnet_mac_t *src);

#endif
