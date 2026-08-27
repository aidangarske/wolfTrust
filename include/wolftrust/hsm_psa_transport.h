/* hsm_psa_transport.h
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

#ifndef WOLFTRUST_HSM_PSA_TRANSPORT_H
#define WOLFTRUST_HSM_PSA_TRANSPORT_H

#include <stdint.h>

#include "wolfhsm/wh_settings.h"
#include "wolfhsm/wh_comm.h"

#include "wolftrust/services/hsm_relay.h"

/* SPM-mediated wolfHSM client transport (WT-FFM-0054): every wire packet
 * crosses to the secure side as one synchronous psa_call to SERVICE_HSM —
 * no shared-RAM CSR handshake, no direct-CMSE veneer. Send performs the
 * whole round trip and stashes the response, so Recv completes on the first
 * try and the client's blocking wrappers never spin on NOTREADY. */

typedef struct wt_hsm_psa_transport_cfg {
    uint32_t sid;      /* SERVICE_HSM SID from the platform manifest */
    uint32_t version;  /* service version to connect with */
} wt_hsm_psa_transport_cfg_t;

typedef struct wt_hsm_psa_transport_ctx {
    int32_t handle;
    uint16_t resp_len;
    uint8_t has_resp;
    uint8_t resp[WT_HSM_RELAY_MSG_MAX];
} wt_hsm_psa_transport_ctx_t;

extern const whTransportClientCb wt_hsm_psa_transport_cb;

#endif /* WOLFTRUST_HSM_PSA_TRANSPORT_H */
