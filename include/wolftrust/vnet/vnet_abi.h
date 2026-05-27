/* vnet_abi.h — public ABI for the wolfTrust VNET service.
 *
 * Both the secure side (which defines the NSC veneers) and the
 * non-secure guest (which links against secure_cmse_implib.o to import
 * them) include this header. Keep it free of secure-side internals.
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

#ifndef WOLFTRUST_VNET_ABI_H
#define WOLFTRUST_VNET_ABI_H

#include <stdbool.h>
#include <stdint.h>
#include "wolftrust/vnet/vnet_config.h"
#include "wolftrust/vnet/vnet_mac.h"
#include "wolftrust/vnet/vnet_errors.h"

typedef struct vnet_info {
    uint32_t   abi_version;
    uint32_t   features;
    uint16_t   mtu;
    int16_t    rx_irq;
    vnet_mac_t default_mac;
    bool       mac_set;
} vnet_info_t;

typedef struct vnet_rx_meta {
    uint16_t token_slot;
    uint16_t token_gen;
    uint16_t len;
    uint16_t flags;
    uint32_t src_vm;
} vnet_rx_meta_t;

/* The seven NSC veneers exported by the secure side. Each returns a
 * signed int (wolfHSM/wt_vnet error space; 0 on success, negative on
 * failure). NS guests link these via secure_cmse_implib.o. */
int WolfTrust_VNet_Open(vnet_info_t *ns_info);
int WolfTrust_VNet_SetMac(const uint8_t *ns_mac6, uint32_t flags);
int WolfTrust_VNet_Tx(const void *ns_frame, uint16_t len, uint32_t flags);
int WolfTrust_VNet_RxPoll(vnet_rx_meta_t *ns_meta);
int WolfTrust_VNet_RxRead(uint16_t slot, uint16_t gen,
                          void *ns_dst, uint16_t dst_len);
int WolfTrust_VNet_RxRelease(uint16_t slot, uint16_t gen);
int WolfTrust_VNet_IrqAck(void);

#endif
