/* ffm.c
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

#include "wolftrust/ffm.h"

#include <string.h>

#define WT_FFM_HANDLE_INDEX_MASK 0x1FU
#define WT_FFM_HANDLE_TYPE_SHIFT 5U
#define WT_FFM_HANDLE_TYPE_MASK  0x3U
#define WT_FFM_HANDLE_GEN_SHIFT  7U
#define WT_FFM_HANDLE_GEN_MASK   0x00FFFFFFU
#define WT_FFM_HANDLE_CONNECTION 1U
#define WT_FFM_HANDLE_MESSAGE    2U

static uint32_t wt_ffm_next_generation(uint32_t generation)
{
    generation = (generation + 1U) & WT_FFM_HANDLE_GEN_MASK;
    return generation == 0U ? 1U : generation;
}

static psa_handle_t wt_ffm_make_handle(uint32_t type, uint16_t index,
                                       uint32_t generation)
{
    uint32_t value;

    value = ((generation & WT_FFM_HANDLE_GEN_MASK) <<
             WT_FFM_HANDLE_GEN_SHIFT) |
            ((type & WT_FFM_HANDLE_TYPE_MASK) <<
             WT_FFM_HANDLE_TYPE_SHIFT) |
            ((uint32_t)index + 1U);
    return (psa_handle_t)value;
}

static int wt_ffm_decode_handle(psa_handle_t handle, uint32_t type,
                                size_t limit, uint16_t* index,
                                uint32_t* generation)
{
    uint32_t value;
    uint32_t encoded_type;
    uint32_t encoded_index;

    if (handle <= 0 || index == NULL || generation == NULL)
        return WT_FFM_ERROR_HANDLE;

    value = (uint32_t)handle;
    encoded_type = (value >> WT_FFM_HANDLE_TYPE_SHIFT) &
                   WT_FFM_HANDLE_TYPE_MASK;
    encoded_index = value & WT_FFM_HANDLE_INDEX_MASK;
    if (encoded_type != type || encoded_index == 0U ||
            encoded_index > limit) {
        return WT_FFM_ERROR_HANDLE;
    }

    *index = (uint16_t)(encoded_index - 1U);
    *generation = value >> WT_FFM_HANDLE_GEN_SHIFT;
    if (*generation == 0U)
        return WT_FFM_ERROR_HANDLE;

    return WT_FFM_SUCCESS;
}

static int wt_ffm_find_partition(const wt_ffm_runtime_t* runtime,
                                 int32_t partition_id,
                                 uint16_t* partition_index)
{
    size_t i;

    if (runtime == NULL || partition_index == NULL || partition_id <= 0)
        return WT_FFM_ERROR_ARGUMENT;

    for (i = 0U; i < runtime->partition_count; i++) {
        if (runtime->partitions[i].manifest->domain_id ==
                (wt_domain_id_t)partition_id) {
            *partition_index = (uint16_t)i;
            return WT_FFM_SUCCESS;
        }
    }

    return WT_FFM_ERROR_POLICY;
}

static int wt_ffm_find_service(const wt_ffm_runtime_t* runtime, uint32_t sid,
                               uint16_t* service_index)
{
    size_t i;

    if (runtime == NULL || service_index == NULL || sid == 0U)
        return WT_FFM_ERROR_ARGUMENT;

    for (i = 0U; i < runtime->service_count; i++) {
        if (runtime->services[i].descriptor->sid == sid) {
            *service_index = (uint16_t)i;
            return WT_FFM_SUCCESS;
        }
    }

    return WT_FFM_ERROR_POLICY;
}

static int wt_ffm_caller_allowed(const wt_ffm_runtime_t* runtime,
                                 psa_client_id_t caller,
                                 uint16_t service_index)
{
    const wt_ffm_service_runtime_t* service;
    const wt_partition_manifest_t* partition;
    size_t i;
    size_t j;

    if (caller == 0 || service_index >= runtime->service_count)
        return 0;

    service = &runtime->services[service_index];
    if (caller < 0)
        return service->descriptor->nonsecure_clients != 0U;

    for (i = 0U; i < runtime->partition_count; i++) {
        partition = runtime->partitions[i].manifest;
        if (partition->domain_id != (wt_domain_id_t)caller)
            continue;
        if (i == service->partition_index)
            return 0;
        for (j = 0U; j < partition->dependency_count; j++) {
            if (partition->dependencies[j] == service->descriptor->sid)
                return 1;
        }
        return 0;
    }

    return 0;
}

static int wt_ffm_version_allowed(const wt_service_descriptor_t* service,
                                  uint32_t requested)
{
    if (requested == 0U)
        return 0;
    if (service->version_policy == WT_SERVICE_VERSION_STRICT)
        return requested == service->version;
    return requested <= service->version;
}

static int wt_ffm_alloc_connection(wt_ffm_runtime_t* runtime,
                                   uint16_t* connection_index)
{
    size_t i;

    for (i = 0U; i < WT_FFM_MAX_CONNECTIONS; i++) {
        if (runtime->connections[i].allocated == 0U) {
            runtime->connections[i].allocated = 1U;
            runtime->connections[i].state = WT_IPC_CONNECTION_FREE;
            *connection_index = (uint16_t)i;
            return WT_FFM_SUCCESS;
        }
    }

    return WT_FFM_ERROR_RESOURCE;
}

static void wt_ffm_release_connection(wt_ffm_runtime_t* runtime,
                                      uint16_t connection_index)
{
    wt_ffm_connection_runtime_t* connection;
    uint32_t generation;

    connection = &runtime->connections[connection_index];
    generation = wt_ffm_next_generation(connection->generation);
    (void)memset(connection, 0, sizeof(*connection));
    connection->generation = generation;
    connection->state = WT_IPC_CONNECTION_FREE;
}

static int wt_ffm_connection_from_handle(wt_ffm_runtime_t* runtime,
                                         psa_client_id_t caller,
                                         psa_handle_t handle,
                                         uint16_t* connection_index)
{
    wt_ffm_connection_runtime_t* connection;
    uint32_t generation;
    uint16_t index;
    int ret;

    ret = wt_ffm_decode_handle(handle, WT_FFM_HANDLE_CONNECTION,
                               WT_FFM_MAX_CONNECTIONS, &index, &generation);
    if (ret != WT_FFM_SUCCESS)
        return ret;

    connection = &runtime->connections[index];
    if (connection->allocated == 0U ||
            connection->generation != generation)
        return WT_FFM_ERROR_HANDLE;
    if (connection->caller != caller)
        return WT_FFM_ERROR_POLICY;

    *connection_index = index;
    return WT_FFM_SUCCESS;
}

static int wt_ffm_alloc_message(wt_ffm_runtime_t* runtime,
                                uint16_t* message_index)
{
    size_t i;

    for (i = 0U; i < WT_FFM_MAX_MESSAGES; i++) {
        if (runtime->messages[i].allocated == 0U) {
            runtime->messages[i].allocated = 1U;
            runtime->messages[i].next = WT_FFM_QUEUE_NONE;
            *message_index = (uint16_t)i;
            return WT_FFM_SUCCESS;
        }
    }

    return WT_FFM_ERROR_RESOURCE;
}

static void wt_ffm_release_message(wt_ffm_runtime_t* runtime,
                                   uint16_t message_index)
{
    wt_ffm_message_runtime_t* message;
    uint32_t generation;

    message = &runtime->messages[message_index];
    generation = wt_ffm_next_generation(message->generation);
    (void)memset(message, 0, sizeof(*message));
    message->generation = generation;
    message->next = WT_FFM_QUEUE_NONE;
}

static int wt_ffm_message_from_handle(wt_ffm_runtime_t* runtime,
                                      int32_t partition_id,
                                      psa_handle_t handle,
                                      uint16_t* message_index)
{
    wt_ffm_message_runtime_t* message;
    wt_ffm_service_runtime_t* service;
    uint32_t generation;
    uint16_t index;
    int ret;

    ret = wt_ffm_decode_handle(handle, WT_FFM_HANDLE_MESSAGE,
                               WT_FFM_MAX_MESSAGES, &index, &generation);
    if (ret != WT_FFM_SUCCESS)
        return ret;

    message = &runtime->messages[index];
    if (message->allocated == 0U || message->active == 0U ||
            message->generation != generation)
        return WT_FFM_ERROR_HANDLE;

    service = &runtime->services[message->service_index];
    if (runtime->partitions[service->partition_index].manifest->domain_id !=
            (wt_domain_id_t)partition_id) {
        return WT_FFM_ERROR_POLICY;
    }

    *message_index = index;
    return WT_FFM_SUCCESS;
}

static void wt_ffm_update_service_signal(wt_ffm_runtime_t* runtime,
                                         uint16_t service_index)
{
    wt_ffm_service_runtime_t* service;
    wt_ffm_partition_runtime_t* partition;
    uint32_t signal;

    service = &runtime->services[service_index];
    partition = &runtime->partitions[service->partition_index];
    signal = service->descriptor->signal;
    if (service->queue_head == WT_FFM_QUEUE_NONE)
        partition->asserted_signals &= ~signal;
    else
        partition->asserted_signals |= signal;
}

static void wt_ffm_enqueue(wt_ffm_runtime_t* runtime, uint16_t service_index,
                           uint16_t message_index)
{
    wt_ffm_service_runtime_t* service;

    service = &runtime->services[service_index];
    if (service->queue_tail == WT_FFM_QUEUE_NONE)
        service->queue_head = message_index;
    else
        runtime->messages[service->queue_tail].next = message_index;
    service->queue_tail = message_index;
    wt_ffm_update_service_signal(runtime, service_index);
}

static int wt_ffm_prepare_vectors(wt_ffm_runtime_t* runtime,
                                  wt_ffm_message_runtime_t* message,
                                  const psa_invec* in_vec, size_t in_len,
                                  psa_outvec* out_vec, size_t out_len)
{
    wt_ipc_vector_t inputs[PSA_MAX_IOVEC];
    wt_ipc_vector_t outputs[PSA_MAX_IOVEC];
    size_t input_offset = 0U;
    size_t output_offset = 0U;
    size_t total = 0U;
    size_t i;
    int ret;

    if ((in_len != 0U && in_vec == NULL) ||
            (out_len != 0U && out_vec == NULL))
        return WT_FFM_ERROR_ARGUMENT;

    for (i = 0U; i < in_len && i < PSA_MAX_IOVEC; i++) {
        inputs[i].base = (uintptr_t)in_vec[i].base;
        inputs[i].length = in_vec[i].len;
    }
    for (i = 0U; i < out_len && i < PSA_MAX_IOVEC; i++) {
        outputs[i].base = (uintptr_t)out_vec[i].base;
        outputs[i].length = out_vec[i].len;
    }
    ret = wt_ipc_validate_vectors(inputs, in_len, outputs, out_len,
                                  WT_FFM_TRANSFER_BYTES, &total);
    if (ret != WT_IPC_VALID)
        return WT_FFM_ERROR_BUFFER;

    message->in_count = in_len;
    message->out_count = out_len;
    for (i = 0U; i < in_len; i++) {
        if (in_vec[i].len != 0U &&
                runtime->ops->check_read(runtime->port_context,
                    message->caller, in_vec[i].base, in_vec[i].len) == 0) {
            return WT_FFM_ERROR_POLICY;
        }
        message->in_offset[i] = input_offset;
        message->in_size[i] = in_vec[i].len;
        if (in_vec[i].len != 0U)
            (void)memcpy(&message->input[input_offset], in_vec[i].base,
                         in_vec[i].len);
        input_offset += in_vec[i].len;
    }
    for (i = 0U; i < out_len; i++) {
        if (out_vec[i].len != 0U &&
                runtime->ops->check_write(runtime->port_context,
                    message->caller, out_vec[i].base, out_vec[i].len) == 0) {
            return WT_FFM_ERROR_POLICY;
        }
        message->out_offset[i] = output_offset;
        message->out_size[i] = out_vec[i].len;
        message->client_output[i] = out_vec[i].base;
        output_offset += out_vec[i].len;
    }

    return WT_FFM_SUCCESS;
}

static int wt_ffm_dispatch_message(wt_ffm_runtime_t* runtime,
                                   uint16_t message_index)
{
    wt_ffm_message_runtime_t* message = &runtime->messages[message_index];
    wt_ffm_service_runtime_t* service;
    int32_t partition_id;
    int ret;

    service = &runtime->services[message->service_index];
    partition_id = (int32_t)runtime->partitions[
        service->partition_index].manifest->domain_id;
    ret = runtime->ops->dispatch(runtime->port_context, runtime,
                                 partition_id);
    if (ret != WT_FFM_SUCCESS)
        return ret;
    if (message->complete == 0U)
        return WT_FFM_ERROR_NOT_READY;
    return WT_FFM_SUCCESS;
}

int wt_ffm_init(wt_ffm_runtime_t* runtime,
                const wt_system_manifest_t* manifest,
                const wt_ffm_port_ops_t* ops, void* port_context)
{
    size_t service_count = 0U;
    size_t i;
    size_t j;

    if (runtime == NULL || manifest == NULL || ops == NULL ||
            ops->check_read == NULL || ops->check_write == NULL ||
            ops->dispatch == NULL || ops->panic == NULL) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    if (manifest->partition_count > WT_FFM_MAX_PARTITIONS)
        return WT_FFM_ERROR_MANIFEST;
    for (i = 0U; i < manifest->partition_count; i++) {
        if (manifest->partitions[i].model != WT_PARTITION_MODEL_IPC)
            return WT_FFM_ERROR_MANIFEST;
        if (manifest->partitions[i].service_count >
                WT_FFM_MAX_SERVICES - service_count) {
            return WT_FFM_ERROR_MANIFEST;
        }
        service_count += manifest->partitions[i].service_count;
    }

    (void)memset(runtime, 0, sizeof(*runtime));
    runtime->manifest = manifest;
    runtime->ops = ops;
    runtime->port_context = port_context;
    runtime->partition_count = manifest->partition_count;
    runtime->service_count = service_count;
    service_count = 0U;
    for (i = 0U; i < manifest->partition_count; i++) {
        runtime->partitions[i].manifest = &manifest->partitions[i];
        for (j = 0U; j < manifest->partitions[i].service_count; j++) {
            runtime->services[service_count].descriptor =
                &manifest->partitions[i].services[j];
            runtime->services[service_count].partition_index = (uint16_t)i;
            runtime->services[service_count].queue_head = WT_FFM_QUEUE_NONE;
            runtime->services[service_count].queue_tail = WT_FFM_QUEUE_NONE;
            service_count++;
        }
    }
    for (i = 0U; i < WT_FFM_MAX_CONNECTIONS; i++)
        runtime->connections[i].generation = 1U;
    for (i = 0U; i < WT_FFM_MAX_MESSAGES; i++) {
        runtime->messages[i].generation = 1U;
        runtime->messages[i].next = WT_FFM_QUEUE_NONE;
    }

    return WT_FFM_SUCCESS;
}

uint32_t wt_ffm_framework_version(const wt_ffm_runtime_t* runtime)
{
    if (runtime == NULL || runtime->manifest == NULL)
        return 0U;
    return PSA_FRAMEWORK_VERSION;
}

uint32_t wt_ffm_service_version(const wt_ffm_runtime_t* runtime,
                                psa_client_id_t caller, uint32_t sid)
{
    uint16_t service_index;

    if (wt_ffm_find_service(runtime, sid, &service_index) != WT_FFM_SUCCESS ||
            !wt_ffm_caller_allowed(runtime, caller, service_index)) {
        return PSA_VERSION_NONE;
    }
    return runtime->services[service_index].descriptor->version;
}

psa_handle_t wt_ffm_connect(wt_ffm_runtime_t* runtime,
                            psa_client_id_t caller, uint32_t sid,
                            uint32_t version)
{
    wt_ffm_connection_runtime_t* connection;
    wt_ffm_message_runtime_t* message;
    uint16_t connection_index;
    uint16_t message_index;
    uint16_t service_index;
    psa_handle_t handle;
    psa_status_t status;
    int ret;

    if (runtime == NULL || caller == 0)
        return (psa_handle_t)PSA_ERROR_INVALID_ARGUMENT;
    ret = wt_ffm_find_service(runtime, sid, &service_index);
    if (ret != WT_FFM_SUCCESS ||
            !wt_ffm_caller_allowed(runtime, caller, service_index) ||
            !wt_ffm_version_allowed(
                runtime->services[service_index].descriptor, version)) {
        return (psa_handle_t)PSA_ERROR_CONNECTION_REFUSED;
    }
    if (runtime->services[service_index].descriptor->connection_based == 0U)
        return (psa_handle_t)PSA_ERROR_NOT_SUPPORTED;

    if (wt_ffm_alloc_connection(runtime, &connection_index) !=
            WT_FFM_SUCCESS)
        return (psa_handle_t)PSA_ERROR_CONNECTION_BUSY;
    if (wt_ffm_alloc_message(runtime, &message_index) != WT_FFM_SUCCESS) {
        wt_ffm_release_connection(runtime, connection_index);
        return (psa_handle_t)PSA_ERROR_CONNECTION_BUSY;
    }

    connection = &runtime->connections[connection_index];
    connection->caller = caller;
    connection->service_index = service_index;
    connection->state = WT_IPC_CONNECTION_PENDING_CONNECT;
    handle = wt_ffm_make_handle(WT_FFM_HANDLE_CONNECTION, connection_index,
                                connection->generation);

    message = &runtime->messages[message_index];
    message->caller = caller;
    message->connection_index = connection_index;
    message->service_index = service_index;
    message->type = PSA_IPC_CONNECT;
    wt_ffm_enqueue(runtime, service_index, message_index);
    ret = wt_ffm_dispatch_message(runtime, message_index);
    status = message->reply_status;
    wt_ffm_release_message(runtime, message_index);
    if (ret != WT_FFM_SUCCESS || status != PSA_SUCCESS) {
        wt_ffm_release_connection(runtime, connection_index);
        return ret == WT_FFM_ERROR_RESOURCE ?
            (psa_handle_t)PSA_ERROR_CONNECTION_BUSY :
            (psa_handle_t)(status == PSA_SUCCESS ?
                PSA_ERROR_GENERIC_ERROR : status);
    }

    return handle;
}

psa_status_t wt_ffm_call(wt_ffm_runtime_t* runtime,
                         psa_client_id_t caller, psa_handle_t handle,
                         int32_t type, const psa_invec* in_vec,
                         size_t in_len, psa_outvec* out_vec,
                         size_t out_len)
{
    wt_ffm_connection_runtime_t* connection;
    wt_ffm_message_runtime_t* message;
    uint16_t connection_index;
    uint16_t message_index;
    psa_status_t status;
    size_t i;
    int ret;

    if (runtime == NULL || caller == 0 || type < 0)
        return PSA_ERROR_INVALID_ARGUMENT;
    ret = wt_ffm_connection_from_handle(runtime, caller, handle,
                                        &connection_index);
    if (ret != WT_FFM_SUCCESS)
        return ret == WT_FFM_ERROR_POLICY ? PSA_ERROR_NOT_PERMITTED :
                                            PSA_ERROR_PROGRAMMER_ERROR;
    connection = &runtime->connections[connection_index];
    if (connection->state != WT_IPC_CONNECTION_IDLE)
        return PSA_ERROR_BAD_STATE;
    if (wt_ffm_alloc_message(runtime, &message_index) != WT_FFM_SUCCESS)
        return PSA_ERROR_INSUFFICIENT_MEMORY;

    message = &runtime->messages[message_index];
    message->caller = caller;
    message->connection_index = connection_index;
    message->service_index = connection->service_index;
    message->type = type;
    ret = wt_ffm_prepare_vectors(runtime, message, in_vec, in_len,
                                 out_vec, out_len);
    if (ret != WT_FFM_SUCCESS) {
        wt_ffm_release_message(runtime, message_index);
        return ret == WT_FFM_ERROR_BUFFER ? PSA_ERROR_INVALID_ARGUMENT :
                                            PSA_ERROR_NOT_PERMITTED;
    }

    connection->state = WT_IPC_CONNECTION_PENDING_REQUEST;
    wt_ffm_enqueue(runtime, connection->service_index, message_index);
    ret = wt_ffm_dispatch_message(runtime, message_index);
    if (ret != WT_FFM_SUCCESS) {
        connection->state = WT_IPC_CONNECTION_ERROR;
        wt_ffm_release_message(runtime, message_index);
        return PSA_ERROR_GENERIC_ERROR;
    }

    status = message->reply_status;
    for (i = 0U; i < message->out_count; i++) {
        if (message->out_position[i] != 0U) {
            if (runtime->ops->check_write(runtime->port_context,
                    message->caller, message->client_output[i],
                    message->out_position[i]) == 0) {
                connection->state = WT_IPC_CONNECTION_ERROR;
                wt_ffm_release_message(runtime, message_index);
                return PSA_ERROR_NOT_PERMITTED;
            }
            (void)memcpy(message->client_output[i],
                &message->output[message->out_offset[i]],
                message->out_position[i]);
        }
        out_vec[i].len = message->out_position[i];
    }
    wt_ffm_release_message(runtime, message_index);
    return status;
}

int wt_ffm_close(wt_ffm_runtime_t* runtime, psa_client_id_t caller,
                 psa_handle_t handle)
{
    wt_ffm_connection_runtime_t* connection;
    wt_ffm_message_runtime_t* message;
    uint16_t connection_index;
    uint16_t message_index;
    int ret;

    if (handle == PSA_NULL_HANDLE)
        return WT_FFM_SUCCESS;
    if (runtime == NULL || caller == 0)
        return WT_FFM_ERROR_ARGUMENT;
    ret = wt_ffm_connection_from_handle(runtime, caller, handle,
                                        &connection_index);
    if (ret != WT_FFM_SUCCESS)
        return ret;
    connection = &runtime->connections[connection_index];
    if (connection->state != WT_IPC_CONNECTION_IDLE)
        return WT_FFM_ERROR_STATE;
    if (wt_ffm_alloc_message(runtime, &message_index) != WT_FFM_SUCCESS)
        return WT_FFM_ERROR_RESOURCE;

    connection->state = WT_IPC_CONNECTION_DISCONNECTING;
    message = &runtime->messages[message_index];
    message->caller = caller;
    message->connection_index = connection_index;
    message->service_index = connection->service_index;
    message->type = PSA_IPC_DISCONNECT;
    wt_ffm_enqueue(runtime, connection->service_index, message_index);
    ret = wt_ffm_dispatch_message(runtime, message_index);
    wt_ffm_release_message(runtime, message_index);
    if (ret == WT_FFM_SUCCESS)
        wt_ffm_release_connection(runtime, connection_index);
    return ret;
}

int wt_ffm_wait(wt_ffm_runtime_t* runtime, int32_t partition_id,
                psa_signal_t signal_mask, psa_signal_t* asserted)
{
    uint16_t partition_index;

    if (runtime == NULL || asserted == NULL || signal_mask == 0U)
        return WT_FFM_ERROR_ARGUMENT;
    if (wt_ffm_find_partition(runtime, partition_id, &partition_index) !=
            WT_FFM_SUCCESS)
        return WT_FFM_ERROR_POLICY;
    *asserted = runtime->partitions[partition_index].asserted_signals &
                signal_mask;
    return *asserted == 0U ? WT_FFM_ERROR_NOT_READY : WT_FFM_SUCCESS;
}

psa_status_t wt_ffm_get(wt_ffm_runtime_t* runtime, int32_t partition_id,
                        psa_signal_t signal, psa_msg_t* msg)
{
    wt_ffm_service_runtime_t* service = NULL;
    wt_ffm_message_runtime_t* message;
    wt_ffm_connection_runtime_t* connection;
    uint16_t partition_index;
    uint16_t message_index;
    size_t i;

    if (runtime == NULL || msg == NULL || signal == 0U ||
            wt_ffm_find_partition(runtime, partition_id,
                &partition_index) != WT_FFM_SUCCESS) {
        return PSA_ERROR_PROGRAMMER_ERROR;
    }
    for (i = 0U; i < runtime->service_count; i++) {
        if (runtime->services[i].partition_index == partition_index &&
                runtime->services[i].descriptor->signal == signal) {
            service = &runtime->services[i];
            break;
        }
    }
    if (service == NULL || service->queue_head == WT_FFM_QUEUE_NONE)
        return PSA_ERROR_DOES_NOT_EXIST;

    message_index = service->queue_head;
    message = &runtime->messages[message_index];
    service->queue_head = message->next;
    if (service->queue_head == WT_FFM_QUEUE_NONE)
        service->queue_tail = WT_FFM_QUEUE_NONE;
    message->next = WT_FFM_QUEUE_NONE;
    message->active = 1U;
    wt_ffm_update_service_signal(runtime, message->service_index);

    connection = &runtime->connections[message->connection_index];
    if (message->type == PSA_IPC_CONNECT)
        connection->state = WT_IPC_CONNECTION_CONNECTING;
    else if (message->type >= PSA_IPC_CALL)
        connection->state = WT_IPC_CONNECTION_ACTIVE;

    (void)memset(msg, 0, sizeof(*msg));
    msg->handle = wt_ffm_make_handle(WT_FFM_HANDLE_MESSAGE, message_index,
                                     message->generation);
    msg->type = message->type;
    msg->client_id = message->caller;
    msg->rhandle = (void*)connection->rhandle;
    for (i = 0U; i < PSA_MAX_IOVEC; i++) {
        msg->in_size[i] = message->in_size[i];
        msg->out_size[i] = message->out_size[i];
    }
    return PSA_SUCCESS;
}

int wt_ffm_set_rhandle(wt_ffm_runtime_t* runtime, int32_t partition_id,
                       psa_handle_t msg_handle, void* rhandle)
{
    wt_ffm_message_runtime_t* message;
    uint16_t message_index;

    if (wt_ffm_message_from_handle(runtime, partition_id, msg_handle,
            &message_index) != WT_FFM_SUCCESS)
        return WT_FFM_ERROR_HANDLE;
    message = &runtime->messages[message_index];
    if (message->type == PSA_IPC_DISCONNECT)
        return WT_FFM_ERROR_STATE;
    runtime->connections[message->connection_index].rhandle =
        (uintptr_t)rhandle;
    return WT_FFM_SUCCESS;
}

size_t wt_ffm_read(wt_ffm_runtime_t* runtime, int32_t partition_id,
                   psa_handle_t msg_handle, uint32_t invec_idx,
                   void* buffer, size_t num_bytes)
{
    wt_ffm_message_runtime_t* message;
    uint16_t message_index;
    size_t remaining;
    size_t length;

    if (buffer == NULL || invec_idx >= PSA_MAX_IOVEC ||
            wt_ffm_message_from_handle(runtime, partition_id, msg_handle,
                &message_index) != WT_FFM_SUCCESS) {
        return 0U;
    }
    message = &runtime->messages[message_index];
    if (message->type < PSA_IPC_CALL || invec_idx >= message->in_count)
        return 0U;
    remaining = message->in_size[invec_idx] -
                message->in_position[invec_idx];
    length = num_bytes < remaining ? num_bytes : remaining;
    if (length != 0U) {
        (void)memcpy(buffer,
            &message->input[message->in_offset[invec_idx] +
                            message->in_position[invec_idx]], length);
        message->in_position[invec_idx] += length;
    }
    return length;
}

size_t wt_ffm_skip(wt_ffm_runtime_t* runtime, int32_t partition_id,
                   psa_handle_t msg_handle, uint32_t invec_idx,
                   size_t num_bytes)
{
    wt_ffm_message_runtime_t* message;
    uint16_t message_index;
    size_t remaining;
    size_t length;

    if (invec_idx >= PSA_MAX_IOVEC ||
            wt_ffm_message_from_handle(runtime, partition_id, msg_handle,
                &message_index) != WT_FFM_SUCCESS) {
        return 0U;
    }
    message = &runtime->messages[message_index];
    if (message->type < PSA_IPC_CALL || invec_idx >= message->in_count)
        return 0U;
    remaining = message->in_size[invec_idx] -
                message->in_position[invec_idx];
    length = num_bytes < remaining ? num_bytes : remaining;
    message->in_position[invec_idx] += length;
    return length;
}

int wt_ffm_write(wt_ffm_runtime_t* runtime, int32_t partition_id,
                 psa_handle_t msg_handle, uint32_t outvec_idx,
                 const void* buffer, size_t num_bytes)
{
    wt_ffm_message_runtime_t* message;
    uint16_t message_index;
    size_t remaining;

    if ((buffer == NULL && num_bytes != 0U) ||
            outvec_idx >= PSA_MAX_IOVEC ||
            wt_ffm_message_from_handle(runtime, partition_id, msg_handle,
                &message_index) != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    message = &runtime->messages[message_index];
    if (message->type < PSA_IPC_CALL || outvec_idx >= message->out_count)
        return WT_FFM_ERROR_STATE;
    remaining = message->out_size[outvec_idx] -
                message->out_position[outvec_idx];
    if (num_bytes > remaining)
        return WT_FFM_ERROR_BUFFER;
    if (num_bytes != 0U) {
        (void)memcpy(&message->output[message->out_offset[outvec_idx] +
                                      message->out_position[outvec_idx]],
                     buffer, num_bytes);
        message->out_position[outvec_idx] += num_bytes;
    }
    return WT_FFM_SUCCESS;
}

int wt_ffm_reply(wt_ffm_runtime_t* runtime, int32_t partition_id,
                 psa_handle_t msg_handle, psa_status_t status)
{
    wt_ffm_message_runtime_t* message;
    wt_ffm_connection_runtime_t* connection;
    uint16_t message_index;

    if (wt_ffm_message_from_handle(runtime, partition_id, msg_handle,
            &message_index) != WT_FFM_SUCCESS)
        return WT_FFM_ERROR_HANDLE;
    message = &runtime->messages[message_index];
    connection = &runtime->connections[message->connection_index];

    if (message->type == PSA_IPC_CONNECT) {
        connection->state = status == PSA_SUCCESS ?
            WT_IPC_CONNECTION_IDLE : WT_IPC_CONNECTION_ERROR;
    }
    else if (message->type == PSA_IPC_DISCONNECT) {
        connection->state = WT_IPC_CONNECTION_TERMINAL;
    }
    else if (message->type >= PSA_IPC_CALL) {
        connection->state = status == PSA_ERROR_PROGRAMMER_ERROR ?
            WT_IPC_CONNECTION_ERROR : WT_IPC_CONNECTION_IDLE;
    }
    else {
        return WT_FFM_ERROR_STATE;
    }

    message->reply_status = status;
    message->active = 0U;
    message->complete = 1U;
    return WT_FFM_SUCCESS;
}
