/* vnet_ring.h
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

#ifndef WOLFTRUST_VNET_RING_H
#define WOLFTRUST_VNET_RING_H

#include <stdbool.h>
#include <stdint.h>

typedef struct vnet_rx_desc {
    uint16_t slot;
    uint16_t gen;
    uint16_t len;
    uint16_t flags;
    uint32_t src_vm;
} vnet_rx_desc_t;

typedef struct vnet_ring {
    vnet_rx_desc_t *buf;
    uint16_t        capacity;
    uint16_t        head;
    uint16_t        tail;
    uint16_t        count;
} vnet_ring_t;

void vnet_ring_init(vnet_ring_t *r, vnet_rx_desc_t *storage, uint16_t capacity);
int  vnet_ring_push(vnet_ring_t *r, const vnet_rx_desc_t *desc);
int  vnet_ring_pop(vnet_ring_t *r, vnet_rx_desc_t *out);
int  vnet_ring_drop_head(vnet_ring_t *r);
int  vnet_ring_peek(const vnet_ring_t *r, vnet_rx_desc_t *out);
uint16_t vnet_ring_count(const vnet_ring_t *r);
bool     vnet_ring_empty(const vnet_ring_t *r);
bool     vnet_ring_full(const vnet_ring_t *r);

#endif
