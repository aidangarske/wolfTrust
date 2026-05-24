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
 *   - The wolfCrypt static-memory pool (WOLFSSL_STATIC_MEMORY path).
 *   - The shared flash-backed NVM context.
 *   - The shared NVM serialisation lock (callbacks in wt_hsm_lock.c).
 *   - Per-guest whServerContext instances driven by per-guest coroutines.
 *
 * What this file does NOT own:
 *   - Transport implementation (Wave 4, cmse_transport.c).
 *   - Lock callback implementations (Wave 3B, wt_hsm_lock.c).
 *
 * Static-memory strategy:
 *   user_settings.h sets both WOLFSSL_STATIC_MEMORY and WOLFSSL_NO_MALLOC.
 *   With WOLFSSL_STATIC_MEMORY the wolfCrypt allocator dispatches through
 *   WOLFSSL_HEAP_HINT structs carved out of a caller-supplied buffer.
 *   wolfSSL_SetGlobalHeapHint() makes the pool the default for all
 *   XMALLOC(heap=NULL) calls, which covers every wolfCrypt internal
 *   allocation that does not carry an explicit heap hint.
 *
 *   wc_LoadStaticMemory() is called once with g_wolfcrypt_pool at boot;
 *   the resulting WOLFSSL_HEAP_HINT pointer is registered as the global
 *   hint.  wolfHSM's NVM layer uses XMALLOC only during Init; the wolfCrypt
 *   crypto layer keeps its working state on the stack or in caller-supplied
 *   structs (ecc_key, WC_RNG …) so the pool is sufficient.
 *
 *   If wh_NvmFlash_Init or wh_Server_Init internally allocate from the heap
 *   during initialisation they will draw from this pool.  In practice the
 *   wolfHSM NVM flash layer does not heap-allocate; the whNvmFlashContext
 *   and directory are stack/static structures.  Should a future wolfHSM
 *   version add dynamic allocation we will need to increase
 *   WT_HSM_WOLFCRYPT_POOL_BYTES accordingly.
 */

/* wolfCrypt settings must come first. */
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/memory.h"
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
#include "wolftrust/sched/coroutine.h"
#include "wolftrust/sync/mutex.h"
#include "wolftrust/services/hsm.h"

#include "hsm_flash.h"

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * wolfCrypt static memory pool.
 *
 * Sized empirically: ECC P-256 keygen + sign with HashDRBG fits in ~16 KB.
 * We over-provision to 24 KB to give the bucketed allocator slack across
 * multiple concurrent per-guest operations.
 * ---------------------------------------------------------------------- */
#define WT_HSM_WOLFCRYPT_POOL_BYTES (24u * 1024u)

static uint8_t g_wolfcrypt_pool[WT_HSM_WOLFCRYPT_POOL_BYTES]
    __attribute__((aligned(8)));

/* One-time heap hint pointer populated by wc_LoadStaticMemory. */
static WOLFSSL_HEAP_HINT *g_heap_hint = NULL;

/* -------------------------------------------------------------------------
 * Per-coroutine secure stacks.  WT_CO_STACK_SIZE is sized for wolfCrypt TFM
 * ECC operations, which use deep temporary big-int frames on Cortex-M.
 * ---------------------------------------------------------------------- */
static uint8_t g_co_stacks[WT_MAX_GUESTS][WT_CO_STACK_SIZE]
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
    wt_co_t                  *coroutine;
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
 * Forward declaration — coroutine body defined below.
 * ---------------------------------------------------------------------- */
static void wt_hsm_coroutine_main(void *arg);

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
     * 2. Initialise wolfCrypt static memory pool.
     *
     * wc_LoadStaticMemory carves WOLFSSL_HEAP and WOLFSSL_HEAP_HINT
     * structs from the front of g_wolfcrypt_pool, then partitions the
     * remainder into bucketed free-lists according to WOLFMEM_BUCKETS /
     * WOLFMEM_DIST defaults.  WOLFMEM_GENERAL means all allocations
     * (not I/O-only) come from this pool.
     *
     * wolfSSL_SetGlobalHeapHint installs the resulting hint as the
     * default for every XMALLOC(heap=NULL) call, which covers all
     * wolfCrypt internal allocations that do not carry an explicit hint.
     * With WOLFSSL_NO_MALLOC set, any allocation that misses both the
     * per-call hint and the global hint returns NULL immediately instead
     * of falling through to the system allocator.
     * ---------------------------------------------------------------- */
    rc = wc_LoadStaticMemory(&g_heap_hint,
                             g_wolfcrypt_pool,
                             (unsigned int)sizeof(g_wolfcrypt_pool),
                             WOLFMEM_GENERAL,
                             0 /* maxSz: no per-operation cap */);
    if (rc != 0) {
        return rc;
    }
    wolfSSL_SetGlobalHeapHint(g_heap_hint);

    /* ------------------------------------------------------------------
     * 3. Initialise the target flash backend.
     * ---------------------------------------------------------------- */
    rc = g_wt_hsm_flash_cb.Init(wt_hsm_flash_context(),
                                wt_hsm_flash_config());
    if (rc != 0) {
        return rc;
    }

    /* ------------------------------------------------------------------
     * 4. Initialise NVM flash-log layer.
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
     * 5. Set up the NVM lock before calling wh_Nvm_Init.
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
     * 6. Initialise the NVM context.
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
 * wt_hsm_coroutine_main
 *
 * Runs indefinitely inside a per-guest coroutine.  Calls
 * wh_Server_HandleRequestMessage once per iteration and yields if no
 * request is pending (WH_ERROR_NOTREADY) or on unexpected errors.
 * ====================================================================== */
static void wt_hsm_coroutine_main(void *arg)
{
    wt_guest_id_t   gid = (wt_guest_id_t)(uintptr_t)arg;
    wt_hsm_guest_t *g   = &g_guests[gid];

    for (;;) {
        int rc = wh_Server_HandleRequestMessage(&g->server);
        if (rc == WH_ERROR_NOTREADY) {
            wt_co_block();
        }
        else if (rc != WH_ERROR_OK) {
            /* TODO: forward error to secure log buffer when available. */
            wt_co_block();
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
     * than routing back through a HSM client callback.  The heap hint NULL
     * is acceptable here: wolfCrypt will use the global heap hint that
     * wt_hsm_init() registered with wolfSSL_SetGlobalHeapHint().
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
     * 7. Create coroutine.
     * ---------------------------------------------------------------- */
    g->coroutine = wt_co_create_blocked(g_co_stacks[guest_id],
                                        WT_CO_STACK_SIZE,
                                        wt_hsm_coroutine_main,
                                        (void *)(uintptr_t)guest_id);
    if (g->coroutine == NULL) {
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
 * wt_hsm_guest_coroutine
 *
 * Returns the coroutine handle for the given guest so NSC veneers can
 * wake it before calling wt_co_tick.  Returns NULL for unknown guests or
 * guests that have not yet been initialised.
 * ====================================================================== */
struct wt_co *wt_hsm_guest_coroutine(wt_guest_id_t guest_id)
{
    if (guest_id >= WT_MAX_GUESTS) return NULL;
    return g_guests[guest_id].coroutine;
}
