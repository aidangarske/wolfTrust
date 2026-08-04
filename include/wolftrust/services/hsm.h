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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WOLFTRUST_SERVICES_HSM_H
#define WOLFTRUST_SERVICES_HSM_H

#include "wolftrust/types.h"

/* Pull in the wolfHSM comm header for the whTransportServerCb type.
 * This is the minimal wolfHSM dependency; the wolfCrypt settings header
 * must be visible on the include path before this file is processed. */
#include "wolfhsm/wh_comm.h"

/* Initialise the wolfHSM service: wolfCrypt static memory pool,
 * target-backed NVM, shared lock, the per-guest crypto contexts, but NOT
 * the per-guest transport (that is wired by wt_hsm_guest_init for each
 * guest). Call once at boot, before wt_hsm_guest_init. Returns 0 on
 * success, negative on failure. Failure is fatal — the caller should panic. */
int wt_hsm_init(void);

/* Initialise the per-guest wolfHSM server context, transport, and
 * tasklet. `transport_cb` and `transport_ctx` come from the CMSE transport
 * module. The tasklet starts blocked and is later scheduled by the monitor as
 * the guest's runnable representative while that guest is waiting on HSM
 * work. Safe to call only from monitor init (before scheduler starts).
 * Returns 0 on success, negative on failure. */
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

/* Return the tasklet handle for guest_id, or NULL if guest_id is out of
 * range or the guest has not yet been initialised via wt_hsm_guest_init.
 * Used by the monitor to wake per-guest wolfHSM service work. */
struct wt_co;
struct wt_co *wt_hsm_guest_tasklet(wt_guest_id_t guest_id);

/* Reverse lookup: which guest owns `tasklet`? Returns WT_MAX_GUESTS if it
 * does not match any per-guest server tasklet. Used by the Secure fault
 * dispatcher to map a faulted tasklet back to its NS client. */
wt_guest_id_t wt_hsm_guest_for_tasklet(const struct wt_co *tasklet);

/* Signal a terminal Secure-side fault for guest_id: drops the NVM lock
 * if the dying tasklet was holding it, writes a WH_ERROR_ABORTED
 * fatal-response into the guest's transport, and clears the ready bit
 * so subsequent NSC veneers reject HSM calls from this guest. Safe to
 * call from handler mode. Returns WH_ERROR_OK on success. */
int wt_hsm_signal_fault(wt_guest_id_t guest_id);

/* Provision or reopen the Initial Attestation Key in the wolfHSM keystore.
 * The private key is non-exportable and restricted to signing. */
int wt_hsm_attest_init(void);

/* Sign a SHA-256 digest with the protected Initial Attestation Key. Output is
 * the 64-byte COSE ECDSA form, r followed by s. */
int wt_hsm_attest_sign(const uint8_t* digest, size_t digestSize,
                       uint8_t* signature, size_t signatureCapacity,
                       size_t* signatureSize);

/* Return the IAK public point in X9.63 form, 0x04 followed by X and Y. */
int wt_hsm_attest_public_key(uint8_t* publicKey, size_t publicKeyCapacity,
                             size_t* publicKeySize);

#endif /* WOLFTRUST_SERVICES_HSM_H */
