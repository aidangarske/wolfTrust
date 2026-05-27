/* vnet_pool.h
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

#ifndef WOLFTRUST_VNET_POOL_H
#define WOLFTRUST_VNET_POOL_H

#include <stdbool.h>
#include <stdint.h>
#include "wolftrust/vnet/vnet_config.h"

typedef struct vnet_frame {
    uint16_t len;
    uint16_t gen;
    uint16_t refcnt;
    uint16_t flags;
    uint32_t src_vm;
    uint32_t dst_mask;
    uint32_t born_tick;
    uint8_t  data[WT_VNET_FRAME_MAX];
} vnet_frame_t;

typedef struct vnet_pool {
    vnet_frame_t *slots;
    uint16_t      slot_count;
    uint16_t      in_use_count;
} vnet_pool_t;

#define VNET_POOL_BAD_SLOT 0xFFFFU

void vnet_pool_init(vnet_pool_t *p, vnet_frame_t *storage, uint16_t count);

/* Returns slot index on success, VNET_POOL_BAD_SLOT if pool is full.
 * Bumps gen, sets refcnt=1, stores born_tick. Caller fills len/flags/etc. */
uint16_t vnet_pool_alloc(vnet_pool_t *p, uint32_t now_tick);

/* Increment refcnt for an additional destination. Returns 0 on success,
 * negative if slot/gen mismatch or slot is free. */
int vnet_pool_ref(vnet_pool_t *p, uint16_t slot, uint16_t gen);

/* Decrement refcnt; free if it reaches zero. Returns
 *   1 if slot was freed,
 *   0 if slot is still referenced,
 *  <0 on stale cookie / double release / out of range. */
int vnet_pool_release(vnet_pool_t *p, uint16_t slot, uint16_t gen);

/* Force-free regardless of refcnt. Used by the timeout reaper.
 * Returns 0 if slot was in use and is now freed, <0 if already free or OOR. */
int vnet_pool_force_free(vnet_pool_t *p, uint16_t slot);

/* Iterate the pool and force-free any slot whose born_tick is older
 * than (now_tick - timeout_ticks). Returns count of slots freed. */
uint16_t vnet_pool_drop_expired(vnet_pool_t *p,
                                uint32_t      now_tick,
                                uint32_t      timeout_ticks);

/* Returns pointer to the slot only if slot is in use AND gen matches.
 * Returns NULL otherwise. */
vnet_frame_t *vnet_pool_slot(vnet_pool_t *p, uint16_t slot, uint16_t gen);

uint16_t vnet_pool_free_count(const vnet_pool_t *p);
uint16_t vnet_pool_in_use_count(const vnet_pool_t *p);

#endif
