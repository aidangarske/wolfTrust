/* hsm_flash.h
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

#ifndef WOLFTRUST_STM32H563_HSM_FLASH_H
#define WOLFTRUST_STM32H563_HSM_FLASH_H

#include "wolfhsm/wh_flash.h"

extern const whFlashCb g_wt_hsm_flash_cb;

void *wt_hsm_flash_context(void);
const void *wt_hsm_flash_config(void);

#if defined(WT_CONFORMANCE) && (WT_CONFORMANCE == 1)
/* Privileged survive-reset NVM sync for the conformance DRIVER partition
 * (P5 K2): store==0 loads the reserved flash sector into buf, store!=0 erases
 * that sector and programs buf back. len must be a multiple of the 16-byte
 * program unit. Returns 0 on success, -1 on failure. */
int wt_conf_nvm_flash_sync(uint8_t *buf, uint32_t len, int store);
#endif

#endif /* WOLFTRUST_STM32H563_HSM_FLASH_H */
