/* Zephyr SYS_INIT wrapper for the wolfTrust wolfHSM client. Brings up the
 * client at POST_KERNEL so SYS_INIT consumers running later (wolfPSA's own
 * init, an app's main()) find wc_CryptoCb_RegisterDevice() already done
 * and the secure-side server reachable. */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_client_cryptocb.h"

#include "wolfssl/wolfcrypt/cryptocb.h"

LOG_MODULE_REGISTER(wolftrust_wolfhsm_client, LOG_LEVEL_INF);

int  wolfhsm_guest_init(void);
whClientContext *wolfhsm_guest_client(void);

static int wolftrust_wolfhsm_client_sys_init(void)
{
    whClientContext *ctx;
    int rc;

    rc = wolfhsm_guest_init();
    if (rc != WH_ERROR_OK) {
        LOG_ERR("wolfhsm_guest_init failed rc=%d", rc);
        return -EIO;
    }

    ctx = wolfhsm_guest_client();
    rc = wc_CryptoCb_RegisterDevice(WH_DEV_ID, wh_Client_CryptoCb, ctx);
    if (rc != 0) {
        LOG_ERR("wc_CryptoCb_RegisterDevice failed rc=%d", rc);
        return -EIO;
    }
    /* wolfCrypt's "default devId" (wc_CryptoCb_DefaultDevID) returns the
     * first registered crypto_cb device; with WH_DEV_ID being the only
     * device we register, that's already WH_DEV_ID. wolfPSA threads its
     * own runtime-settable devId via wolfPSA_SetDefaultDevID() — done in
     * the wolfpsa module's SYS_INIT hook. */

    LOG_INF("wolfHSM client up; devId=0x%08x registered", (unsigned)WH_DEV_ID);
    return 0;
}

SYS_INIT(wolftrust_wolfhsm_client_sys_init, POST_KERNEL,
         CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
