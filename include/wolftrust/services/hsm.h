/* hsm.h
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

#ifndef WOLFTRUST_SERVICES_HSM_H
#define WOLFTRUST_SERVICES_HSM_H

#include "wolftrust/types.h"

/* Pull in the wolfHSM comm header for the whTransportServerCb type.
 * This is the minimal wolfHSM dependency; the wolfCrypt settings header
 * must be visible on the include path before this file is processed. */
#include "wolfhsm/wh_comm.h"

/* Initialise the wolfHSM service: wolfCrypt static memory pool,
 * shared NVM (wh_flash_ramsim, volatile), shared lock, the per-guest
 * crypto contexts, but NOT the per-guest transport (that is wired by
 * wt_hsm_guest_init for each guest). Call once at boot, before
 * wt_hsm_guest_init. Returns 0 on success, negative on failure.
 * Failure is fatal — the caller should panic. */
int wt_hsm_init(void);

/* Initialise the per-guest wolfHSM server context, transport, and
 * coroutine. `transport_cb` and `transport_ctx` come from the Wave 4
 * CMSE transport module. After this call the guest's coroutine is
 * RUNNABLE and the next wt_co_tick will start it. Safe to call only
 * from monitor init (before scheduler starts). Returns 0 on success,
 * negative on failure. */
int wt_hsm_guest_init(wt_guest_id_t guest_id,
                      const whTransportServerCb *transport_cb,
                      void *transport_ctx,
                      const void *transport_cfg);

/* Returns true if guest_id has been successfully initialised. Used
 * by the NSC veneers to reject HSM calls from guests that don't
 * have HSM provisioned. */
bool wt_hsm_guest_ready(wt_guest_id_t guest_id);

/* For audit / instrumentation. Returns the wolfHSM client_id value
 * (key namespace) we use for guest_id. Currently this is just
 * guest_id + 1 (0 is reserved). */
uint16_t wt_hsm_guest_client_id(wt_guest_id_t guest_id);

/* Return the coroutine handle for guest_id, or NULL if guest_id is out of
 * range or the guest has not yet been initialised via wt_hsm_guest_init.
 * Used by NSC veneers to wake the per-guest wolfHSM coroutine before ticking
 * the scheduler. */
struct wt_co;
struct wt_co *wt_hsm_guest_coroutine(wt_guest_id_t guest_id);

#endif /* WOLFTRUST_SERVICES_HSM_H */
