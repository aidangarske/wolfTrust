/* vnet_service.h
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

#ifndef WOLFTRUST_SERVICES_VNET_SERVICE_H
#define WOLFTRUST_SERVICES_VNET_SERVICE_H

#ifdef CONFIG_VNET

#include "wolftrust/types.h"

/* Initialise the monitor-owned VNET subsystem. Called once at boot
 * from wt_monitor_init() when CONFIG_VNET is on. Wires the static
 * frame pool, FDB, per-vnic rings, and switch state; programs the
 * synthetic RX IRQ as NS-targeted and enabled in the NVIC. */
void wt_vnet_service_init(void);

/* OR the synthetic RX IRQ bit into mask so it survives the per-guest
 * irq_mask programmed by wt_platform_apply_irq_mask. Without this,
 * the per-guest mask would silently disable the vIRQ in the NVIC. */
void wt_vnet_service_augment_irq_mask(wt_irq_mask_t *mask);

/* Reflect the switch's per-vnic rx_irq_pending bit into the NS NVIC
 * for the guest about to resume. Called from wt_dispatch_guest just
 * before the BXNS. Level semantics: pending while the queue is
 * non-empty (and the queue empties only via vnet_rx_release). */
void wt_vnet_service_refresh_irq(wt_guest_id_t guest_id);

#endif /* CONFIG_VNET */

#endif
