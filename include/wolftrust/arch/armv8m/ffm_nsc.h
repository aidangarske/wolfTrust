/* ffm_nsc.h
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

#ifndef WOLFTRUST_ARCH_ARMV8M_FFM_NSC_H
#define WOLFTRUST_ARCH_ARMV8M_FFM_NSC_H

/* Install the Armv8-M CMSE NS-window checks into the neutral FF-M boot core
 * (wt_ffm_boot_set_memcheck). Call at platform init, before any NS guest can
 * reach the WolfTrust_FFM_* secure-gateway veneers; until installed the core
 * fails closed and rejects every NS window. */
void wt_ffm_nsc_install(void);

#endif /* WOLFTRUST_ARCH_ARMV8M_FFM_NSC_H */
