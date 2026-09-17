/* ipc.c
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

#include "wolftrust/ipc.h"

#include <string.h>

static int wt_ipc_event_valid(wt_ipc_connection_event_t event)
{
    return (unsigned int)event <= (unsigned int)WT_IPC_EVENT_ABORT;
}

static int wt_ipc_state_valid(wt_ipc_connection_state_t state)
{
    return (unsigned int)state <= (unsigned int)WT_IPC_CONNECTION_TERMINAL;
}

int wt_ipc_handle_create(wt_ipc_handle_t* handle,
                         uint32_t value,
                         uint32_t generation,
                         uint32_t owner_id,
                         wt_ipc_handle_type_t type)
{
    if (handle == NULL || value == 0U || generation == 0U ||
            owner_id == 0U || (unsigned int)type >
            (unsigned int)WT_IPC_HANDLE_MESSAGE ||
            type == WT_IPC_HANDLE_NONE) {
        return WT_IPC_ERROR_HANDLE;
    }

    handle->value = value;
    handle->generation = generation;
    handle->owner_id = owner_id;
    handle->type = type;
    return WT_IPC_VALID;
}

int wt_ipc_handle_matches(const wt_ipc_handle_t* expected,
                          const wt_ipc_handle_t* presented)
{
    if (expected == NULL || presented == NULL)
        return WT_IPC_ERROR_HANDLE;

    if (expected->value == 0U || expected->generation == 0U ||
            expected->owner_id == 0U ||
            expected->type == WT_IPC_HANDLE_NONE ||
            (unsigned int)expected->type >
                (unsigned int)WT_IPC_HANDLE_MESSAGE ||
            presented->value == 0U || presented->generation == 0U ||
            presented->owner_id == 0U ||
            presented->type == WT_IPC_HANDLE_NONE ||
            (unsigned int)presented->type >
                (unsigned int)WT_IPC_HANDLE_MESSAGE) {
        return WT_IPC_ERROR_HANDLE;
    }

    if (memcmp(expected, presented, sizeof(*expected)) != 0)
        return WT_IPC_ERROR_HANDLE;

    return WT_IPC_VALID;
}

int wt_ipc_connection_open(wt_ipc_connection_t* connection,
                           const wt_ipc_handle_t* handle,
                           uint32_t owner_id,
                           uint32_t service_sid,
                           uint32_t service_version,
                           bool stateless)
{
    if (connection == NULL || handle == NULL || owner_id == 0U ||
            service_sid == 0U || service_version == 0U ||
            handle->type != WT_IPC_HANDLE_CONNECTION ||
            handle->owner_id != owner_id ||
            wt_ipc_handle_matches(handle, handle) != WT_IPC_VALID) {
        return WT_IPC_ERROR_ARGUMENT;
    }

    (void)memset(connection, 0, sizeof(*connection));
    connection->state = stateless ? WT_IPC_CONNECTION_IDLE :
                                    WT_IPC_CONNECTION_PENDING_CONNECT;
    connection->handle = *handle;
    connection->service_sid = service_sid;
    connection->service_version = service_version;
    connection->owner_id = owner_id;
    connection->stateless = stateless;
    return WT_IPC_VALID;
}

int wt_ipc_connection_transition(wt_ipc_connection_t* connection,
                                  uint32_t caller_id,
                                  const wt_ipc_handle_t* handle,
                                  wt_ipc_connection_event_t event)
{
    wt_ipc_connection_state_t next_state;

    if (connection == NULL || handle == NULL || caller_id == 0U ||
            !wt_ipc_event_valid(event) ||
            !wt_ipc_state_valid(connection->state)) {
        return WT_IPC_ERROR_ARGUMENT;
    }
    if (caller_id != connection->owner_id)
        return WT_IPC_ERROR_OWNER;
    if (wt_ipc_handle_matches(&connection->handle, handle) != WT_IPC_VALID)
        return WT_IPC_ERROR_HANDLE;

    next_state = connection->state;
    switch (event) {
        case WT_IPC_EVENT_CONNECT_ACCEPT:
            if (connection->stateless || connection->state !=
                    WT_IPC_CONNECTION_PENDING_CONNECT)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_CONNECTING;
            break;

        case WT_IPC_EVENT_CONNECT_COMPLETE:
            if (connection->stateless || connection->state !=
                    WT_IPC_CONNECTION_CONNECTING)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_IDLE;
            break;

        case WT_IPC_EVENT_REQUEST:
            if (connection->state != WT_IPC_CONNECTION_IDLE)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_PENDING_REQUEST;
            break;

        case WT_IPC_EVENT_REQUEST_ACTIVE:
            if (connection->state != WT_IPC_CONNECTION_PENDING_REQUEST)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_ACTIVE;
            break;

        case WT_IPC_EVENT_REPLY:
            if (connection->state != WT_IPC_CONNECTION_ACTIVE)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_IDLE;
            break;

        case WT_IPC_EVENT_CLOSE:
            if (connection->stateless || connection->state !=
                    WT_IPC_CONNECTION_IDLE)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_DISCONNECTING;
            break;

        case WT_IPC_EVENT_CLOSE_COMPLETE:
            if (connection->state != WT_IPC_CONNECTION_DISCONNECTING)
                return WT_IPC_ERROR_STATE;
            (void)memset(connection, 0, sizeof(*connection));
            connection->state = WT_IPC_CONNECTION_FREE;
            return WT_IPC_VALID;

        case WT_IPC_EVENT_FAIL:
            if (connection->state == WT_IPC_CONNECTION_FREE ||
                    connection->state == WT_IPC_CONNECTION_TERMINAL)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_ERROR;
            break;

        case WT_IPC_EVENT_ABORT:
            if (connection->state == WT_IPC_CONNECTION_FREE ||
                    connection->state == WT_IPC_CONNECTION_TERMINAL)
                return WT_IPC_ERROR_STATE;
            next_state = WT_IPC_CONNECTION_TERMINAL;
            break;
    }

    connection->state = next_state;
    return WT_IPC_VALID;
}

static int wt_ipc_validate_vector_list(const wt_ipc_vector_t* vectors,
                                       size_t count,
                                       size_t* total_length)
{
    size_t i;

    if (count != 0U && vectors == NULL)
        return WT_IPC_ERROR_ARGUMENT;

    for (i = 0U; i < count; i++) {
        const wt_ipc_vector_t* vector = &vectors[i];

        if (vector->length != 0U && vector->base == 0U)
            return WT_IPC_ERROR_VECTOR_ADDRESS;
        if (vector->length > (size_t)(UINTPTR_MAX - vector->base))
            return WT_IPC_ERROR_VECTOR_OVERFLOW;
        if (vector->length > SIZE_MAX - *total_length)
            return WT_IPC_ERROR_VECTOR_OVERFLOW;
        *total_length += vector->length;
    }

    return WT_IPC_VALID;
}

int wt_ipc_validate_vectors(const wt_ipc_vector_t* inputs,
                            size_t input_count,
                            const wt_ipc_vector_t* outputs,
                            size_t output_count,
                            size_t total_limit,
                            size_t* total_length)
{
    int result;
    size_t total = 0U;

    if (total_length == NULL)
        return WT_IPC_ERROR_ARGUMENT;
    if (input_count > WT_IPC_MAX_VECTORS ||
            output_count > WT_IPC_MAX_VECTORS ||
            input_count > WT_IPC_MAX_VECTORS - output_count) {
        return WT_IPC_ERROR_VECTOR_COUNT;
    }

    result = wt_ipc_validate_vector_list(inputs, input_count, &total);
    if (result != WT_IPC_VALID)
        return result;
    result = wt_ipc_validate_vector_list(outputs, output_count, &total);
    if (result != WT_IPC_VALID)
        return result;
    if (total_limit != 0U && total > total_limit)
        return WT_IPC_ERROR_VECTOR_LIMIT;

    *total_length = total;
    return WT_IPC_VALID;
}
