/* vnet_fdb.h
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

#ifndef WOLFTRUST_VNET_FDB_H
#define WOLFTRUST_VNET_FDB_H

#include <stdbool.h>
#include <stdint.h>
#include "wolftrust/vnet/vnet_mac.h"

#define VNET_FDB_NO_VM 0xFFFFFFFFu

typedef enum vnet_fdb_kind {
    VNET_FDB_EMPTY    = 0,
    VNET_FDB_ASSIGNED = 1,
    VNET_FDB_LEARNED  = 2
} vnet_fdb_kind_t;

typedef struct vnet_fdb_entry {
    vnet_mac_t mac;
    uint32_t   guest_id;
    uint8_t    kind;
    uint8_t    _pad[3];
} vnet_fdb_entry_t;

typedef struct vnet_fdb {
    vnet_fdb_entry_t *entries;
    uint16_t          capacity;
} vnet_fdb_t;

void vnet_fdb_init(vnet_fdb_t *fdb, vnet_fdb_entry_t *storage, uint16_t capacity);

/* Pin (mac -> guest_id) as ASSIGNED. Fails (<0) if:
 *   - mac is already ASSIGNED to a different guest (WT_VNET_E_DUP_MAC),
 *   - this guest already has a different ASSIGNED mac (WT_VNET_E_DUP_MAC),
 *   - no capacity (WT_VNET_E_NOTREADY).
 * If mac is already ASSIGNED to the same guest, returns OK (idempotent).
 * If mac is currently LEARNED, the entry is promoted to ASSIGNED for the
 * given guest (overwriting any LEARNED owner). */
int vnet_fdb_assign(vnet_fdb_t *fdb, uint32_t guest_id, const vnet_mac_t *mac);

/* Drop the ASSIGNED entry for this guest (if any) and any LEARNED
 * entries that resolve to it. */
void vnet_fdb_release(vnet_fdb_t *fdb, uint32_t guest_id);

/* Record (mac -> guest_id) as LEARNED. If mac is already ASSIGNED to a
 * different guest, returns WT_VNET_E_SPOOF without modifying the table.
 * If ASSIGNED to the same guest, returns OK (no-op). Otherwise inserts
 * or overwrites a LEARNED entry; on a full table, evicts the oldest
 * LEARNED entry. Returns WT_VNET_E_BADARG if mac is multicast/zero. */
int vnet_fdb_learn(vnet_fdb_t *fdb, uint32_t guest_id, const vnet_mac_t *mac);

/* Return the guest_id mapped to mac (ASSIGNED or LEARNED), or
 * VNET_FDB_NO_VM if not present / mac is multicast. */
uint32_t vnet_fdb_lookup(const vnet_fdb_t *fdb, const vnet_mac_t *mac);

/* Get the ASSIGNED mac for a guest. Returns true if found and fills *out;
 * false otherwise. out may be NULL to just probe presence. */
bool vnet_fdb_get_assigned(const vnet_fdb_t *fdb,
                           uint32_t          guest_id,
                           vnet_mac_t       *out);

#endif
