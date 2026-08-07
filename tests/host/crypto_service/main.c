/* main.c
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

/* Host proof that wt_crypto_service_dispatch (item 3c) produces a correct
 * SHA-256 digest through a real psa_connect/psa_call round trip, not just
 * a compiling-and-linking claim. wt_ffm_call() invokes the port's dispatch
 * callback synchronously, so wt_crypto_service_dispatch is registered as
 * that callback directly -- no scheduler or CMSE glue needed to prove the
 * service logic itself. */

#include "wolftrust/ffm.h"
#include "wolftrust/services/crypto_service.h"

#include <stdio.h>
#include <string.h>

#define TEST_CRYPTO_PARTITION 2
#define TEST_CRYPTO_SID       4097U
#define TEST_NS_CLIENT        (-1)

static int test_check_read(void* context, psa_client_id_t caller,
                           const void* address, size_t size)
{
    (void)context;
    (void)caller;
    return size == 0U || address != NULL;
}

static int test_check_write(void* context, psa_client_id_t caller,
                            void* address, size_t size)
{
    (void)context;
    (void)caller;
    return size == 0U || address != NULL;
}

static void test_panic(void* context, int32_t partition_id)
{
    (void)context;
    (void)partition_id;
}

static const wt_ffm_port_ops_t g_port_ops = {
    test_check_read,
    test_check_write,
    wt_crypto_service_dispatch,
    test_panic
};

static const wt_service_descriptor_t g_services[] = {
    {
        "SERVICE_CRYPTO", TEST_CRYPTO_SID, 1U, WT_SERVICE_VERSION_RELAXED,
        0x10U, 0U, 1U, 1U
    }
};

static const wt_partition_manifest_t g_partitions[] = {
    {
        "PARTITION_CRYPTO", TEST_CRYPTO_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_services, sizeof(g_services) / sizeof(g_services[0]),
        NULL, 0U, NULL, 0U
    }
};

static const wt_system_manifest_t g_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "crypto-service-test",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_partitions,
    .partition_count = sizeof(g_partitions) / sizeof(g_partitions[0])
};

int main(void)
{
    /* SHA-256("wolfTrust FF-M SERVICE_CRYPTO dispatch test") */
    static const uint8_t input[] =
        "wolfTrust FF-M SERVICE_CRYPTO dispatch test";
    static const uint8_t expected[32] = {
        0x20, 0x03, 0xdf, 0x15, 0x2a, 0x52, 0x8a, 0x06,
        0xc8, 0xd3, 0x48, 0xb8, 0xfa, 0x8b, 0x2f, 0x87,
        0xf7, 0x1f, 0xae, 0xc6, 0x24, 0x6c, 0x7e, 0x72,
        0x8e, 0x27, 0xa4, 0xb5, 0x0a, 0x49, 0x84, 0x66
    };
    wt_ffm_runtime_t runtime;
    psa_handle_t handle;
    uint8_t digest[32];
    psa_invec in_vec = { input, sizeof(input) - 1U };
    psa_outvec out_vec = { digest, sizeof(digest) };
    psa_status_t status;

    if (wt_ffm_init(&runtime, &g_manifest, &g_port_ops, NULL) !=
            WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "wt_ffm_init failed\n");
        return 1;
    }

    handle = wt_ffm_connect(&runtime, TEST_NS_CLIENT, TEST_CRYPTO_SID, 1U);
    if (!PSA_HANDLE_IS_VALID(handle)) {
        (void)fprintf(stderr, "psa_connect(SERVICE_CRYPTO) failed\n");
        return 1;
    }

    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle, PSA_IPC_CALL,
                         &in_vec, 1U, &out_vec, 1U);
    if (status != PSA_SUCCESS) {
        (void)fprintf(stderr, "psa_call(SERVICE_CRYPTO) failed st=%d\n",
                      (int)status);
        return 1;
    }
    if (out_vec.len != sizeof(digest) ||
            memcmp(digest, expected, sizeof(expected)) != 0) {
        (void)fprintf(stderr,
                      "SHA-256 digest mismatch through FF-M dispatch\n");
        return 1;
    }

    if (wt_ffm_close(&runtime, TEST_NS_CLIENT, handle) != WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "psa_close(SERVICE_CRYPTO) failed\n");
        return 1;
    }

    (void)printf("PASS: SERVICE_CRYPTO SHA-256 KAT through real FF-M "
                 "dispatch\n");
    return 0;
}
