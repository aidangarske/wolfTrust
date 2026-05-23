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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/*
 * wolfHSM build configuration for the wolfTrust STM32H563 non-secure (guest)
 * partition.
 *
 * PICKUP MECHANISM NOTE: see secure/wh_settings_local.h for a full description
 * of the two options (symlink/copy as wolfhsm_cfg.h, or -include).  The same
 * mechanism applies here; only the role flips from server to client.
 */

#ifndef WOLFTRUST_NS_WH_SETTINGS_GUEST_H
#define WOLFTRUST_NS_WH_SETTINGS_GUEST_H

/*---------------------------------------------------------------------------
 * Role: client only
 *
 * The non-secure guest links the wolfHSM client.  wh_client.c is gated on
 * WOLFHSM_CFG_ENABLE_CLIENT; without this define, wh_Client_Init and friends
 * are absent at link time.
 *
 * WOLFHSM_CFG_ENABLE_SERVER is intentionally NOT defined: the guest does not
 * host a server and must not pull in wh_server.c.
 *---------------------------------------------------------------------------*/
#define WOLFHSM_CFG_ENABLE_CLIENT

/*---------------------------------------------------------------------------
 * Communication buffer
 *
 * MUST match the secure side exactly.  The CMSE shared buffer is 256 B per
 * slot; 8 B is consumed by the whTransportMemCsr header, leaving 248 B for
 * payload.  If client and server disagree, messages will be fragmented
 * incorrectly and the protocol will desync.
 *
 * WARNING: any change to this value MUST also be applied to
 * secure/wh_settings_local.h.
 *---------------------------------------------------------------------------*/
#define WOLFHSM_CFG_COMM_DATA_LEN 248

/*---------------------------------------------------------------------------
 * System time
 *
 * Suppress the wolfHSM port-time requirement.  Same rationale as the secure
 * side: no RTC or port-time abstraction is available on this board during
 * bring-up.
 *---------------------------------------------------------------------------*/
#define WOLFHSM_CFG_NO_SYS_TIME

/*---------------------------------------------------------------------------
 * Hexdump
 *
 * Disable the hex-dump debug helper to reduce code footprint on the guest.
 *---------------------------------------------------------------------------*/
#define WOLFHSM_CFG_HEXDUMP_DISABLE

/*---------------------------------------------------------------------------
 * Disabled extensions (opt-in, so omitting the define is sufficient)
 *
 * WOLFHSM_CFG_SHE_EXTENSION         -- AutoSAR SHE not required
 * WOLFHSM_CFG_DMA                   -- DMA address translation not used
 * WOLFHSM_CFG_ENABLE_AUTHENTICATION -- mutual auth handshake not used
 * WOLFHSM_CFG_LOGGING               -- log subsystem not used
 * WOLFHSM_CFG_THREADSAFE            -- single-threaded guest; lock vtable
 *                                      would be dead weight
 *
 * All are #ifdef-gated in the wolfHSM headers; leaving them undefined is the
 * correct way to exclude them.  No #undef is needed.
 *---------------------------------------------------------------------------*/

/* Guest-side counterpart to secure/wh_settings_local.h.
 * Any change to WOLFHSM_CFG_COMM_DATA_LEN MUST be applied to BOTH files. */

#endif /* WOLFTRUST_NS_WH_SETTINGS_GUEST_H */
