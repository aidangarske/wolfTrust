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

extern int WolfTrust_HSM_Cancel(uint16_t seq);
extern int WolfTrust_HSM_Poll(uint16_t seq);

static const char *g_tag;

int wt_zephyr_client_init(const char *tag)
{
    g_tag = tag;
    return WolfTrust_HSM_Cancel(0u);
}

int wt_zephyr_client_ping(void)
{
    if (g_tag == NULL) {
        return WH_ERROR_BADARGS;
    }

    return WolfTrust_HSM_Poll(0u);
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
