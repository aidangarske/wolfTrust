/* wolftrust_client.c
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

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <wolftrust/zephyr/client.h>

#define WH_ERROR_OK       0
#define WH_ERROR_BADARGS -2000
#define WH_ERROR_NOTREADY -2001
#define WH_ERROR_ABORTED -2002

/* Liveness rides the mediated FF-M gateway (WT-FFM-0054): one veneer call
 * proving the SPM answers. The raw WolfTrust_HSM_* transport is retired. */
extern uint32_t WolfTrust_FFM_FrameworkVersion(void);

static const char *g_tag;

int wt_zephyr_client_init(const char *tag)
{
    g_tag = tag;
    return (WolfTrust_FFM_FrameworkVersion() == 0x0100u) ? WH_ERROR_OK
                                                         : WH_ERROR_ABORTED;
}

int wt_zephyr_client_ping(void)
{
    if (g_tag == NULL) {
        return WH_ERROR_BADARGS;
    }

    return (WolfTrust_FFM_FrameworkVersion() == 0x0100u) ? WH_ERROR_OK
                                                         : WH_ERROR_NOTREADY;
}

const char *wt_zephyr_client_status_string(int rc)
{
    switch (rc) {
    case WH_ERROR_OK:
        return "ok";
    case WH_ERROR_BADARGS:
        return "badargs";
    case WH_ERROR_NOTREADY:
        return "notready";
    case WH_ERROR_ABORTED:
        return "aborted";
    default:
        return "unknown";
    }
}
