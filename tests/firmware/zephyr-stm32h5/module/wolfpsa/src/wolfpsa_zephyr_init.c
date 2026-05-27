/* Zephyr SYS_INIT wrapper for wolfPSA.
 *
 * Runs at SYS_INIT priority just after the wolfhsm-client module's hook
 * (which registers wh_Client_CryptoCb against WH_DEV_ID). We:
 *   1. tell wolfPSA to thread WH_DEV_ID through every wc_*Init() call
 *      it issues (the runtime-devid patch carried in the lib/wolfPSA
 *      submodule provides wolfPSA_SetDefaultDevID for this).
 *   2. call psa_crypto_init() so the PSA Crypto API is usable from
 *      app code that runs out of main().
 */

#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <psa/crypto.h>
#include <wolfpsa/psa_engine.h>

#include "wolfhsm/wh_client.h"

LOG_MODULE_REGISTER(wolfpsa_zephyr, LOG_LEVEL_INF);

static int wolfpsa_zephyr_sys_init(void)
{
    psa_status_t st;

    (void)wolfPSA_SetDefaultDevID(WH_DEV_ID);

    st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        LOG_ERR("psa_crypto_init failed st=%d", (int)st);
        return -EIO;
    }

    LOG_INF("wolfPSA up; default devId=0x%08x", (unsigned)WH_DEV_ID);
    return 0;
}

/* APPLICATION priority guarantees we run AFTER the wolfhsm-client module's
 * POST_KERNEL hook (so the crypto_cb device is registered before we ask
 * wolfPSA to use it) but before main(). */
SYS_INIT(wolfpsa_zephyr_sys_init, APPLICATION,
         CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
