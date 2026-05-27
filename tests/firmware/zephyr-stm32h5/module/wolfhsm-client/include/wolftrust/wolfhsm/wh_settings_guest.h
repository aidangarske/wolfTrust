/* wolfHSM client configuration for wolfTrust's Zephyr non-secure guest.
 * Ported from tests/firmware/stm32h563/nonsecure/wh_settings_guest.h.
 * The wolfHSM CMake shim wires this header in as wolfhsm_cfg.h via the
 * trampoline generated in ${ZEPHYR_BINARY_DIR}/include/generated/. */

#ifndef WOLFTRUST_ZEPHYR_WH_SETTINGS_GUEST_H
#define WOLFTRUST_ZEPHYR_WH_SETTINGS_GUEST_H

/* Client-only build. wh_server.c never gets compiled into the guest;
 * wh_client.c is gated on this define. */
#define WOLFHSM_CFG_ENABLE_CLIENT

/* MUST match the secure-side wh_settings_local.h. CMSE shared buffer is
 * 256 B per slot; 8 B is consumed by the whTransportMemCsr header,
 * leaving 248 B for payload. */
#define WOLFHSM_CFG_COMM_DATA_LEN 248

/* No RTC / port-time on this target. */
#define WOLFHSM_CFG_NO_SYS_TIME

/* Drop the hexdump debug helper to keep the guest small. */
#define WOLFHSM_CFG_HEXDUMP_DISABLE

#endif /* WOLFTRUST_ZEPHYR_WH_SETTINGS_GUEST_H */
