/* wh_settings_guest.h
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

/* wolfHSM client configuration for wolfTrust's Zephyr non-secure guest.
 * Ported from tests/firmware/stm32h563/nonsecure/wh_settings_guest.h.
 * The wolfHSM CMake shim wires this header in as wolfhsm_cfg.h via the
 * trampoline generated in ${ZEPHYR_BINARY_DIR}/include/generated/. */

#ifndef WOLFTRUST_ZEPHYR_WH_SETTINGS_GUEST_H
#define WOLFTRUST_ZEPHYR_WH_SETTINGS_GUEST_H

/* Client-only build. wh_server.c never gets compiled into the guest;
 * wh_client.c is gated on this define. */
#define WOLFHSM_CFG_ENABLE_CLIENT

/* MUST match the secure-side wh_settings_local.h. Each CMSE slot is 384 B:
 * 8 B whTransportMemCsr + 8 B whCommHeader + 368 B payload. */
#define WOLFHSM_CFG_COMM_DATA_LEN 368

/* No RTC / port-time on this target. */
#define WOLFHSM_CFG_NO_SYS_TIME

/* Drop the hexdump debug helper to keep the guest small. */
#define WOLFHSM_CFG_HEXDUMP_DISABLE

#endif /* WOLFTRUST_ZEPHYR_WH_SETTINGS_GUEST_H */
