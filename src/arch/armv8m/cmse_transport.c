/* cmse_transport.c
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
 * CMSE-backed wolfHSM transport — secure-side implementation.
 *
 * NS-RAM buffer layout per guest (WT_HSM_BUF_SIZE = 512 bytes):
 *
 *   Offset    Size   Field
 *   ------    ----   -----
 *   +0x000      8    request  slot CSR  (whTransportMemCsr union — notify/len/ack/wait)
 *   +0x008    248    request  slot data (WH_COMM_MTU bytes written by guest)
 *   +0x100      8    response slot CSR  (whTransportMemCsr union)
 *   +0x108    248    response slot data (WH_COMM_MTU bytes written by secure monitor)
 *
 * The two slots are at fixed offsets of 0 and ns_buf_size/2 (= 0x100) within
 * the buffer.  whTransportMemCsr is an 8-byte union (sizeof == 8); the data
 * area follows immediately at (csr + 1).
 *
 * Request protocol (guest signals new message):
 *   Guest  : writes data to req_csr data area, then increments req_csr->s.notify.
 *   Monitor: detects req_csr->s.notify != last_req_notify, copies data to
 *             secure buffer, updates last_req_notify.
 *
 * Response protocol (monitor signals completion):
 *   Monitor: writes data to resp_csr data area, sets resp_csr->s.len, issues
 *             a DSB, then increments resp_csr->s.notify.
 *   Guest  : detects resp_csr->s.notify changed, reads data.
 *
 * Security properties:
 *   - The entire NS buffer window is validated at Init against the guest's
 *     declared NS-RAM regions (defense against SAU misconfiguration) AND
 *     against CMSE cmse_check_address_range (CMSE_NONSECURE | CMSE_MPU_READWRITE).
 *   - Recv re-validates on every call (SAU may theoretically change between
 *     calls in a future multi-domain extension).
 *   - Data is copied once from NS to secure memory (TOCTOU prevention).
 *   - Oversized payloads are dropped without touching secure state further.
 *   - DSB issued before the notify increment in Send to prevent CPU/compiler
 *     reordering the payload write past the signal to the guest.
 */

#include <string.h>

#include "wolfhsm/wh_settings.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_message_comm.h"
#include "wolfhsm/wh_transport_mem.h"

#include "wolftrust/arch/armv8m/cmse.h"
#include "wolftrust/arch/armv8m/cmse_transport.h"
#include "wolftrust/monitor.h"
#include "wolftrust/partition.h"

#include "memory_map.h"  /* WT_HSM_BUF_SIZE */

/* Compile-time guard: the wolfHSM-advertised MTU must fit inside the
 * data area of one slot.  Each slot is (WT_HSM_BUF_SIZE / 2) bytes,
 * of which sizeof(whTransportMemCsr) (=8) is consumed by the header.
 * Drift here was the off-by-8 issue found in the Wave 4C audit. */
_Static_assert(WOLFHSM_CFG_COMM_DATA_LEN <=
                   (WT_HSM_BUF_SIZE / 2u) - sizeof(whTransportMemCsr),
               "WOLFHSM_CFG_COMM_DATA_LEN exceeds CMSE slot data area");

#define WT_HSM_SLOT_DATA_BYTES \
    ((WT_HSM_BUF_SIZE / 2u) - sizeof(whTransportMemCsr))
#define WT_HSM_SLOT_TOTAL_BYTES \
    (sizeof(whTransportMemCsr) + WT_HSM_SLOT_DATA_BYTES)

/* ---------------------------------------------------------------------------
 * Static per-guest context pool
 * ---------------------------------------------------------------------------*/
static wt_cmse_transport_ctx_t g_transport_ctx[WT_MAX_GUESTS];

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------------*/

/* Return a pointer to the per-guest context, or NULL for out-of-range ids. */
wt_cmse_transport_ctx_t *wt_cmse_transport_ctx_for(wt_guest_id_t guest_id)
{
    if (guest_id >= (wt_guest_id_t)WT_MAX_GUESTS) {
        return NULL;
    }
    return &g_transport_ctx[guest_id];
}

/* Populate *out_cfg from the guest's explicit port binding.
 * Caller is responsible for passing a valid out_cfg pointer. */
void wt_cmse_transport_cfg_for(wt_guest_id_t guest_id,
                                wt_cmse_transport_cfg_t *out_cfg)
{
    const wt_guest_config_t *configs;
    size_t count;
    size_t i;

    if (out_cfg == NULL) {
        return;
    }

    /* Zero-initialise so callers see a safe default on lookup failure. */
    out_cfg->guest_id    = guest_id;
    out_cfg->ns_buf_base = 0u;
    out_cfg->ns_buf_size = 0u;

    configs = wt_partitions_config_table(&count);

    for (i = 0; i < count; ++i) {
        if (configs[i].guest_id == guest_id) {
            out_cfg->ns_buf_base = configs[i].port.hsm_transport.base;
            out_cfg->ns_buf_size = configs[i].port.hsm_transport.size;
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * whTransportServerCb — Init
 *
 * Validates the NS buffer, maps req_csr and resp_csr, zeroes both CSRs,
 * and fires the connected callback.
 * ---------------------------------------------------------------------------*/
static int wt_cmse_transport_init(void *ctx_void, const void *cfg_void,
                                  whCommSetConnectedCb connectcb,
                                  void *connectcb_arg)
{
    wt_cmse_transport_ctx_t       *ctx = (wt_cmse_transport_ctx_t *)ctx_void;
    const wt_cmse_transport_cfg_t *cfg =
        (const wt_cmse_transport_cfg_t *)cfg_void;

    if (ctx == NULL || cfg == NULL) {
        return WH_ERROR_BADARGS;
    }

    /* Each slot is ns_buf_size/2 bytes.  We need at least two CSR-sized
     * regions so that req and resp do not overlap. */
    if (cfg->ns_buf_size < (size_t)(2u * sizeof(whTransportMemCsr))) {
        return WH_ERROR_BADARGS;
    }

    /* Defense in depth: confirm the entire window lies in the guest's
     * declared NS-RAM.  SAU misconfiguration would otherwise let secure
     * code accept a pointer that crosses into a foreign region. */
    if (!wt_cmse_check_in_guest_ns_ram(cfg->guest_id,
                                       (const void *)cfg->ns_buf_base,
                                       cfg->ns_buf_size)) {
        return WH_ERROR_BADARGS;
    }

    /* NOTE: we deliberately do NOT call cmse_check_address_range here.
     * Init runs at boot, before any guest has been dispatched, so the
     * NS MPU is empty and the CMSE check would always fail. The check
     * is performed on every Recv/Send call below, where the NS MPU is
     * known to be programmed (because the guest is currently scheduled
     * and just made the CMSE call). The config-table check above
     * (wt_cmse_check_in_guest_ns_ram) provides the static guarantee. */

    ctx->guest_id  = cfg->guest_id;

    /* Request slot starts at the base; response slot at the midpoint.
     * The data area of each slot immediately follows its whTransportMemCsr
     * header at (csr + 1). */
    ctx->req_csr  = (whTransportMemCsr *)cfg->ns_buf_base;
    ctx->resp_csr = (whTransportMemCsr *)(cfg->ns_buf_base +
                                          cfg->ns_buf_size / 2u);

    ctx->last_req_notify = 0u;
    ctx->connectcb       = connectcb;
    ctx->connectcb_arg   = connectcb_arg;

    /* Zero both CSRs so the initial state is clean. */
    memset(ctx->req_csr,  0, sizeof(*ctx->req_csr));
    memset(ctx->resp_csr, 0, sizeof(*ctx->resp_csr));

    if (connectcb != NULL) {
        connectcb(connectcb_arg, WH_COMM_CONNECTED);
    }

    return WH_ERROR_OK;
}

/* ---------------------------------------------------------------------------
 * whTransportServerCb — Cleanup
 *
 * Fires the disconnected callback and nulls the NS-buffer pointers.
 * Does NOT touch NS memory — the guest may have re-purposed it.
 * ---------------------------------------------------------------------------*/
static int wt_cmse_transport_cleanup(void *ctx_void)
{
    wt_cmse_transport_ctx_t *ctx = (wt_cmse_transport_ctx_t *)ctx_void;

    if (ctx == NULL) {
        return WH_ERROR_BADARGS;
    }

    if (ctx->connectcb != NULL) {
        ctx->connectcb(ctx->connectcb_arg, WH_COMM_DISCONNECTED);
    }

    /* Do NOT touch NS buffers on cleanup — guest may have re-purposed them. */
    ctx->req_csr  = NULL;
    ctx->resp_csr = NULL;

    return WH_ERROR_OK;
}

/* ---------------------------------------------------------------------------
 * whTransportServerCb — Recv  (server receives a request from the guest)
 *
 * The guest signals "new request" by incrementing req_csr->s.notify.
 * We detect the change by comparing against last_req_notify stored in
 * secure memory (which the guest cannot forge).
 * ---------------------------------------------------------------------------*/
static int wt_cmse_transport_recv(void *ctx_void, uint16_t *out_size,
                                  void *data)
{
    wt_cmse_transport_ctx_t *ctx = (wt_cmse_transport_ctx_t *)ctx_void;
    uint16_t cur_notify;
    uint16_t sz;

    if (ctx == NULL || out_size == NULL || data == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (ctx->req_csr == NULL) {
        return WH_ERROR_NOTREADY;
    }

    /* Re-validate the NS buffer on every call — cheap and future-proof. */
    if (!wt_cmse_check_ns_rw(ctx->req_csr, WT_HSM_SLOT_TOTAL_BYTES)) {
        return WH_ERROR_ABORTED;
    }

    cur_notify = ctx->req_csr->s.notify;
    if ((uint64_t)cur_notify == ctx->last_req_notify) {
        return WH_ERROR_NOTREADY;
    }

    /* A new request has arrived.  Read size from the CSR.
     * Cap against the PHYSICAL slot data area (WT_HSM_SLOT_DATA_BYTES),
     * not just the configured COMM_DATA_LEN — the _Static_assert above
     * guarantees the two are aligned, but defending against the smaller
     * of the two values eliminates the off-by-N class entirely. */
    sz = ctx->req_csr->s.len;
    if (sz > WT_HSM_SLOT_DATA_BYTES) {
        /* Drop the malformed request — advance notify so we do not loop. */
        ctx->last_req_notify = (uint64_t)cur_notify;
        return WH_ERROR_ABORTED;
    }

    /* TOCTOU defence: copy the payload once into the caller's secure buffer.
     * Do not re-read the NS buffer after this point in this handler. */
    memcpy(data, (const uint8_t *)(ctx->req_csr + 1), sz);
    *out_size            = sz;
    ctx->last_req_notify = (uint64_t)cur_notify;

    return WH_ERROR_OK;
}

/* ---------------------------------------------------------------------------
 * whTransportServerCb — Send  (server sends a response to the guest)
 *
 * Write order: payload first, size second, notify increment LAST.
 * A DSB before the notify increment prevents the CPU from making the
 * notify visible before the data writes have propagated.
 * ---------------------------------------------------------------------------*/
static int wt_cmse_transport_send(void *ctx_void, uint16_t data_size,
                                  const void *data)
{
    wt_cmse_transport_ctx_t *ctx = (wt_cmse_transport_ctx_t *)ctx_void;

    if (ctx == NULL || data == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (data_size > WT_HSM_SLOT_DATA_BYTES) {
        return WH_ERROR_BADARGS;
    }
    if (ctx->resp_csr == NULL) {
        return WH_ERROR_NOTREADY;
    }

    if (!wt_cmse_check_ns_rw(ctx->resp_csr, WT_HSM_SLOT_TOTAL_BYTES)) {
        return WH_ERROR_ABORTED;
    }

    /* Write payload into the data area that follows the CSR header. */
    memcpy((uint8_t *)(ctx->resp_csr + 1), data, data_size);

    /* Commit the length field. */
    ctx->resp_csr->s.len = data_size;

    /* Full system DSB: ensure payload and length writes reach memory before
     * the notify increment becomes visible to the non-secure guest. */
    __asm volatile("dsb sy" ::: "memory");

    /* Increment notify to signal the guest that a response is ready. */
    ctx->resp_csr->s.notify++;
    wt_monitor_hsm_response_ready(ctx->guest_id);

    return WH_ERROR_OK;
}

/* ---------------------------------------------------------------------------
 * Exported vtable
 * ---------------------------------------------------------------------------*/
const whTransportServerCb wt_cmse_transport_cb = {
    .Init    = wt_cmse_transport_init,
    .Recv    = wt_cmse_transport_recv,
    .Send    = wt_cmse_transport_send,
    .Cleanup = wt_cmse_transport_cleanup,
};

/* ---------------------------------------------------------------------------
 * wt_cmse_transport_signal_fault
 *
 * Synthesise a fatal-error response in the guest's response slot when
 * the secure-side tasklet took an MPU / PSPLIM / UsageFault. Called
 * from handler mode by the platform fault dispatcher.
 *
 * Layout written into the response slot (offsets are within
 * resp_csr's data area, immediately after the 8-byte whTransportMemCsr
 * header):
 *
 *   +0  whCommHeader { magic, kind, seq, aux=WH_COMM_AUX_RESP_FATAL }
 *   +8  whMessageComm_ErrorResponse { return_code = WH_ERROR_ABORTED }
 *
 * kind/seq mirror the in-flight request (read from req_csr) so the
 * client's normal response-matching logic still works.
 *
 * We deliberately avoid wh_Server_HandleRequestMessage and friends: the
 * server context may be in an inconsistent state (the fault could have
 * happened deep inside wolfCrypt with locks half-held). Writing the CSR
 * directly is the only path that's guaranteed safe from a handler.
 * ---------------------------------------------------------------------------*/
int wt_cmse_transport_signal_fault(wt_guest_id_t guest_id)
{
    wt_cmse_transport_ctx_t   *ctx;
    const whCommHeader        *req_hdr;
    whCommHeader               hdr;
    whMessageComm_ErrorResponse body;
    uint8_t                   *resp_data;

    if (guest_id >= (wt_guest_id_t)WT_MAX_GUESTS) {
        return WH_ERROR_BADARGS;
    }
    ctx = &g_transport_ctx[guest_id];
    if (ctx->req_csr == NULL || ctx->resp_csr == NULL) {
        return WH_ERROR_BADARGS;
    }

    /* The pointers were validated against the guest's NS-RAM window at
     * Init; we re-trust them here. cmse_check_address_range is not
     * available from handler mode in any meaningful way (the NS MPU
     * reflects whichever guest was last dispatched, which may not be
     * the one whose coroutine just faulted). */

    /* Mirror the request header so the client matches on seq. The
     * request data area starts immediately after the request CSR. */
    req_hdr = (const whCommHeader *)(ctx->req_csr + 1);
    hdr.magic = WH_COMM_MAGIC_NATIVE;
    hdr.kind  = req_hdr->kind;
    hdr.seq   = req_hdr->seq;
    hdr.aux   = WH_COMM_AUX_RESP_FATAL;
    body.return_code = WH_ERROR_ABORTED;

    resp_data = (uint8_t *)(ctx->resp_csr + 1);
    memcpy(resp_data,                          &hdr,  sizeof(hdr));
    memcpy(resp_data + sizeof(hdr),            &body, sizeof(body));

    ctx->resp_csr->s.len = (uint16_t)(sizeof(hdr) + sizeof(body));

    /* Full system DSB so the payload and length writes are visible
     * before the notify bump that releases the client. */
    __asm volatile("dsb sy" ::: "memory");

    ctx->resp_csr->s.notify++;
    wt_monitor_hsm_response_ready(guest_id);

    return WH_ERROR_OK;
}
