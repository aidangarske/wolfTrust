/* vnet_fdb.c
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
#include "wolftrust/vnet/vnet_fdb.h"
#include "wolftrust/vnet/vnet_errors.h"

static vnet_fdb_entry_t *fdb_find_mac(const vnet_fdb_t *fdb,
                                      const vnet_mac_t *mac)
{
    uint16_t i;
    if (fdb == NULL || fdb->entries == NULL) return NULL;
    for (i = 0U; i < fdb->capacity; ++i) {
        vnet_fdb_entry_t *e = &fdb->entries[i];
        if (e->kind == (uint8_t)VNET_FDB_EMPTY) continue;
        if (vnet_mac_equals(&e->mac, mac)) return e;
    }
    return NULL;
}

static vnet_fdb_entry_t *fdb_find_assigned_by_guest(const vnet_fdb_t *fdb,
                                                    uint32_t          guest_id)
{
    uint16_t i;
    if (fdb == NULL || fdb->entries == NULL) return NULL;
    for (i = 0U; i < fdb->capacity; ++i) {
        vnet_fdb_entry_t *e = &fdb->entries[i];
        if (e->kind != (uint8_t)VNET_FDB_ASSIGNED) continue;
        if (e->guest_id == guest_id) return e;
    }
    return NULL;
}

static vnet_fdb_entry_t *fdb_find_empty(const vnet_fdb_t *fdb)
{
    uint16_t i;
    if (fdb == NULL || fdb->entries == NULL) return NULL;
    for (i = 0U; i < fdb->capacity; ++i) {
        if (fdb->entries[i].kind == (uint8_t)VNET_FDB_EMPTY) {
            return &fdb->entries[i];
        }
    }
    return NULL;
}

static vnet_fdb_entry_t *fdb_evict_oldest_learned(const vnet_fdb_t *fdb)
{
    /* FIFO eviction by table position: with a linear scan and no aging
     * timestamps, the lowest-index LEARNED entry is a reasonable victim.
     * Wave 1 keeps this simple; aging policy is a Wave 6 hardening item. */
    uint16_t i;
    if (fdb == NULL || fdb->entries == NULL) return NULL;
    for (i = 0U; i < fdb->capacity; ++i) {
        if (fdb->entries[i].kind == (uint8_t)VNET_FDB_LEARNED) {
            return &fdb->entries[i];
        }
    }
    return NULL;
}

void vnet_fdb_init(vnet_fdb_t *fdb, vnet_fdb_entry_t *storage, uint16_t capacity)
{
    uint16_t i;
    if (fdb == NULL) return;
    fdb->entries = storage;
    fdb->capacity = (storage == NULL) ? 0U : capacity;
    for (i = 0U; i < fdb->capacity; ++i) {
        fdb->entries[i].kind = (uint8_t)VNET_FDB_EMPTY;
        fdb->entries[i].guest_id = VNET_FDB_NO_VM;
    }
}

int vnet_fdb_assign(vnet_fdb_t *fdb, uint32_t guest_id, const vnet_mac_t *mac)
{
    vnet_fdb_entry_t *e_mac;
    vnet_fdb_entry_t *e_guest;
    vnet_fdb_entry_t *slot;

    if (fdb == NULL || mac == NULL) return WT_VNET_E_BADARG;
    if (guest_id == VNET_FDB_NO_VM) return WT_VNET_E_BADARG;
    if (!vnet_mac_is_locally_administered_unicast(mac)) {
        return WT_VNET_E_INVAL_MAC;
    }

    e_mac = fdb_find_mac(fdb, mac);
    e_guest = fdb_find_assigned_by_guest(fdb, guest_id);

    if (e_mac != NULL && e_mac->kind == (uint8_t)VNET_FDB_ASSIGNED) {
        if (e_mac->guest_id == guest_id) return WT_VNET_OK;
        return WT_VNET_E_DUP_MAC;
    }

    if (e_guest != NULL && (e_mac == NULL || e_mac != e_guest)) {
        /* Guest already owns a different MAC. Caller must release first. */
        return WT_VNET_E_DUP_MAC;
    }

    if (e_mac != NULL) {
        /* Promote existing LEARNED entry. */
        e_mac->kind = (uint8_t)VNET_FDB_ASSIGNED;
        e_mac->guest_id = guest_id;
        return WT_VNET_OK;
    }

    slot = fdb_find_empty(fdb);
    if (slot == NULL) {
        slot = fdb_evict_oldest_learned(fdb);
    }
    if (slot == NULL) return WT_VNET_E_NOTREADY;

    vnet_mac_copy(&slot->mac, mac);
    slot->guest_id = guest_id;
    slot->kind = (uint8_t)VNET_FDB_ASSIGNED;
    return WT_VNET_OK;
}

void vnet_fdb_release(vnet_fdb_t *fdb, uint32_t guest_id)
{
    uint16_t i;
    if (fdb == NULL || fdb->entries == NULL) return;
    for (i = 0U; i < fdb->capacity; ++i) {
        vnet_fdb_entry_t *e = &fdb->entries[i];
        if (e->kind == (uint8_t)VNET_FDB_EMPTY) continue;
        if (e->guest_id != guest_id) continue;
        e->kind = (uint8_t)VNET_FDB_EMPTY;
        e->guest_id = VNET_FDB_NO_VM;
    }
}

int vnet_fdb_learn(vnet_fdb_t *fdb, uint32_t guest_id, const vnet_mac_t *mac)
{
    vnet_fdb_entry_t *e;
    vnet_fdb_entry_t *slot;

    if (fdb == NULL || mac == NULL) return WT_VNET_E_BADARG;
    if (guest_id == VNET_FDB_NO_VM) return WT_VNET_E_BADARG;
    if (vnet_mac_is_multicast(mac) || vnet_mac_is_zero(mac)) {
        return WT_VNET_E_BADARG;
    }

    e = fdb_find_mac(fdb, mac);
    if (e != NULL) {
        if (e->kind == (uint8_t)VNET_FDB_ASSIGNED) {
            if (e->guest_id == guest_id) return WT_VNET_OK;
            return WT_VNET_E_SPOOF;
        }
        e->guest_id = guest_id;
        e->kind = (uint8_t)VNET_FDB_LEARNED;
        return WT_VNET_OK;
    }

    slot = fdb_find_empty(fdb);
    if (slot == NULL) slot = fdb_evict_oldest_learned(fdb);
    if (slot == NULL) return WT_VNET_E_NOTREADY;

    vnet_mac_copy(&slot->mac, mac);
    slot->guest_id = guest_id;
    slot->kind = (uint8_t)VNET_FDB_LEARNED;
    return WT_VNET_OK;
}

uint32_t vnet_fdb_lookup(const vnet_fdb_t *fdb, const vnet_mac_t *mac)
{
    vnet_fdb_entry_t *e;
    if (fdb == NULL || mac == NULL) return VNET_FDB_NO_VM;
    if (vnet_mac_is_multicast(mac) || vnet_mac_is_zero(mac)) {
        return VNET_FDB_NO_VM;
    }
    e = fdb_find_mac(fdb, mac);
    if (e == NULL) return VNET_FDB_NO_VM;
    return e->guest_id;
}

bool vnet_fdb_get_assigned(const vnet_fdb_t *fdb,
                           uint32_t          guest_id,
                           vnet_mac_t       *out)
{
    vnet_fdb_entry_t *e;
    if (fdb == NULL) return false;
    e = fdb_find_assigned_by_guest(fdb, guest_id);
    if (e == NULL) return false;
    if (out != NULL) vnet_mac_copy(out, &e->mac);
    return true;
}
