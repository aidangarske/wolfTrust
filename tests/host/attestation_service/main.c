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

/* Host proof that Initial Attestation is reachable through real FF-M IPC:
 * wt_attestation_service_dispatch carries a challenge in and a token out over
 * a genuine psa_connect/psa_call round trip. The token backend
 * (wt_initial_attest_get_token) is stubbed here -- its real HSM/COSE-backed
 * implementation is proven on M33MU -- so this suite isolates the IPC routing:
 * challenge delivery, caller-identity derivation, and token return. */

#include "wolftrust/ffm.h"
#include "wolftrust/services/attestation_service.h"
#include "wolftrust/services/initial_attestation.h"

#include <stdio.h>
#include <string.h>

#define TEST_ATTEST_PARTITION 3
#define TEST_ATTEST_SID       4096U
#define TEST_ATTEST_SIGNAL    0x10U
#define TEST_NS_CLIENT        (-1)

static const uint8_t g_expected_challenge[WT_ATTEST_CHALLENGE_SIZE_32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t g_stub_token[] = {
    'W', 'T', '-', 'A', 'T', 'T', 'E', 'S', 'T', '-', 'T', 'O', 'K', 'E', 'N'
};

/* Stub backend: asserts the dispatch delivered the exact challenge and the
 * NS caller's derived guest id, then returns a fixed token. */
int wt_initial_attest_get_token(wt_guest_id_t guestId,
    const uint8_t* challenge, size_t challengeSize, uint8_t* token,
    size_t tokenCapacity, size_t* tokenSize)
{
    if (guestId != 0 || challenge == NULL || token == NULL ||
            tokenSize == NULL) {
        return WT_ATTEST_ERROR_INVALID_ARGUMENT;
    }
    if (challengeSize != WT_ATTEST_CHALLENGE_SIZE_32 ||
            memcmp(challenge, g_expected_challenge,
                   WT_ATTEST_CHALLENGE_SIZE_32) != 0) {
        return WT_ATTEST_ERROR_INVALID_ARGUMENT;
    }
    if (tokenCapacity < sizeof(g_stub_token)) {
        return WT_ATTEST_ERROR_BUFFER_TOO_SMALL;
    }
    (void)memcpy(token, g_stub_token, sizeof(g_stub_token));
    *tokenSize = sizeof(g_stub_token);
    return WT_ATTEST_SUCCESS;
}

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
    wt_attestation_service_dispatch,
    test_panic
};

static const wt_service_descriptor_t g_services[] = {
    {
        "SERVICE_ATTEST", TEST_ATTEST_SID, 1U, WT_SERVICE_VERSION_RELAXED,
        TEST_ATTEST_SIGNAL, 0U, 1U, 1U
    }
};

static const wt_partition_manifest_t g_partitions[] = {
    {
        "PARTITION_ATTEST", TEST_ATTEST_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_services, sizeof(g_services) / sizeof(g_services[0]),
        NULL, 0U, NULL, 0U
    }
};

static const wt_system_manifest_t g_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "attestation-service-test",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_partitions,
    .partition_count = sizeof(g_partitions) / sizeof(g_partitions[0])
};

int main(void)
{
    wt_ffm_runtime_t runtime;
    psa_handle_t handle;
    uint8_t token[WT_ATTEST_MAX_TOKEN_SIZE];
    psa_invec in_vec = { g_expected_challenge, sizeof(g_expected_challenge) };
    psa_outvec out_vec = { token, sizeof(token) };
    psa_status_t status;

    if (wt_ffm_init(&runtime, &g_manifest, &g_port_ops, NULL) !=
            WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "wt_ffm_init failed\n");
        return 1;
    }

    handle = wt_ffm_connect(&runtime, TEST_NS_CLIENT, TEST_ATTEST_SID, 1U);
    if (!PSA_HANDLE_IS_VALID(handle)) {
        (void)fprintf(stderr, "psa_connect(SERVICE_ATTEST) failed\n");
        return 1;
    }

    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle, PSA_IPC_CALL,
                         &in_vec, 1U, &out_vec, 1U);
    if (status != PSA_SUCCESS) {
        (void)fprintf(stderr, "psa_call(SERVICE_ATTEST) failed st=%d\n",
                      (int)status);
        return 1;
    }
    if (out_vec.len != sizeof(g_stub_token) ||
            memcmp(token, g_stub_token, sizeof(g_stub_token)) != 0) {
        (void)fprintf(stderr, "attestation token mismatch through dispatch\n");
        return 1;
    }

    if (wt_ffm_close(&runtime, TEST_NS_CLIENT, handle) != WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "psa_close(SERVICE_ATTEST) failed\n");
        return 1;
    }

    (void)printf("PASS: SERVICE_ATTEST token through real FF-M dispatch\n");
    return 0;
}
