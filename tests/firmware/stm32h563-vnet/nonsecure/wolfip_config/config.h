/* config.h — embedded-sized wolfIP config for the NS vnet guests.
 *
 * Picked up by lib/wolfIP/src/wolfip.c via `#include "config.h"`. We
 * arrange -I so this directory wins over lib/wolfIP/. The defaults in
 * lib/wolfIP/config.h are tuned for POSIX hosts (20 KiB RX + 32 KiB TX
 * per socket) which blows the 32 KiB-per-guest NS RAM budget — this
 * override gives us a single ICMP socket with 1 KiB FIFOs.
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

#ifndef WOLF_CONFIG_H
#define WOLF_CONFIG_H

#define ETHERNET
/* Bounded by the mediated SERVICE_VNET link (WT_VNET_PSA_MTU): one frame
 * plus RX metadata must fit a single psa_call copied transfer. */
#define LINK_MTU 1000
#define LINK_MTU_MIN 64U

#define MAX_TCPSOCKETS 1
#define MAX_UDPSOCKETS 1
#define MAX_ICMPSOCKETS 1

#define RXBUF_SIZE 256
#define TXBUF_SIZE 256

#define MAX_NEIGHBORS 4

#ifndef WOLFIP_MAX_INTERFACES
#define WOLFIP_MAX_INTERFACES 1
#endif

#define WOLFIP_RAWSOCKETS 0
#define WOLFIP_PACKET_SOCKETS 0
#define WOLFIP_ENABLE_FORWARDING 0
#define WOLFIP_ENABLE_LOOPBACK 0
#define WOLFIP_ENABLE_HTTP 0
#define WOLFIP_ENABLE_TFTP 0
#define WOLFIP_VLAN 0
#define CONFIG_IPFILTER 0

/* Defaults that lib/wolfIP/config.h leaves to user override. */
#define WOLFIP_IP "10.0.0.1"
#define HOST_STACK_IP "10.0.0.2"

#endif
