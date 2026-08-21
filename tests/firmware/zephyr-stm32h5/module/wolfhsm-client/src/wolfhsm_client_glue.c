/* wolfhsm_client_glue.c
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

/* wolfHSM client + CMSE-transport glue for the wolfTrust Zephyr non-secure
 * guest. Ported from tests/firmware/stm32h563/nonsecure/wolfhsm_client_glue.c
 * with two Zephyr-specific changes:
 *
 *  - the shared transport CSR base is resolved from the WOLFTRUST_HSM_TRANSPORT
 *    device-tree memory-region (defined by the nucleo_h563zi /ns board variant)
 *    rather than a baremetal linker symbol.
 *
 *  - the local bump allocator and wc_GenerateSeed override that the baremetal
 *    guest carries to fill in for the absence of libc are dropped: Zephyr's
 *    libc heap and entropy driver own those symbols. The RNG stub is kept
 *    because user_settings.h still maps CUSTOM_RAND_GENERATE_BLOCK to it.
 *
 * Buffer layout per guest (WT_HSM_BUF_SIZE = 768 bytes):
 *   +0x000   8 B   request  CSR  (whTransportMemCsr)
 *   +0x008 376 B   request  data
 *   +0x180   8 B   response CSR  (whTransportMemCsr)
 *   +0x188 376 B   response data
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/devicetree.h>

#include "wolfhsm/wh_settings.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_transport_mem.h"
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_client_crypto.h"
#include "wolfhsm/wh_client_cryptocb.h"

#include "wolfssl/wolfcrypt/cryptocb.h"

/* A wolfHSM message on the wire is a whCommHeader followed by up to
 * WOLFHSM_CFG_COMM_DATA_LEN payload bytes, and each transport slot is sized to
 * hold exactly that. Bound transfers by the whole packet, not the payload
 * alone, or a max-size response is wrongly rejected as malformed. */
#define WT_HSM_MAX_PACKET_SZ \
    (sizeof(whCommHeader) + (size_t)WOLFHSM_CFG_COMM_DATA_LEN)

/* The CMSE transport region carved out of guest-a SRAM by the board /ns
 * variant. DT_REG_ADDR resolves to 0x20000100 and DT_REG_SIZE to 0x200. */
#define WT_HSM_TRANSPORT_NODE DT_NODELABEL(wolftrust_hsm_transport)
#define WT_HSM_BUF_BASE       ((uintptr_t)DT_REG_ADDR(WT_HSM_TRANSPORT_NODE))
#define WT_HSM_BUF_SIZE       ((size_t)  DT_REG_SIZE(WT_HSM_TRANSPORT_NODE))

/* NSC veneers, resolved via WOLFTRUST_CMSE_IMPLIB at link time. */
extern int  WolfTrust_HSM_Submit(uint16_t size);
extern int  WolfTrust_HSM_Poll  (uint16_t seq);
extern int  WolfTrust_HSM_Cancel(uint16_t seq);

typedef struct wt_guest_transport_ctx {
    whTransportMemCsr   *req_csr;
    whTransportMemCsr   *resp_csr;
    uint64_t             last_resp_notify;
    whCommSetConnectedCb connectcb;
    void                *connectcb_arg;
} wt_guest_transport_ctx_t;

static wt_guest_transport_ctx_t g_guest_tx;

static int guest_tx_init(void *ctx_v, const void *cfg_v,
                         whCommSetConnectedCb connectcb, void *connectcb_arg)
{
    wt_guest_transport_ctx_t *ctx = (wt_guest_transport_ctx_t *)ctx_v;

    (void)cfg_v;

    if (ctx == NULL) {
        return WH_ERROR_BADARGS;
    }

    ctx->req_csr  = (whTransportMemCsr *)WT_HSM_BUF_BASE;
    ctx->resp_csr = (whTransportMemCsr *)(WT_HSM_BUF_BASE +
                                           (WT_HSM_BUF_SIZE / 2u));

    /* Baseline whatever's already in the response slot before we start
     * polling so a stale notify from a prior boot doesn't fake a reply. */
    ctx->last_resp_notify = ctx->resp_csr->u64;

    ctx->connectcb     = connectcb;
    ctx->connectcb_arg = connectcb_arg;

    if (connectcb != NULL) {
        connectcb(connectcb_arg, WH_COMM_CONNECTED);
    }

    return WH_ERROR_OK;
}

static int guest_tx_cleanup(void *ctx_v)
{
    wt_guest_transport_ctx_t *ctx = (wt_guest_transport_ctx_t *)ctx_v;

    if (ctx == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (ctx->connectcb != NULL) {
        ctx->connectcb(ctx->connectcb_arg, WH_COMM_DISCONNECTED);
    }
    ctx->req_csr  = NULL;
    ctx->resp_csr = NULL;
    return WH_ERROR_OK;
}

static int guest_tx_send(void *ctx_v, uint16_t data_size, const void *data)
{
    wt_guest_transport_ctx_t *ctx = (wt_guest_transport_ctx_t *)ctx_v;

    if (ctx == NULL || data == NULL || ctx->req_csr == NULL) {
        return WH_ERROR_BADARGS;
    }
    if (data_size > WT_HSM_MAX_PACKET_SZ) {
        return WH_ERROR_BADARGS;
    }

    memcpy((uint8_t *)(ctx->req_csr + 1), data, data_size);

    ctx->req_csr->s.len = data_size;
    __asm volatile ("dsb sy" ::: "memory");
    ctx->req_csr->s.notify++;

    return WolfTrust_HSM_Submit(data_size);
}

static int guest_tx_recv(void *ctx_v, uint16_t *out_size, void *data)
{
    wt_guest_transport_ctx_t *ctx = (wt_guest_transport_ctx_t *)ctx_v;
    uint64_t cur;
    uint16_t sz;

    if (ctx == NULL || out_size == NULL || data == NULL ||
        ctx->resp_csr == NULL) {
        return WH_ERROR_BADARGS;
    }

    (void)WolfTrust_HSM_Poll(0);

    cur = ctx->resp_csr->u64;
    if (cur == ctx->last_resp_notify) {
        /* Let the secure side schedule its HSM tasklet instead of
         * hot-spinning across the CMSE boundary. */
        __asm volatile ("wfi");
        return WH_ERROR_NOTREADY;
    }

    sz = ctx->resp_csr->s.len;
    if (sz > WT_HSM_MAX_PACKET_SZ) {
        ctx->last_resp_notify = cur;
        return WH_ERROR_ABORTED;
    }

    memcpy(data, (const uint8_t *)(ctx->resp_csr + 1), sz);
    *out_size             = sz;
    ctx->last_resp_notify = cur;

    return WH_ERROR_OK;
}

static const whTransportClientCb g_guest_transport_cb = {
    .Init    = guest_tx_init,
    .Send    = guest_tx_send,
    .Recv    = guest_tx_recv,
    .Cleanup = guest_tx_cleanup,
};

static whClientContext    g_client_ctx;
static whClientConfig     g_client_cfg;
static whCommClientConfig g_comm_cfg;
static int                g_client_ready;

int wolfhsm_guest_init(void)
{
    int rc;

    g_client_ready = 0;

    g_comm_cfg.transport_cb      = &g_guest_transport_cb;
    g_comm_cfg.transport_context = &g_guest_tx;
    g_comm_cfg.transport_config  = NULL;
    g_comm_cfg.client_id         = 0u;

    g_client_cfg.comm = &g_comm_cfg;

    rc = wh_Client_Init(&g_client_ctx, &g_client_cfg);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    g_client_ready = 1;
    return WH_ERROR_OK;
}

whClientContext *wolfhsm_guest_client(void)
{
    return &g_client_ctx;
}

int wolftrust_guest_rng_stub(unsigned char *output, unsigned int sz)
{
    if (output == NULL && sz != 0u) {
        return WH_ERROR_BADARGS;
    }
    if (g_client_ready == 0) {
        return -1;
    }
    return wh_Client_RngGenerate(&g_client_ctx, output, sz);
}
