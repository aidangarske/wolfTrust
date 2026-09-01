/* wt_spm_fuzz.c
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

/* libFuzzer harness for the Non-secure to SPM boundary.
 *
 * Drives fuzzer bytes through the FF-M runtime the way a Non-secure client
 * reaches it: psa_connect / psa_call / psa_close with attacker-controlled
 * service ids, handles (including forged ones), call types, and input/output
 * vector lengths and contents. Exercises handle decode and ownership, vector
 * preparation and bounds, message routing, and the read/skip/write transfer
 * math under AddressSanitizer.
 *
 * Build: make -C tests/fuzz
 * Run:   ./tests/fuzz/wt_spm_fuzz corpus/ -max_len=4096 -timeout=30
 */

#include "wolftrust/ffm.h"
#include "wolftrust/manifest.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

int LLVMFuzzerInitialize(int* argc, char*** argv);
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

#define FUZZ_RESET_INTERVAL 512
#define FUZZ_MAX_OPS 64

/* Two Non-secure-reachable services: one connection-based, one stateless-shaped
 * (rejected on connect), so the harness exercises both accept and refuse. */
#define FUZZ_CONN_SID    0x2000U
#define FUZZ_STRICT_SID  0x2001U
#define FUZZ_PARTITION   1
#define FUZZ_NS_CLIENT   (-1)

static const wt_service_descriptor_t g_fuzz_services[] = {
    { "fuzz_conn", FUZZ_CONN_SID, 2U, WT_SERVICE_VERSION_RELAXED,
      0x10U, 0U, 1U, 1U },
    { "fuzz_strict", FUZZ_STRICT_SID, 2U, WT_SERVICE_VERSION_STRICT,
      0x20U, 0U, 1U, 1U }
};

static const wt_partition_manifest_t g_fuzz_partitions[] = {
    {
        "fuzz_partition", FUZZ_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_fuzz_services,
        sizeof(g_fuzz_services) / sizeof(g_fuzz_services[0]),
        NULL, 0U, NULL, 0U
    }
};

static const wt_system_manifest_t g_fuzz_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "fuzz",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_fuzz_partitions,
    .partition_count =
        sizeof(g_fuzz_partitions) / sizeof(g_fuzz_partitions[0])
};

static wt_ffm_runtime_t g_runtime;
static uint32_t g_iterations;
static int g_ready;

/* A cursor that never reads past the fuzz buffer; short reads return zero so
 * the harness stays deterministic on a truncated input. */
typedef struct fuzz_cursor {
    const uint8_t* data;
    size_t size;
    size_t pos;
} fuzz_cursor_t;

static uint8_t fuzz_u8(fuzz_cursor_t* cursor)
{
    uint8_t value = 0U;

    if (cursor->pos < cursor->size) {
        value = cursor->data[cursor->pos];
        cursor->pos++;
    }
    return value;
}

static uint32_t fuzz_u32(fuzz_cursor_t* cursor)
{
    uint32_t value;

    value = (uint32_t)fuzz_u8(cursor);
    value |= (uint32_t)fuzz_u8(cursor) << 8;
    value |= (uint32_t)fuzz_u8(cursor) << 16;
    value |= (uint32_t)fuzz_u8(cursor) << 24;
    return value;
}

/* The serving partition drains every input vector and writes a bounded reply
 * to every output vector, then completes the message. This is the transfer
 * surface a real service exposes to a Non-secure caller. */
static int fuzz_dispatch(void* context, wt_ffm_runtime_t* runtime,
                         int32_t partition_id)
{
    static const uint8_t reply[] = { 'w', 't', 'f', 'z' };
    psa_signal_t asserted = 0U;
    psa_msg_t message;
    uint8_t scratch[16];
    size_t i;

    (void)context;
    if (wt_ffm_wait(runtime, partition_id, PSA_WAIT_ANY, &asserted) !=
            WT_FFM_SUCCESS) {
        return -1;
    }
    if (wt_ffm_get(runtime, partition_id, asserted, &message) != PSA_SUCCESS) {
        return -1;
    }

    if (message.type >= 0) {
        for (i = 0U; i < PSA_MAX_IOVEC; i++) {
            while (wt_ffm_read(runtime, partition_id, message.handle,
                    (uint32_t)i, scratch, sizeof(scratch)) == sizeof(scratch)) {
                /* drain */
            }
            (void)wt_ffm_skip(runtime, partition_id, message.handle,
                              (uint32_t)i, 1U);
        }
        for (i = 0U; i < PSA_MAX_IOVEC; i++) {
            (void)wt_ffm_write(runtime, partition_id, message.handle,
                               (uint32_t)i, reply, sizeof(reply));
        }
    }

    (void)wt_ffm_reply(runtime, partition_id, message.handle, PSA_SUCCESS);
    return 0;
}

static int fuzz_check_mem(void* context, psa_client_id_t caller,
                          const void* address, size_t size)
{
    (void)context;
    (void)caller;
    return (size == 0U) || (address != NULL);
}

static int fuzz_check_read(void* context, psa_client_id_t caller,
                           const void* address, size_t size)
{
    return fuzz_check_mem(context, caller, address, size);
}

static int fuzz_check_write(void* context, psa_client_id_t caller,
                            void* address, size_t size)
{
    return fuzz_check_mem(context, caller, address, size);
}

static void fuzz_panic(void* context, int32_t partition_id)
{
    (void)context;
    (void)partition_id;
}

static const wt_ffm_port_ops_t g_fuzz_ops = {
    fuzz_check_read,
    fuzz_check_write,
    fuzz_dispatch,
    fuzz_panic
};

static void fuzz_reset(void)
{
    (void)memset(&g_runtime, 0, sizeof(g_runtime));
    if (wt_ffm_init(&g_runtime, &g_fuzz_manifest, &g_fuzz_ops, NULL) ==
            WT_FFM_SUCCESS) {
        (void)wt_ffm_register_partition(&g_runtime, FUZZ_PARTITION,
                                        fuzz_dispatch, NULL);
        g_ready = 1;
    }
    else {
        g_ready = 0;
    }
}

int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    (void)argc;
    (void)argv;
    fuzz_reset();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    fuzz_cursor_t cursor;
    psa_invec in_vec[PSA_MAX_IOVEC];
    psa_outvec out_vec[PSA_MAX_IOVEC];
    uint8_t out_buf[PSA_MAX_IOVEC][64];
    psa_handle_t handles[4];
    size_t handle_count = 0U;
    size_t op;
    size_t ops;
    size_t i;

    if (!g_ready) {
        fuzz_reset();
        if (!g_ready) {
            return 0;
        }
    }
    g_iterations++;
    if ((g_iterations % FUZZ_RESET_INTERVAL) == 0U) {
        fuzz_reset();
        if (!g_ready) {
            return 0;
        }
    }

    cursor.data = data;
    cursor.size = size;
    cursor.pos = 0U;
    (void)memset(handles, 0, sizeof(handles));

    ops = (size_t)(fuzz_u8(&cursor) % FUZZ_MAX_OPS) + 1U;
    for (op = 0U; op < ops; op++) {
        uint8_t selector = fuzz_u8(&cursor);
        uint32_t sid;
        int32_t type;
        psa_handle_t handle;
        size_t in_len;
        size_t out_len;

        switch (selector & 0x07U) {
        case 0: /* connect: real sid sometimes, fuzzed sid otherwise */
            sid = ((selector & 0x08U) != 0U) ? (uint32_t)fuzz_u32(&cursor) :
                  (((selector & 0x10U) != 0U) ? FUZZ_CONN_SID : FUZZ_STRICT_SID);
            handle = wt_ffm_connect(&g_runtime, FUZZ_NS_CLIENT, sid,
                                    fuzz_u32(&cursor));
            if (PSA_HANDLE_IS_VALID(handle) &&
                    handle_count < (sizeof(handles) / sizeof(handles[0]))) {
                handles[handle_count] = handle;
                handle_count++;
            }
            break;
        case 1: /* call on a held handle */
        case 2: /* call on a forged handle */
            if ((selector & 0x01U) != 0U && handle_count > 0U) {
                handle = handles[fuzz_u8(&cursor) % handle_count];
            }
            else {
                handle = (psa_handle_t)fuzz_u32(&cursor);
            }
            type = (int32_t)fuzz_u32(&cursor);
            in_len = (size_t)(fuzz_u8(&cursor) % (PSA_MAX_IOVEC + 2U));
            out_len = (size_t)(fuzz_u8(&cursor) % (PSA_MAX_IOVEC + 2U));
            for (i = 0U; i < PSA_MAX_IOVEC; i++) {
                uint8_t vlen = fuzz_u8(&cursor);
                size_t take = (size_t)(vlen % 33U);

                if (cursor.pos + take > cursor.size) {
                    take = cursor.size - cursor.pos;
                }
                in_vec[i].base = (take != 0U) ? &cursor.data[cursor.pos] : NULL;
                in_vec[i].len = take;
                cursor.pos += take;
                out_vec[i].base = out_buf[i];
                out_vec[i].len = (size_t)(fuzz_u8(&cursor) % 65U);
            }
            (void)wt_ffm_call(&g_runtime, FUZZ_NS_CLIENT, handle, type,
                              in_vec, in_len, out_vec, out_len);
            break;
        case 3: /* close a held handle */
            if (handle_count > 0U) {
                handle = handles[fuzz_u8(&cursor) % handle_count];
            }
            else {
                handle = (psa_handle_t)fuzz_u32(&cursor);
            }
            (void)wt_ffm_close(&g_runtime, FUZZ_NS_CLIENT, handle);
            break;
        case 4: /* version query with a fuzzed sid */
            (void)wt_ffm_service_version(&g_runtime, FUZZ_NS_CLIENT,
                                         fuzz_u32(&cursor));
            break;
        default: /* close a forged handle, then framework-version probe */
            (void)wt_ffm_close(&g_runtime, FUZZ_NS_CLIENT,
                               (psa_handle_t)fuzz_u32(&cursor));
            (void)wt_ffm_framework_version(&g_runtime);
            break;
        }
    }

    return 0;
}
