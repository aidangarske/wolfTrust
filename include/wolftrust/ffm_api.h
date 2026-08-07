/* ffm_api.h
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

#ifndef WOLFTRUST_FFM_API_H
#define WOLFTRUST_FFM_API_H

#include "wolftrust/ffm.h"

typedef struct wt_ffm_identity_ops {
    psa_client_id_t (*current_client)(void* context);
    int32_t (*current_partition)(void* context);
} wt_ffm_identity_ops_t;

int wt_ffm_api_bind(wt_ffm_runtime_t* runtime,
                    const wt_ffm_identity_ops_t* identity_ops,
                    void* identity_context);
void wt_ffm_api_unbind(void);

#endif /* WOLFTRUST_FFM_API_H */
