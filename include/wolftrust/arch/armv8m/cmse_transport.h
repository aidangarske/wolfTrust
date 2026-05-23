/* cmse_transport.h
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

/*
 * CMSE-backed wolfHSM transport — secure side declarations.
 *
 * Each guest owns a 512-byte NS-RAM window, split into two 256-byte slots:
 *   [+0x000 .. +0x0FF]  request  slot: whTransportMemCsr header + data
 *   [+0x100 .. +0x1FF]  response slot: whTransportMemCsr header + data
 *
 * The secure monitor validates every access against the guest's declared
 * NS-RAM window AND via ARMv8-M CMSE (cmse_check_address_range).
 */

#ifndef WOLFTRUST_ARCH_ARMV8M_CMSE_TRANSPORT_H
#define WOLFTRUST_ARCH_ARMV8M_CMSE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_transport_mem.h"
#include "wolftrust/types.h"

/* ---------------------------------------------------------------------------
 * Per-guest init configuration — passed to wt_cmse_transport_init.
 * ns_buf_base and ns_buf_size are filled from wt_guest_config_t.hsm_transport
 * by wt_cmse_transport_cfg_for().
 * ---------------------------------------------------------------------------*/
typedef struct wt_cmse_transport_cfg {
    wt_guest_id_t guest_id;
    uintptr_t     ns_buf_base;   /* WT_GUESTn_HSM_BUF_BASE */
    size_t        ns_buf_size;   /* WT_HSM_BUF_SIZE = 512 */
} wt_cmse_transport_cfg_t;

/* ---------------------------------------------------------------------------
 * Per-guest transport context — one instance per guest in static storage.
 * ---------------------------------------------------------------------------*/
typedef struct wt_cmse_transport_ctx {
    wt_guest_id_t guest_id;
    /* Pointers into the guest's NS RAM, validated once at Init. */
    whTransportMemCsr       *req_csr;
    whTransportMemCsr       *resp_csr;
    /* Last request notify value we have processed.  Compare incoming
     * req_csr->s.notify to detect new requests; if equal, return NOTREADY. */
    uint64_t                 last_req_notify;
    /* Connected-callback (used by wolfHSM when transport state changes). */
    whCommSetConnectedCb     connectcb;
    void                    *connectcb_arg;
} wt_cmse_transport_ctx_t;

/* ---------------------------------------------------------------------------
 * Exported vtable — pass as transport_cb to wt_hsm_guest_init.
 * ---------------------------------------------------------------------------*/
extern const whTransportServerCb wt_cmse_transport_cb;

/* Return a pointer to the per-guest context (static storage), or NULL if
 * guest_id is out of range. */
wt_cmse_transport_ctx_t *wt_cmse_transport_ctx_for(wt_guest_id_t guest_id);

/* Populate *out_cfg from the guest's wt_guest_config_t.hsm_transport so
 * that the platform init code can call wt_cmse_transport_init without
 * embedding platform addresses directly. */
void wt_cmse_transport_cfg_for(wt_guest_id_t guest_id,
                                wt_cmse_transport_cfg_t *out_cfg);

#endif /* WOLFTRUST_ARCH_ARMV8M_CMSE_TRANSPORT_H */
