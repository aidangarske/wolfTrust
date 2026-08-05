/* context.h
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WOLFTRUST_ARCH_ARMV8M_CONTEXT_H
#define WOLFTRUST_ARCH_ARMV8M_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct wt_armv8m_context {
    uint32_t r4_r11[8];
    uintptr_t psp_ns;
    uintptr_t msp_ns;
    uintptr_t vector_table_ns;
    uint32_t control_ns;
    uintptr_t exc_return;
    uintptr_t pc;
    uintptr_t lr;
    uint32_t xpsr;
    bool frame_stacked;
    bool active_exception;
} wt_armv8m_context_t;

/* Temporary alias keeps the H563 guest monitor source compatible. */
typedef wt_armv8m_context_t wt_guest_context_t;

#endif
