/* vnet_nsc.c
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

/* Armv8-M NS-client gateway for the VNET service: the cmse_nonsecure_entry
 * veneers plus the CMSE window checks and secure copy-bounce, extracted from
 * the neutral service (src/services/vnet/vnet_service.c) so the core carries
 * no CMSE dependency. The switch never sees NS memory: every buffer is
 * validated then copied through secure scratch before vnet_switch_* runs. */

#ifdef CONFIG_VNET

#include <stdint.h>
#include <string.h>
#include "wolftrust/types.h"
#include "wolftrust/arch/armv8m/cmse.h"
#include "wolftrust/vnet/vnet_abi.h"
#include "wolftrust/vnet/vnet_switch.h"
#include "wolftrust/vnet/vnet_errors.h"
#include "wolftrust/services/vnet_service.h"

static int do_open(vnet_info_t *ns_info)
{
    uint32_t vm;
    vnet_switch_t *sw;
    vnet_info_t local;
    int rc = wt_vnet_service_begin(&vm, &sw);
    if (rc != WT_VNET_OK) return rc;
    if (ns_info == NULL) return WT_VNET_E_BADARG;
    if (!wt_cmse_check_ns_rw(ns_info, sizeof(*ns_info))) {
        return WT_VNET_E_ACCESS;
    }
    if (!wt_cmse_check_in_guest_ns_ram((wt_guest_id_t)vm, ns_info,
                                       sizeof(*ns_info))) {
        return WT_VNET_E_ACCESS;
    }
    rc = vnet_switch_open(sw, vm, &local);
    if (rc != WT_VNET_OK) return rc;
    memcpy(ns_info, &local, sizeof(local));
    return WT_VNET_OK;
}

static int do_set_mac(const uint8_t *ns_mac6, uint32_t flags)
{
    uint32_t vm;
    vnet_switch_t *sw;
    vnet_mac_t mac;
    int rc = wt_vnet_service_begin(&vm, &sw);
    (void)flags;
    if (rc != WT_VNET_OK) return rc;
    if (ns_mac6 == NULL) return WT_VNET_E_BADARG;
    if (!wt_cmse_check_ns_ro(ns_mac6, VNET_MAC_LEN)) return WT_VNET_E_ACCESS;
    /* Use the addr variant — the MAC may legitimately live in the
     * guest's flash (.rodata) rather than its RAM window. */
    if (!wt_cmse_check_in_guest_ns_addr((wt_guest_id_t)vm, ns_mac6,
                                        VNET_MAC_LEN)) {
        return WT_VNET_E_ACCESS;
    }
    memcpy(mac.b, ns_mac6, VNET_MAC_LEN);
    return vnet_switch_assign_mac(sw, vm, &mac);
}

static int do_tx(const void *ns_frame, uint16_t len, uint32_t flags)
{
    uint32_t vm;
    vnet_switch_t *sw;
    int rc = wt_vnet_service_begin(&vm, &sw);
    uint8_t scratch[WT_VNET_FRAME_MAX];
    (void)flags;
    if (rc != WT_VNET_OK) return rc;
    if (ns_frame == NULL) return WT_VNET_E_BADARG;
    if (len < WT_VNET_FRAME_MIN || len > (uint16_t)WT_VNET_FRAME_MAX) {
        return WT_VNET_E_FRAME_LEN;
    }
    if (!wt_cmse_check_ns_ro(ns_frame, len)) return WT_VNET_E_ACCESS;
    /* TX buffers may live in either RAM (typical, wolfIP socket FIFOs)
     * or flash (a static literal — unusual but valid). */
    if (!wt_cmse_check_in_guest_ns_addr((wt_guest_id_t)vm, ns_frame, len)) {
        return WT_VNET_E_ACCESS;
    }
    memcpy(scratch, ns_frame, len);
    return vnet_switch_tx(sw, vm, scratch, len, wt_vnet_service_now_tick());
}

static int do_rx_poll(vnet_rx_meta_t *ns_meta)
{
    uint32_t vm;
    vnet_switch_t *sw;
    vnet_rx_meta_t local;
    int rc = wt_vnet_service_begin(&vm, &sw);
    if (rc != WT_VNET_OK) return rc;
    if (ns_meta == NULL) return WT_VNET_E_BADARG;
    if (!wt_cmse_check_ns_rw(ns_meta, sizeof(*ns_meta))) {
        return WT_VNET_E_ACCESS;
    }
    if (!wt_cmse_check_in_guest_ns_ram((wt_guest_id_t)vm, ns_meta,
                                       sizeof(*ns_meta))) {
        return WT_VNET_E_ACCESS;
    }
    rc = vnet_switch_poll_rx(sw, vm, &local);
    if (rc != WT_VNET_OK) return rc;
    memcpy(ns_meta, &local, sizeof(local));
    return WT_VNET_OK;
}

static int do_rx_read(uint16_t slot, uint16_t gen,
                      void *ns_dst, uint16_t dst_len)
{
    uint32_t vm;
    vnet_switch_t *sw;
    int rc = wt_vnet_service_begin(&vm, &sw);
    uint8_t scratch[WT_VNET_FRAME_MAX];
    int n;
    if (rc != WT_VNET_OK) return rc;
    if (ns_dst == NULL) return WT_VNET_E_BADARG;
    if (dst_len == 0 || dst_len > (uint16_t)WT_VNET_FRAME_MAX) {
        return WT_VNET_E_BADARG;
    }
    if (!wt_cmse_check_ns_rw(ns_dst, dst_len)) return WT_VNET_E_ACCESS;
    if (!wt_cmse_check_in_guest_ns_ram((wt_guest_id_t)vm, ns_dst, dst_len)) {
        return WT_VNET_E_ACCESS;
    }
    n = vnet_switch_read_rx(sw, vm, slot, gen, scratch, dst_len);
    if (n < 0) return n;
    memcpy(ns_dst, scratch, (size_t)n);
    return n;
}

static int do_rx_release(uint16_t slot, uint16_t gen)
{
    uint32_t vm;
    vnet_switch_t *sw;
    int rc = wt_vnet_service_begin(&vm, &sw);
    if (rc != WT_VNET_OK) return rc;
    return vnet_switch_release_rx(sw, vm, slot, gen);
}

static int do_irq_ack(void)
{
    uint32_t vm;
    vnet_switch_t *sw;
    int rc = wt_vnet_service_begin(&vm, &sw);
    if (rc != WT_VNET_OK) return rc;
    return vnet_switch_irq_ack(sw, vm);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_VNet_Open(vnet_info_t *ns_info)
{
    return do_open(ns_info);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_VNet_SetMac(const uint8_t *ns_mac6, uint32_t flags)
{
    return do_set_mac(ns_mac6, flags);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_VNet_Tx(const void *ns_frame, uint16_t len, uint32_t flags)
{
    return do_tx(ns_frame, len, flags);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_VNet_RxPoll(vnet_rx_meta_t *ns_meta)
{
    return do_rx_poll(ns_meta);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_VNet_RxRead(uint16_t slot, uint16_t gen,
                          void *ns_dst, uint16_t dst_len)
{
    return do_rx_read(slot, gen, ns_dst, dst_len);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_VNet_RxRelease(uint16_t slot, uint16_t gen)
{
    return do_rx_release(slot, gen);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_VNet_IrqAck(void)
{
    return do_irq_ack();
}

#endif /* CONFIG_VNET */
