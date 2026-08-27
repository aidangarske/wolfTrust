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

/* Host proof for the OS-neutral PSA FF-M client core (P7-S1). The real
 * WolfTrust_FFM_* veneers are Armv8-M CMSE code, so this test supplies host
 * stubs that route them to the in-process FF-M runtime the port drives, then
 * exercises psa_connect/psa_call/psa_close from src/client/psa_ffm_client.c
 * against a registered SERVICE_CRYPTO. It proves the neutral client marshals
 * psa_invec/psa_outvec into the veneer iovec, writes the outvec lengths back,
 * and reaches the service without any operating-system dependency. */

#include "wolftrust/ffm_boot.h"
#include "wolftrust/spm_sched.h"
#include "wolftrust/services/crypto_service.h"
#include "wolftrust/services/vault_service.h"
#include "wolftrust/ffm_crypto_client.h"
#include "wolftrust/ffm_veneer.h"
#include "psa/client.h"
#include "psa_manifest/pid.h"

#include <stdio.h>
#include <string.h>

#include <wolfssl/wolfcrypt/random.h>

#define TEST_CRYPTO_SID 4097U
#define TEST_VAULT_SID  4098U
#define TEST_NS_CLIENT  (-1)

/* ---- platform + scheduler stubs the neutral boot core needs ---- */
void wt_platform_panic(void)
{
}

int wt_platform_run_crypto_sp_isolated(const uint8_t* input, size_t input_len,
                                       uint8_t* digest, size_t digest_len)
{
    return wt_crypto_sp_hash(input, input_len, digest, digest_len);
}

int wt_spm_sched_add(wt_ffm_runtime_t* runtime, int32_t partition_id,
                     wt_spm_sp_entry_fn entry, void* arg)
{
    (void)runtime; (void)partition_id; (void)entry; (void)arg;
    return WT_FFM_SUCCESS;
}

int wt_spm_sched_start(wt_ffm_runtime_t* runtime, int32_t partition_id)
{
    (void)runtime; (void)partition_id;
    return WT_FFM_SUCCESS;
}

int wt_spm_vault_start(wt_ffm_runtime_t* runtime, int32_t partition_id)
{
    (void)runtime; (void)partition_id;
    return WT_FFM_SUCCESS;
}

int wt_spm_its_start(wt_ffm_runtime_t* runtime, int32_t partition_id)
{
    (void)runtime; (void)partition_id;
    return WT_FFM_SUCCESS;
}

int wt_spm_ps_start(wt_ffm_runtime_t* runtime, int32_t partition_id)
{
    (void)runtime; (void)partition_id;
    return WT_FFM_SUCCESS;
}

/* ---- host memcheck standing in for the Armv8-M CMSE checker ---- */
static int test_ns_check_read(wt_guest_id_t guest_id, const void* address,
                              size_t size)
{
    (void)guest_id;
    return size == 0U || address != NULL;
}

static int test_ns_check_write(wt_guest_id_t guest_id, void* address,
                               size_t size)
{
    (void)guest_id;
    return size == 0U || address != NULL;
}

/* ---- host stubs for the Armv8-M NS->S veneers: route to the runtime ---- */
int32_t WolfTrust_FFM_Connect(uint32_t sid, uint32_t version)
{
    return (int32_t)wt_ffm_connect(wt_ffm_boot_runtime_mut(),
                                   TEST_NS_CLIENT, sid, version);
}

void WolfTrust_FFM_Close(int32_t handle)
{
    (void)wt_ffm_close(wt_ffm_boot_runtime_mut(), TEST_NS_CLIENT, handle);
}

static int g_random_calls;

int32_t WolfTrust_FFM_Call(int32_t handle, int32_t type,
                           wt_ffm_veneer_iovec_t* iv)
{
    psa_invec in[WT_FFM_VENEER_IOVEC_MAX];
    psa_outvec out[WT_FFM_VENEER_IOVEC_MAX];
    psa_status_t st;
    uint32_t i;

    if (type == WT_CRYPTO_OP_RANDOM) {
        g_random_calls++;
    }
    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));
    for (i = 0u; i < iv->in_count; i++) {
        in[i].base = iv->in[i].base;
        in[i].len = iv->in[i].len;
    }
    for (i = 0u; i < iv->out_count; i++) {
        out[i].base = iv->out[i].base;
        out[i].len = iv->out[i].len;
    }
    st = wt_ffm_call(wt_ffm_boot_runtime_mut(), TEST_NS_CLIENT, handle, type,
                     in, iv->in_count, out, iv->out_count);
    for (i = 0u; i < iv->out_count; i++) {
        iv->out[i].len = (uint32_t)out[i].len;
    }
    return (int32_t)st;
}

uint32_t WolfTrust_FFM_FrameworkVersion(void)
{
    return PSA_FRAMEWORK_VERSION;
}

uint32_t WolfTrust_FFM_ServiceVersion(uint32_t sid)
{
    (void)sid;
    return 1u;
}

/* ---- vault fixture for the RANDOM forward: a live host wc_RNG stands in
 * for the wolfHSM vault RNG the production keyvault backend uses ---- */
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

static wt_crypto_service_ctx_t g_crypto_ctx;

/* ---- manifest fixture: the crypto partition as production declares it ---- */
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
        "PARTITION_CRYPTO", PARTITION_CRYPTO_ID, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_services, sizeof(g_services) / sizeof(g_services[0]),
        g_vault_dep, 1U, NULL, 0U
    },
    {
        "PARTITION_VAULT", PARTITION_VAULT_ID, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_vault_services, sizeof(g_vault_services) / sizeof(g_vault_services[0]),
        NULL, 0U, NULL, 0U
    }
};

static const wt_system_manifest_t g_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "psa-ffm-client-test",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_partitions,
    .partition_count = sizeof(g_partitions) / sizeof(g_partitions[0])
};

static int g_failures;

static void check(int ok, const char* what)
{
    if (ok) {
        printf("PASS: %s\n", what);
    }
    else {
        printf("FAIL: %s\n", what);
        g_failures++;
    }
}

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
    uint8_t digest[32];
    uint8_t rng_buf[300];
    psa_handle_t handle;
    psa_invec in_vec;
    psa_outvec out_vec;
    psa_status_t status;
    size_t n;
    int zero;

    check(wt_ffm_boot_init(&g_manifest) == WT_FFM_SUCCESS,
          "P7-S1 boot core initializes from the manifest");
    wt_ffm_boot_set_memcheck(test_ns_check_read, test_ns_check_write);

    /* Give the crypto partition the vault route the production port supplies
     * (spm_svc.c), so the RANDOM forward runs the real SP-to-SP path. */
    g_crypto_ctx.transport = wt_spm_transport_direct;
    g_crypto_ctx.compute = wt_crypto_sp_hash;
    g_crypto_ctx.vault_sid = TEST_VAULT_SID;
    g_crypto_ctx.vault_handle = 0;
    check(wt_ffm_register_partition(wt_ffm_boot_runtime_mut(),
                                    PARTITION_CRYPTO_ID,
                                    wt_crypto_service_dispatch,
                                    &g_crypto_ctx) == WT_FFM_SUCCESS,
          "P7-S3 crypto partition re-registers with the vault route");
    wt_vault_service_set_key_backend(&g_test_key_backend);

    check(psa_framework_version() == PSA_FRAMEWORK_VERSION,
          "P7-S1 psa_framework_version reports 0x0100 through the neutral core");

    handle = psa_connect(TEST_CRYPTO_SID, 1U);
    check(PSA_HANDLE_IS_VALID(handle),
          "P7-S1 psa_connect(SERVICE_CRYPTO) returns a valid handle");

    in_vec.base = input;
    in_vec.len = sizeof(input) - 1U;
    out_vec.base = digest;
    out_vec.len = sizeof(digest);
    memset(digest, 0, sizeof(digest));
    status = psa_call(handle, PSA_IPC_CALL, &in_vec, 1U, &out_vec, 1U);
    check(status == PSA_SUCCESS,
          "P7-S1 psa_call round trip through the neutral client succeeds");
    check(memcmp(digest, expected, sizeof(expected)) == 0,
          "P7-S1 SERVICE_CRYPTO SHA-256 KAT digest matches");
    check(out_vec.len == sizeof(expected),
          "P7-S1 psa_call writes the produced outvec length back");
    psa_close(handle);

    /* An iovec count above PSA_MAX_IOVEC is a caller programmer error and must
     * be refused before any veneer call. */
    status = psa_call(handle, PSA_IPC_CALL, &in_vec, 5U, &out_vec, 1U);
    check(status == PSA_ERROR_PROGRAMMER_ERROR,
          "P7-S1 psa_call rejects an over-count invec (PROGRAMMER_ERROR)");

    /* P7-S3: the OS-neutral crypto-service RNG helper (WT-FFM-0054). A
     * 300-byte fill crosses the WT_CRYPTO_RANDOM_MAX bound, proving the
     * chunk loop issues one mediated psa_call per bounded chunk. */
    memset(rng_buf, 0, sizeof(rng_buf));
    g_random_calls = 0;
    check(wt_ffm_crypto_random(TEST_CRYPTO_SID, rng_buf,
                               sizeof(rng_buf)) == 0,
          "P7-S3 wt_ffm_crypto_random fills 300 bytes through the SPM");
    check(g_random_calls == 2,
          "P7-S3 300-byte fill chunked into two bounded psa_calls");
    zero = 1;
    for (n = 0U; n < sizeof(rng_buf); n++) {
        if (rng_buf[n] != 0U) {
            zero = 0;
        }
    }
    check(zero == 0, "P7-S3 vault-backed RNG output is not all zero");
    g_random_calls = 0;
    check(wt_ffm_crypto_random(TEST_CRYPTO_SID, NULL, 32U) == -1 &&
              wt_ffm_crypto_random(TEST_CRYPTO_SID, rng_buf, 0U) == -1 &&
              g_random_calls == 0,
          "P7-S3 NULL/zero-length RNG requests refused before any call");

    /* Clearing the port memcheck seam fails closed. */
    wt_ffm_boot_set_memcheck(NULL, NULL);
    handle = psa_connect(TEST_CRYPTO_SID, 1U);
    out_vec.base = digest;
    out_vec.len = sizeof(digest);
    status = psa_call(handle, PSA_IPC_CALL, &in_vec, 1U, &out_vec, 1U);
    check(status != PSA_SUCCESS,
          "P7-S1 clearing the port memcheck fails the NS call closed");

    if (g_failures == 0) {
        printf("PASS: psa_ffm_client (OS-neutral PSA FF-M client core)\n");
        return 0;
    }
    printf("FAIL: psa_ffm_client (%d failures)\n", g_failures);
    return 1;
}
