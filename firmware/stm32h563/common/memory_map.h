/* memory_map.h
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

#ifndef WOLFTRUST_FW_STM32H563_MEMORY_MAP_H
#define WOLFTRUST_FW_STM32H563_MEMORY_MAP_H

#define WT_FLASH_S_BASE          0x0C000000u
#define WT_FLASH_S_SIZE          0x00020000u
#define WT_FLASH_NSC_BASE        0x0C000400u
#define WT_FLASH_NSC_END         0x0C0007FFu

#define WT_FLASH_NS_BASE         0x08000000u
#define WT_GUEST0_FLASH_BASE     0x08002000u
#define WT_GUEST1_FLASH_BASE     0x08012000u
#define WT_GUEST_FLASH_SIZE      0x00010000u

#define WT_RAM_NS_BASE           0x20000000u
#define WT_GUEST0_RAM_BASE       0x20000000u
#define WT_GUEST1_RAM_BASE       0x20004000u
#define WT_GUEST_RAM_SIZE        0x00004000u

#define WT_RAM_S_BASE            0x30020000u
#define WT_RAM_S_SIZE            0x00080000u

#define WT_SHARED_STATUS_ADDR    0x20000000u

#endif
