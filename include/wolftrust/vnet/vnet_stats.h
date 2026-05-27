/* vnet_stats.h
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

#ifndef WOLFTRUST_VNET_STATS_H
#define WOLFTRUST_VNET_STATS_H

#include <stdint.h>

typedef struct vnet_stats {
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t rx_bytes;

    uint32_t drop_pool_full;
    uint32_t drop_queue_full;
    uint32_t drop_unknown_unicast;
    uint32_t drop_policy;
    uint32_t drop_timeout;

    uint32_t err_malformed;
    uint32_t err_spoof_src_mac;
    uint32_t err_dup_mac_assign;
    uint32_t err_invalid_mac_assign;
    uint32_t err_release;
} vnet_stats_t;

#endif
