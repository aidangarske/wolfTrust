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

/* Initialise the monitor-owned VNET subsystem. Called once at boot
 * from wt_monitor_init() when CONFIG_VNET is on. Wires the static
 * frame pool, FDB, per-vnic rings, and switch state. */
void wt_vnet_service_init(void);

#endif /* CONFIG_VNET */

#endif
