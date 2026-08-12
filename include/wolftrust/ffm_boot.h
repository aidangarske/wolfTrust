/* ffm_boot.h
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

#ifndef WOLFTRUST_FFM_BOOT_H
#define WOLFTRUST_FFM_BOOT_H

#include "wolftrust/ffm.h"
#include "wolftrust/ffm_veneer.h"
#include "wolftrust/manifest.h"

int wt_ffm_boot_init(const wt_system_manifest_t* manifest);
/* Upgrade the crypto partition to a scheduled unprivileged coroutine (P1t).
 * Call after wt_tasklet_init; requires the coroutine scheduler, so the
 * WT_ENGINE_HSM boot path invokes it once the scheduler is up. */
int wt_ffm_boot_start_sched(void);
const wt_ffm_runtime_t* wt_ffm_boot_runtime(void);

#endif /* WOLFTRUST_FFM_BOOT_H */
