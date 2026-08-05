/* ipc.h
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

#ifndef WOLFTRUST_IPC_H
#define WOLFTRUST_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WT_IPC_MAX_VECTORS 4U

typedef enum wt_ipc_connection_state {
    WT_IPC_CONNECTION_FREE = 0,
    WT_IPC_CONNECTION_PENDING_CONNECT,
    WT_IPC_CONNECTION_CONNECTING,
    WT_IPC_CONNECTION_IDLE,
    WT_IPC_CONNECTION_PENDING_REQUEST,
    WT_IPC_CONNECTION_ACTIVE,
    WT_IPC_CONNECTION_DISCONNECTING,
    WT_IPC_CONNECTION_ERROR,
    WT_IPC_CONNECTION_TERMINAL
} wt_ipc_connection_state_t;

typedef enum wt_ipc_connection_event {
    WT_IPC_EVENT_CONNECT_ACCEPT = 0,
    WT_IPC_EVENT_CONNECT_COMPLETE,
    WT_IPC_EVENT_REQUEST,
    WT_IPC_EVENT_REQUEST_ACTIVE,
    WT_IPC_EVENT_REPLY,
    WT_IPC_EVENT_CLOSE,
    WT_IPC_EVENT_CLOSE_COMPLETE,
    WT_IPC_EVENT_FAIL,
    WT_IPC_EVENT_ABORT
} wt_ipc_connection_event_t;

typedef enum wt_ipc_handle_type {
    WT_IPC_HANDLE_NONE = 0,
    WT_IPC_HANDLE_CONNECTION = 1,
    WT_IPC_HANDLE_MESSAGE = 2
} wt_ipc_handle_type_t;

typedef struct wt_ipc_handle {
    uint32_t value;
    uint32_t generation;
    uint32_t owner_id;
    wt_ipc_handle_type_t type;
} wt_ipc_handle_t;

typedef struct wt_ipc_connection {
    wt_ipc_connection_state_t state;
    wt_ipc_handle_t handle;
    uint32_t service_sid;
    uint32_t service_version;
    uint32_t owner_id;
    bool stateless;
} wt_ipc_connection_t;

typedef struct wt_ipc_vector {
    uintptr_t base;
    size_t length;
} wt_ipc_vector_t;

typedef enum wt_ipc_result {
    WT_IPC_VALID = 0,
    WT_IPC_ERROR_ARGUMENT = -300,
    WT_IPC_ERROR_HANDLE = -301,
    WT_IPC_ERROR_OWNER = -302,
    WT_IPC_ERROR_STATE = -303,
    WT_IPC_ERROR_POLICY = -304,
    WT_IPC_ERROR_VECTOR_COUNT = -305,
    WT_IPC_ERROR_VECTOR_ADDRESS = -306,
    WT_IPC_ERROR_VECTOR_OVERFLOW = -307,
    WT_IPC_ERROR_VECTOR_LIMIT = -308
} wt_ipc_result_t;

int wt_ipc_handle_create(wt_ipc_handle_t* handle,
                         uint32_t value,
                         uint32_t generation,
                         uint32_t owner_id,
                         wt_ipc_handle_type_t type);

int wt_ipc_handle_matches(const wt_ipc_handle_t* expected,
                          const wt_ipc_handle_t* presented);

int wt_ipc_connection_open(wt_ipc_connection_t* connection,
                           const wt_ipc_handle_t* handle,
                           uint32_t owner_id,
                           uint32_t service_sid,
                           uint32_t service_version,
                           bool stateless);

int wt_ipc_connection_transition(wt_ipc_connection_t* connection,
                                  uint32_t caller_id,
                                  const wt_ipc_handle_t* handle,
                                  wt_ipc_connection_event_t event);

int wt_ipc_validate_vectors(const wt_ipc_vector_t* inputs,
                            size_t input_count,
                            const wt_ipc_vector_t* outputs,
                            size_t output_count,
                            size_t total_limit,
                            size_t* total_length);

#endif
