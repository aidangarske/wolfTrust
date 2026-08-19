/* port_nvm.h
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

#ifndef WOLFTRUST_PORT_NVM_H
#define WOLFTRUST_PORT_NVM_H

#include "wolfhsm/wh_flash.h"

/* MCU-family NVM/flash provider contract (WT-PORT-0003). The SoC port supplies
 * the wolfHSM flash callback plus its opaque context and config; the core HSM
 * service consumes them through this neutral header instead of a SoC-specific
 * one. A new port implements these three symbols. */
extern const whFlashCb g_wt_hsm_flash_cb;
void *wt_hsm_flash_context(void);
const void *wt_hsm_flash_config(void);

#endif /* WOLFTRUST_PORT_NVM_H */
