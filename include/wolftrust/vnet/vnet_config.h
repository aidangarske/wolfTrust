/* vnet_config.h
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

#ifndef WOLFTRUST_VNET_CONFIG_H
#define WOLFTRUST_VNET_CONFIG_H

#ifndef WT_VNET_FRAME_MAX
#define WT_VNET_FRAME_MAX 1536U
#endif

#ifndef WT_VNET_POOL_SLOTS
#define WT_VNET_POOL_SLOTS 8U
#endif

#ifndef WT_VNET_RX_QUEUE_DEPTH
#define WT_VNET_RX_QUEUE_DEPTH 8U
#endif

#ifndef WT_VNET_FDB_ENTRIES
#define WT_VNET_FDB_ENTRIES 16U
#endif

#ifndef WT_VNET_RX_IRQ
#define WT_VNET_RX_IRQ 130U
#endif

#ifndef WT_VNET_TIMEOUT_TICKS
#define WT_VNET_TIMEOUT_TICKS 500U
#endif

#ifndef WT_VNET_UNKNOWN_UCAST_FLOOD
#define WT_VNET_UNKNOWN_UCAST_FLOOD 0
#endif

#define WT_VNET_FRAME_MIN 14U

#define WT_VNET_ABI_VERSION 1U

#endif
