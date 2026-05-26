/* wt_hsm.c
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
 * wolfHSM service module — Wave 3.
 *
 * Owns:
 *   - The shared flash-backed NVM context.
 *   - The shared NVM serialisation lock (callbacks in wt_hsm_lock.c).
 *   - Per-guest whServerContext instances driven by per-guest tasklets.
 *
 * What this file does NOT own:
 *   - Transport implementation (Wave 4, cmse_transport.c).
 *   - Lock callback implementations (Wave 3B, wt_hsm_lock.c).
 *
 * Heap strategy:
 *   The secure profile defines NO_WOLFSSL_MEMORY + WOLFSSL_NO_MALLOC. There
 *   is no malloc/sbrk path and no wolfCrypt static heap arena; accidental
 *   XMALLOC users fail closed. The HSM server, NVM and crypto state used here
 *   is static, stack-owned, or caller-provided.
 */

/* wolfCrypt settings must come first. */
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/wc_port.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfssl/wolfcrypt/error-crypt.h"

/* wolfHSM headers. */
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_comm.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_lock.h"
#include "wolfhsm/wh_server.h"

/* wolfTrust headers. */
#include "wolftrust/types.h"
#include "wolftrust/sched/tasklet.h"
#include "wolftrust/sync/mutex.h"
#include "wolftrust/services/hsm.h"
#include "wolftrust/arch/armv8m/cmse_transport.h"

#include "hsm_flash.h"

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Per-tasklet secure stacks. Target builds may override WT_CO_STACK_SIZE
 * after measuring stack high-water marks for their HSM workload.
 *
 * Keep a guard area immediately below each descending stack.  Hardware PSPLIM
 * should trap a real underflow before this area is used, but the guard prevents
 * adjacent service metadata from being corrupted on emulators or during early
 * bring-up when stack-limit handling is incomplete.
 * ---------------------------------------------------------------------- */
#define WT_HSM_STACK_UNDERFLOW_GUARD_SIZE 256u

typedef struct wt_hsm_stack_slot {
    uint8_t guard[WT_HSM_STACK_UNDERFLOW_GUARD_SIZE];
    uint8_t stack[WT_CO_STACK_SIZE];
} wt_hsm_stack_slot_t;

static wt_hsm_stack_slot_t g_co_stack_slots[WT_MAX_GUESTS]
    __attribute__((aligned(8)));

/* -------------------------------------------------------------------------
 * Per-guest state.
 * ---------------------------------------------------------------------- */
typedef struct wt_hsm_guest {
    whServerContext           server;
    whServerCryptoContext     crypto;
    /* Transport config storage kept alive for the server context lifetime. */
    void                     *transport_ctx;
    const whTransportServerCb *transport_cb;
    const void               *transport_cfg;
    whCommServerConfig        comm_cfg;
    whServerConfig            server_cfg;
    wt_tasklet_t             *tasklet;
    bool                      ready;
} wt_hsm_guest_t;

static wt_hsm_guest_t g_guests[WT_MAX_GUESTS];

/* -------------------------------------------------------------------------
 * Shared NVM state (one instance, serialised by g_nvm_lock_mutex).
 * ---------------------------------------------------------------------- */
static whNvmContext      g_nvm_ctx;
static whNvmFlashContext g_nvm_flash_ctx;

static const whNvmCb   g_nvm_flash_cb[1] = {WH_NVM_FLASH_CB};

/* -------------------------------------------------------------------------
 * Shared NVM lock.
 *
 * g_wt_hsm_lock_cb is defined in the parallel Wave 3B file wt_hsm_lock.c.
 * Its callbacks dispatch acquire/release to g_nvm_lock_mutex.
 * ---------------------------------------------------------------------- */
static wt_mutex_t  g_nvm_lock_mutex;
static whLockConfig g_nvm_lock_cfg;
extern const whLockCb g_wt_hsm_lock_cb; /* defined in wt_hsm_lock.c */

/* -------------------------------------------------------------------------
 * Forward declaration — tasklet body defined below.
 * ---------------------------------------------------------------------- */
static void wt_hsm_tasklet_main(void *arg);

/* =========================================================================
 * wt_hsm_init
 * ====================================================================== */
int wt_hsm_init(void)
{
    int rc;

    whNvmFlashConfig nvm_flash_cfg;
    whNvmConfig      nvm_cfg;

    /* ------------------------------------------------------------------
     * 1. Global wolfCrypt init.
     * ---------------------------------------------------------------- */
    rc = wolfCrypt_Init();
    if (rc != 0) {
        return rc;
    }

    /* ------------------------------------------------------------------
     * 2. Initialise the target flash backend.
     * ---------------------------------------------------------------- */
    rc = g_wt_hsm_flash_cb.Init(wt_hsm_flash_context(),
                                wt_hsm_flash_config());
    if (rc != 0) {
        return rc;
    }

    /* ------------------------------------------------------------------
     * 3. Initialise NVM flash-log layer.
     *
     * whNvmFlashConfig wires the target flash callback table and context into
     * the flash-log NVM backend.
     * ---------------------------------------------------------------- */
    (void)memset(&g_nvm_flash_ctx, 0, sizeof(g_nvm_flash_ctx));
    (void)memset(&nvm_flash_cfg, 0, sizeof(nvm_flash_cfg));

    nvm_flash_cfg.cb      = &g_wt_hsm_flash_cb;
    nvm_flash_cfg.context = wt_hsm_flash_context();
    nvm_flash_cfg.config  = wt_hsm_flash_config();

    /* ------------------------------------------------------------------
     * 4. Set up the NVM lock before calling wh_Nvm_Init.
     *
     * The lock must be initialised (via its init callback) before the
     * NVM context is fully wired, because wh_Nvm_Init may attempt to
     * call wh_Lock_Init internally via the whNvmConfig.lockConfig path.
     * We pre-initialise our mutex here for clarity.
     * ---------------------------------------------------------------- */
    wt_mutex_init(&g_nvm_lock_mutex);

    g_nvm_lock_cfg.cb      = &g_wt_hsm_lock_cb;
    g_nvm_lock_cfg.context = &g_nvm_lock_mutex;
    g_nvm_lock_cfg.config  = NULL; /* no extra config needed by our callbacks */

    /* ------------------------------------------------------------------
     * 5. Initialise the NVM context.
     *
     * whNvmConfig.cb points to the flash-NVM callback table
     * (wh_NvmFlash_Init etc.), .context is the whNvmFlashContext, and
     * .config is the whNvmFlashConfig passed through to wh_NvmFlash_Init.
     * The lockConfig field activates the embedded whLock for thread-safe
     * builds (WOLFHSM_CFG_THREADSAFE is defined in wh_settings_local.h).
     * ---------------------------------------------------------------- */
    (void)memset(&g_nvm_ctx, 0, sizeof(g_nvm_ctx));
    (void)memset(&nvm_cfg, 0, sizeof(nvm_cfg));

    nvm_cfg.cb       = (whNvmCb *)g_nvm_flash_cb;
    nvm_cfg.context  = &g_nvm_flash_ctx;
    nvm_cfg.config   = &nvm_flash_cfg;
#ifdef WOLFHSM_CFG_THREADSAFE
    nvm_cfg.lockConfig = &g_nvm_lock_cfg;
#endif

    rc = wh_Nvm_Init(&g_nvm_ctx, &nvm_cfg);
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    return 0;
}

/* =========================================================================
 * wt_hsm_tasklet_main
 *
 * Runs indefinitely inside a per-guest tasklet.  Calls
 * wh_Server_HandleRequestMessage once per iteration and blocks if no
 * request is pending (WH_ERROR_NOTREADY) or on unexpected errors.
 * ====================================================================== */
static void wt_hsm_tasklet_main(void *arg)
{
    wt_guest_id_t   gid = (wt_guest_id_t)(uintptr_t)arg;
    wt_hsm_guest_t *g   = &g_guests[gid];

    for (;;) {
        int rc = wh_Server_HandleRequestMessage(&g->server);
        if (rc == WH_ERROR_NOTREADY) {
            wt_tasklet_block();
        }
        else if (rc != WH_ERROR_OK) {
            /* TODO: forward error to secure log buffer when available. */
            wt_tasklet_block();
        }
    }
}

/* =========================================================================
 * wt_hsm_guest_init
 * ====================================================================== */
int wt_hsm_guest_init(wt_guest_id_t guest_id,
                      const whTransportServerCb *transport_cb,
                      void *transport_ctx,
                      const void *transport_cfg)
{
    int             rc;
    wt_hsm_guest_t *g;

    /* ------------------------------------------------------------------
     * 1. Bounds + duplicate check.
     * ---------------------------------------------------------------- */
    if (guest_id >= WT_MAX_GUESTS) {
        return WH_ERROR_BADARGS;
    }
    g = &g_guests[guest_id];
    if (g->ready) {
        return WH_ERROR_BADARGS;
    }

    /* ------------------------------------------------------------------
     * 2. Zero the per-guest struct for a clean slate.
     * ---------------------------------------------------------------- */
    (void)memset(g, 0, sizeof(*g));

    /* ------------------------------------------------------------------
     * 3. Initialise the RNG that lives inside the crypto context.
     *
     * The whServerCryptoContext embeds WC_RNG rng[1].  We initialise it
     * with INVALID_DEVID so the server's RNG uses the local entropy source
     * (CUSTOM_RAND_GENERATE_BLOCK = wolftrust_rng_generate_block) rather
     * than routing back through a HSM client callback. No heap hint is used:
     * the secure wolfCrypt build has no heap allocator.
     * ---------------------------------------------------------------- */
    rc = wc_InitRng_ex(g->crypto.rng, NULL, INVALID_DEVID);
    if (rc != 0) {
        return rc;
    }

    /* ------------------------------------------------------------------
     * 4. Stash transport pointers and build comm config.
     *
     * whCommServerConfig.server_id is uint8_t; truncation from uint16_t
     * is intentional — client IDs 1..WT_MAX_GUESTS all fit in a byte.
     * ---------------------------------------------------------------- */
    g->transport_ctx = transport_ctx;
    g->transport_cb  = transport_cb;
    g->transport_cfg = transport_cfg;

    g->comm_cfg.transport_cb      = transport_cb;
    g->comm_cfg.transport_context = transport_ctx;
    g->comm_cfg.transport_config  = transport_cfg;
    g->comm_cfg.server_id         = (uint8_t)wt_hsm_guest_client_id(guest_id);

    /* ------------------------------------------------------------------
     * 5. Build server config.
     * ---------------------------------------------------------------- */
    g->server_cfg.comm_config = &g->comm_cfg;
    g->server_cfg.nvm         = &g_nvm_ctx;
    g->server_cfg.crypto      = &g->crypto;
#if defined(WOLF_CRYPTO_CB)
    g->server_cfg.devId       = INVALID_DEVID;
#endif

    /* ------------------------------------------------------------------
     * 6. Initialise the wolfHSM server context.
     *
     * Note: wh_Server_Init expects NVM and crypto to be initialised before
     * it is called.  NVM was initialised in wt_hsm_init(); the RNG (crypto)
     * was initialised in step 3 above.
     * ---------------------------------------------------------------- */
    rc = wh_Server_Init(&g->server, &g->server_cfg);
    if (rc != WH_ERROR_OK) {
        wc_FreeRng(g->crypto.rng);
        return rc;
    }

    /* Mark the server connected so HandleRequestMessage does not reject
     * incoming packets immediately. */
    rc = wh_Server_SetConnected(&g->server, WH_COMM_CONNECTED);
    if (rc != WH_ERROR_OK) {
        wh_Server_Cleanup(&g->server);
        wc_FreeRng(g->crypto.rng);
        return rc;
    }

    /* ------------------------------------------------------------------
     * 7. Create tasklet.
     * ---------------------------------------------------------------- */
    g->tasklet = wt_tasklet_create_blocked(g_co_stack_slots[guest_id].stack,
                                           WT_CO_STACK_SIZE,
                                           wt_hsm_tasklet_main,
                                           (void *)(uintptr_t)guest_id);
    if (g->tasklet == NULL) {
        wh_Server_Cleanup(&g->server);
        wc_FreeRng(g->crypto.rng);
        return WH_ERROR_ABORTED;
    }

    /* ------------------------------------------------------------------
     * 8. Mark guest ready.
     * ---------------------------------------------------------------- */
    g->ready = true;
    return 0;
}

/* =========================================================================
 * wt_hsm_guest_ready
 * ====================================================================== */
bool wt_hsm_guest_ready(wt_guest_id_t guest_id)
{
    if (guest_id >= WT_MAX_GUESTS) {
        return false;
    }
    return g_guests[guest_id].ready;
}

/* =========================================================================
 * wt_hsm_guest_client_id
 *
 * Client-ID 0 is reserved; guests are numbered from 1.
 * ====================================================================== */
uint16_t wt_hsm_guest_client_id(wt_guest_id_t guest_id)
{
    return (uint16_t)(guest_id + 1u);
}

/* =========================================================================
 * wt_hsm_guest_tasklet
 *
 * Returns the tasklet handle for the given guest so the monitor can wake it
 * at epoch boundaries.  Returns NULL for unknown guests or
 * guests that have not yet been initialised.
 * ====================================================================== */
struct wt_co *wt_hsm_guest_tasklet(wt_guest_id_t guest_id)
{
    if (guest_id >= WT_MAX_GUESTS) return NULL;
    return g_guests[guest_id].tasklet;
}

/* =========================================================================
 * wt_hsm_guest_for_tasklet
 *
 * Reverse lookup. Linear scan is fine: WT_MAX_GUESTS is small (currently 2)
 * and this is only called from the Secure fault dispatcher.
 * ====================================================================== */
wt_guest_id_t wt_hsm_guest_for_tasklet(const struct wt_co *tasklet)
{
    wt_guest_id_t gid;

    if (tasklet == NULL) return WT_MAX_GUESTS;

    for (gid = 0; gid < WT_MAX_GUESTS; gid++) {
        if (g_guests[gid].tasklet == tasklet) {
            return gid;
        }
    }
    return WT_MAX_GUESTS;
}

/* =========================================================================
 * wt_hsm_signal_fault
 *
 * Called from the Secure fault dispatcher after wt_tasklet_mark_faulted has
 * removed the tasklet from the scheduler. Drops any NVM lock the dying
 * tasklet still held, writes a WH_ERROR_ABORTED fatal-response into
 * the guest's transport so the NS client unblocks with a clean error,
 * and clears the ready bit so future NSC veneers reject HSM calls from
 * this guest.
 *
 * Idempotent: calling on an already-faulted guest is harmless.
 * ====================================================================== */
int wt_hsm_signal_fault(wt_guest_id_t guest_id)
{
    wt_hsm_guest_t *g;

    if (guest_id >= WT_MAX_GUESTS) {
        return WH_ERROR_BADARGS;
    }
    g = &g_guests[guest_id];

    /* Force-release the NVM lock if the faulted tasklet was its holder.
     * This is the only mutex in the secure-side wolfHSM service; if more
     * are added later, this is the place to drop them all. */
    if (g->tasklet != NULL) {
        wt_mutex_release_if_holder(&g_nvm_lock_mutex, g->tasklet);
    }

    /* Tell the NS client. Failure here just means the transport was
     * never wired (guest_id outside transport range) — still safe. */
    (void)wt_cmse_transport_signal_fault(guest_id);

    g->ready = false;
    return WH_ERROR_OK;
}
