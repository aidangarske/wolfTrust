/* vnet_ring.c
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
#include "wolftrust/vnet/vnet_ring.h"
#include "wolftrust/vnet/vnet_errors.h"

void vnet_ring_init(vnet_ring_t *r, vnet_rx_desc_t *storage, uint16_t capacity)
{
    if (r == NULL) return;
    r->buf = storage;
    r->capacity = (storage == NULL) ? 0U : capacity;
    r->head = 0U;
    r->tail = 0U;
    r->count = 0U;
}

int vnet_ring_push(vnet_ring_t *r, const vnet_rx_desc_t *desc)
{
    if (r == NULL || desc == NULL) return WT_VNET_E_BADARG;
    if (r->buf == NULL || r->capacity == 0U) return WT_VNET_E_NOTREADY;
    if (r->count >= r->capacity) return WT_VNET_E_QUEUE_FULL;
    r->buf[r->head] = *desc;
    r->head = (uint16_t)((r->head + 1U) % r->capacity);
    r->count = (uint16_t)(r->count + 1U);
    return WT_VNET_OK;
}

int vnet_ring_pop(vnet_ring_t *r, vnet_rx_desc_t *out)
{
    if (r == NULL || out == NULL) return WT_VNET_E_BADARG;
    if (r->buf == NULL || r->capacity == 0U) return WT_VNET_E_NOTREADY;
    if (r->count == 0U) return WT_VNET_E_EMPTY;
    *out = r->buf[r->tail];
    r->tail = (uint16_t)((r->tail + 1U) % r->capacity);
    r->count = (uint16_t)(r->count - 1U);
    return WT_VNET_OK;
}

int vnet_ring_drop_head(vnet_ring_t *r)
{
    if (r == NULL) return WT_VNET_E_BADARG;
    if (r->buf == NULL || r->capacity == 0U) return WT_VNET_E_NOTREADY;
    if (r->count == 0U) return WT_VNET_E_EMPTY;
    r->tail = (uint16_t)((r->tail + 1U) % r->capacity);
    r->count = (uint16_t)(r->count - 1U);
    return WT_VNET_OK;
}

int vnet_ring_peek(const vnet_ring_t *r, vnet_rx_desc_t *out)
{
    if (r == NULL || out == NULL) return WT_VNET_E_BADARG;
    if (r->buf == NULL || r->capacity == 0U) return WT_VNET_E_NOTREADY;
    if (r->count == 0U) return WT_VNET_E_EMPTY;
    *out = r->buf[r->tail];
    return WT_VNET_OK;
}

uint16_t vnet_ring_count(const vnet_ring_t *r)
{
    if (r == NULL) return 0U;
    return r->count;
}

bool vnet_ring_empty(const vnet_ring_t *r)
{
    if (r == NULL) return true;
    return r->count == 0U;
}

bool vnet_ring_full(const vnet_ring_t *r)
{
    if (r == NULL) return false;
    return r->count >= r->capacity;
}
