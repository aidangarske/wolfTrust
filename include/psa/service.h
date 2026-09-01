/* service.h
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

#ifndef PSA_SERVICE_H
#define PSA_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "psa/client.h"

typedef int32_t psa_client_id_t;
typedef uint32_t psa_signal_t;

typedef struct psa_msg_t {
    int32_t type;
    psa_handle_t handle;
    psa_client_id_t client_id;
    void* rhandle;
    size_t in_size[PSA_MAX_IOVEC];
    size_t out_size[PSA_MAX_IOVEC];
} psa_msg_t;

#define PSA_POLL           0U
#define PSA_BLOCK          0x80000000U
#define PSA_WAIT_ANY       0xFFFFFFFFU
#define PSA_DOORBELL       0x00000008U
#define PSA_IPC_CONNECT    (-1)
#define PSA_IPC_DISCONNECT (-2)

psa_signal_t psa_wait(psa_signal_t signal_mask, uint32_t timeout);
psa_status_t psa_get(psa_signal_t signal, psa_msg_t* msg);
void psa_set_rhandle(psa_handle_t msg_handle, void* rhandle);
size_t psa_read(psa_handle_t msg_handle, uint32_t invec_idx,
                void* buffer, size_t num_bytes);
size_t psa_skip(psa_handle_t msg_handle, uint32_t invec_idx,
                size_t num_bytes);
void psa_write(psa_handle_t msg_handle, uint32_t outvec_idx,
               const void* buffer, size_t num_bytes);
void psa_reply(psa_handle_t msg_handle, psa_status_t status);
void psa_notify(int32_t partition_id);
void psa_clear(void);
void psa_eoi(psa_signal_t irq_signal);
void psa_irq_enable(psa_signal_t irq_signal);
void psa_panic(void) __attribute__((noreturn));

#endif /* PSA_SERVICE_H */
