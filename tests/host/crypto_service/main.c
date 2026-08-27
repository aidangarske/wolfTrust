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
 * service logic itself. P7-S3 adds the vault-backed WT_CRYPTO_OP_RANDOM
 * round trip (WT-FFM-0054): crypto forwards to a real SERVICE_VAULT
 * partition whose key backend draws from a live wc_RNG. */

#include "wolftrust/ffm.h"
#include "wolftrust/services/crypto_service.h"
#include "wolftrust/services/vault_service.h"

#include <stdio.h>
#include <string.h>

#include <wolfssl/wolfcrypt/random.h>

#define TEST_CRYPTO_PARTITION 2
#define TEST_CRYPTO_SID       4097U
#define TEST_VAULT_PARTITION  3
#define TEST_VAULT_SID        4098U
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

/* The crypto SP's service loop must issue a *blocking* psa_wait so the
 * coroutine scheduler suspends it until a message arrives; a POLL wait returns
 * NOT_READY at boot and kills the partition. This shim records the timeout the
 * loop actually asked for. */
static int g_wait_block_seen;

static int guard_transport(wt_ffm_runtime_t* runtime, wt_spm_call_t* call)
{
    if (call->op == WT_SPM_OP_WAIT && (call->timeout & PSA_BLOCK) != 0U)
        g_wait_block_seen = 1;
    return wt_spm_transport_direct(runtime, call);
}

/* Real vault-domain RNG for the RANDOM forward: a live wc_RNG on the host
 * stands in for the wolfHSM vault RNG the production keyvault backend uses. */
static WC_RNG g_test_rng;
static int g_test_rng_ready;

static psa_status_t test_vault_random(uint8_t* out, size_t len)
{
    if (g_test_rng_ready == 0) {
        if (wc_InitRng(&g_test_rng) != 0) {
            return PSA_ERROR_GENERIC_ERROR;
        }
        g_test_rng_ready = 1;
    }
    if (wc_RNG_GenerateBlock(&g_test_rng, out, (word32)len) != 0) {
        return PSA_ERROR_GENERIC_ERROR;
    }
    return PSA_SUCCESS;
}

static const wt_vault_key_backend_t g_test_key_backend = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    test_vault_random
};

/* One dispatch router serves both partitions; the crypto SP gets a context
 * carrying the vault route, exactly as the production port supplies it. */
static wt_crypto_service_ctx_t g_crypto_ctx;

static int test_dispatch(void* context, wt_ffm_runtime_t* runtime,
                         int32_t partition_id)
{
    (void)context;
    if (partition_id == TEST_VAULT_PARTITION) {
        return wt_vault_service_dispatch(NULL, runtime, partition_id);
    }
    return wt_crypto_service_dispatch(&g_crypto_ctx, runtime, partition_id);
}

static const wt_ffm_port_ops_t g_port_ops = {
    test_check_read,
    test_check_write,
    test_dispatch,
    test_panic
};

static const wt_service_descriptor_t g_services[] = {
    {
        "SERVICE_CRYPTO", TEST_CRYPTO_SID, 1U, WT_SERVICE_VERSION_RELAXED,
        0x10U, 0U, 1U, 1U
    }
};

static const wt_service_descriptor_t g_vault_services[] = {
    {
        "SERVICE_VAULT", TEST_VAULT_SID, 1U, WT_SERVICE_VERSION_RELAXED,
        0x10U, 0U, 0U, 1U
    }
};

static const uint32_t g_vault_dep[] = { TEST_VAULT_SID };

static const wt_partition_manifest_t g_partitions[] = {
    {
        "PARTITION_CRYPTO", TEST_CRYPTO_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_services, sizeof(g_services) / sizeof(g_services[0]),
        g_vault_dep, 1U, NULL, 0U
    },
    {
        "PARTITION_VAULT", TEST_VAULT_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_vault_services, sizeof(g_vault_services) / sizeof(g_vault_services[0]),
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
    static uint8_t over_cap[WT_CRYPTO_SP_INPUT_MAX + 1U];
    static uint8_t over_rng[WT_CRYPTO_RANDOM_MAX + 1U];
    wt_ffm_runtime_t runtime;
    psa_handle_t handle;
    uint8_t digest[32];
    uint8_t direct_digest[32];
    uint8_t rng_a[32];
    uint8_t rng_b[32];
    psa_invec in_vec = { input, sizeof(input) - 1U };
    psa_outvec out_vec = { digest, sizeof(digest) };
    psa_invec over_vec = { over_cap, sizeof(over_cap) };
    psa_outvec over_out = { digest, sizeof(digest) };
    psa_outvec rng_vec = { NULL, 0U };
    psa_status_t status;
    size_t i;
    int zero;

    if (wt_ffm_init(&runtime, &g_manifest, &g_port_ops, NULL) !=
            WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "wt_ffm_init failed\n");
        return 1;
    }

    g_crypto_ctx.transport = guard_transport;
    g_crypto_ctx.compute = wt_crypto_sp_hash;
    g_crypto_ctx.vault_sid = TEST_VAULT_SID;
    g_crypto_ctx.vault_handle = 0;
    wt_vault_service_set_key_backend(&g_test_key_backend);

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
    if (g_wait_block_seen == 0) {
        (void)fprintf(stderr,
                      "crypto SP issued a non-blocking psa_wait\n");
        return 1;
    }

    /* Isolated compute in isolation: the pure SP function the narrowed-MPU
     * context will run must produce the same digest with no psa_* calls. */
    if (wt_crypto_sp_hash(input, sizeof(input) - 1U, direct_digest,
                          sizeof(direct_digest)) != WT_FFM_SUCCESS ||
            memcmp(direct_digest, expected, sizeof(expected)) != 0) {
        (void)fprintf(stderr, "wt_crypto_sp_hash isolated compute mismatch\n");
        return 1;
    }

    /* Copied-IOVEC bound: a request past the SP input cap is refused, not
     * truncated -- the isolated compute never overruns its private buffer. */
    memset(over_cap, 'A', sizeof(over_cap));
    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle, PSA_IPC_CALL,
                         &over_vec, 1U, &over_out, 1U);
    if (status == PSA_SUCCESS) {
        (void)fprintf(stderr, "over-cap request was not refused\n");
        return 1;
    }

    /* Vault-backed randomness (WT-FFM-0054): the crypto SP forwards RANDOM
     * over a real SP-to-SP connection to SERVICE_VAULT, whose backend draws
     * from a live wc_RNG — proven by two draws that fill and differ. */
    memset(rng_a, 0, sizeof(rng_a));
    memset(rng_b, 0, sizeof(rng_b));
    rng_vec.base = rng_a;
    rng_vec.len = sizeof(rng_a);
    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle,
                         WT_CRYPTO_OP_RANDOM, NULL, 0U, &rng_vec, 1U);
    if (status != PSA_SUCCESS || rng_vec.len != sizeof(rng_a)) {
        (void)fprintf(stderr, "RANDOM op failed st=%d len=%u\n", (int)status,
                      (unsigned)rng_vec.len);
        return 1;
    }
    rng_vec.base = rng_b;
    rng_vec.len = sizeof(rng_b);
    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle,
                         WT_CRYPTO_OP_RANDOM, NULL, 0U, &rng_vec, 1U);
    if (status != PSA_SUCCESS ||
            memcmp(rng_a, rng_b, sizeof(rng_a)) == 0) {
        (void)fprintf(stderr, "RANDOM draws identical or second draw "
                      "failed\n");
        return 1;
    }
    zero = 1;
    for (i = 0U; i < sizeof(rng_a); i++) {
        if (rng_a[i] != 0U) {
            zero = 0;
        }
    }
    if (zero != 0) {
        (void)fprintf(stderr, "RANDOM draw is all zero\n");
        return 1;
    }

    /* Bound: a request past WT_CRYPTO_RANDOM_MAX is refused, and a zero-length
     * request is refused, never served partially. */
    rng_vec.base = over_rng;
    rng_vec.len = sizeof(over_rng);
    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle,
                         WT_CRYPTO_OP_RANDOM, NULL, 0U, &rng_vec, 1U);
    if (status == PSA_SUCCESS) {
        (void)fprintf(stderr, "over-max RANDOM request was not refused\n");
        return 1;
    }
    rng_vec.base = rng_a;
    rng_vec.len = 0U;
    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle,
                         WT_CRYPTO_OP_RANDOM, NULL, 0U, &rng_vec, 1U);
    if (status == PSA_SUCCESS) {
        (void)fprintf(stderr, "zero-length RANDOM request was not refused\n");
        return 1;
    }

    /* Fail closed: without a vault route the partition refuses RANDOM — it
     * holds no entropy source of its own. */
    g_crypto_ctx.vault_sid = 0U;
    rng_vec.base = rng_a;
    rng_vec.len = sizeof(rng_a);
    status = wt_ffm_call(&runtime, TEST_NS_CLIENT, handle,
                         WT_CRYPTO_OP_RANDOM, NULL, 0U, &rng_vec, 1U);
    g_crypto_ctx.vault_sid = TEST_VAULT_SID;
    if (status != PSA_ERROR_NOT_SUPPORTED) {
        (void)fprintf(stderr, "vault-less RANDOM did not fail closed "
                      "st=%d\n", (int)status);
        return 1;
    }

    if (wt_ffm_close(&runtime, TEST_NS_CLIENT, handle) != WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "psa_close(SERVICE_CRYPTO) failed\n");
        return 1;
    }

    (void)printf("PASS: SERVICE_CRYPTO SHA-256 KAT through real FF-M "
                 "dispatch\n");
    (void)printf("PASS: SERVICE_CRYPTO vault-backed RANDOM (WT-FFM-0054)\n");
    return 0;
}
