/* spm_gate.h
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

#ifndef WOLFTRUST_SPM_GATE_H
#define WOLFTRUST_SPM_GATE_H

#include "wolftrust/ffm.h"
#include "wolftrust/ffm_domain.h"

/* The single privileged entry point every SP-side psa_* call funnels through.
 * On target a scheduled Secure Partition runs unprivileged on its own stack and
 * traps here via SVC; the handler unmarshals registers into wt_spm_call_t and
 * calls wt_spm_gate on the SPM (MSP_S, privileged) stack. On the host the same
 * gate is called directly, so the dispatch, argument validation, and block
 * classification are one architecture-neutral implementation exercised both
 * ways. WT-FFM-0014. */
typedef enum wt_spm_op {
    WT_SPM_OP_WAIT = 0,
    WT_SPM_OP_GET,
    WT_SPM_OP_SET_RHANDLE,
    WT_SPM_OP_READ,
    WT_SPM_OP_SKIP,
    WT_SPM_OP_WRITE,
    WT_SPM_OP_REPLY,
    WT_SPM_OP_NOTIFY,
    WT_SPM_OP_CLEAR,
    WT_SPM_OP_VERSION,
    WT_SPM_OP_CONNECT,
    WT_SPM_OP_CALL,
    WT_SPM_OP_CLOSE
} wt_spm_op_t;

/* SP-as-client iovec capacity per direction (i003 widens with the NS veneer). */
#define WT_SPM_SP_IOVEC 4U

typedef struct wt_spm_call {
    wt_spm_op_t  op;
    int32_t      partition_id;   /* acting partition (SVC stamps the caller) */
    int32_t      notify_partition; /* NOTIFY target */
    psa_handle_t msg_handle;   /* GET/SET_RHANDLE/READ/SKIP/WRITE/REPLY;
                                * CALL/CLOSE: connection handle */
    psa_signal_t signal_mask;  /* WAIT in */
    uint32_t     timeout;      /* WAIT in: PSA_BLOCK blocks, PSA_POLL returns */
    psa_signal_t signal;       /* GET in */
    uint32_t     vec_idx;      /* READ/SKIP/WRITE in */
    void*        buffer;       /* READ out / WRITE in (SP-domain pointer) */
    size_t       num_bytes;    /* READ/SKIP/WRITE in */
    psa_status_t status;       /* REPLY in */
    psa_msg_t*   msg;          /* GET out (SP-domain pointer) */
    psa_signal_t* asserted;    /* WAIT out (SP-domain pointer) */
    void*        rhandle;      /* SET_RHANDLE in */

    /* SP-as-client IPC (WT-FFM-0014): a partition connecting to another
     * partition's service. The op is issued twice around a block: the first
     * pass validates + enqueues (pending_valid set, NOT_READY returned), the
     * wake pass harvests the completed message. */
    uint32_t     sid;          /* CONNECT/VERSION in */
    uint32_t     version;      /* CONNECT in */
    int32_t      call_type;    /* CALL in */
    psa_invec    sp_in[WT_SPM_SP_IOVEC];   /* CALL in (SP-domain pointers) */
    psa_outvec   sp_out[WT_SPM_SP_IOVEC];  /* CALL in/out (SP-domain pointers) */
    uint8_t      sp_in_len;    /* CALL in */
    uint8_t      sp_out_len;   /* CALL in */
    uint16_t     pending_msg;  /* async resume state (gate-owned) */
    uint8_t      pending_valid;

    psa_status_t ret_status;   /* GET/REPLY/CALL result */
    size_t       ret_size;     /* READ/SKIP result */
    int          ret_int;      /* WAIT/SET_RHANDLE/WRITE/NOTIFY/CLEAR result */
    psa_handle_t ret_handle;   /* CONNECT result */
    uint32_t     ret_version;  /* VERSION result */
} wt_spm_call_t;

/* Run one SPM service request against the runtime. When caller_domain is not
 * NULL, every SP-supplied pointer the op dereferences is bounds-checked against
 * that partition's resolved protection domain first; an out-of-domain pointer
 * fails the op closed rather than letting the privileged SPM touch memory the
 * unprivileged partition could not reach itself. Returns WT_FFM_SUCCESS when
 * the op ran (its PSA-level result is in the call's ret_* fields), or a
 * WT_FFM_ERROR_* code when the request itself was rejected. */
int wt_spm_gate(wt_ffm_runtime_t* runtime,
                const wt_secure_domain_t* caller_domain,
                wt_spm_call_t* call);

/* Non-zero when the just-gated call must suspend its Secure Partition: a
 * psa_wait whose signals are not yet asserted. The coroutine layer blocks the
 * SP on this and re-gates the same wait when a later signal wakes it. */
int wt_spm_call_would_block(const wt_spm_call_t* call);

/* Transport a service loop's request to the gate. The direct transport calls
 * wt_spm_gate in the same privilege context (host tests, inline dispatch);
 * the ARMv8-M port installs an SVC transport so a scheduled unprivileged
 * partition traps to the privileged SPM instead. */
typedef int (*wt_spm_transport_fn)(wt_ffm_runtime_t* runtime,
                                   wt_spm_call_t* call);
int wt_spm_transport_direct(wt_ffm_runtime_t* runtime, wt_spm_call_t* call);

#endif /* WOLFTRUST_SPM_GATE_H */
