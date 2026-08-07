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

#include "wolftrust/ffm_api.h"

#include "val_interfaces.h"

#include <stdio.h>
#include <string.h>

#define TEST_SERVER_PARTITION 1
#define TEST_CLIENT_PARTITION 2
#define TEST_NS_CLIENT        (-1)

typedef struct test_context {
    psa_client_id_t caller;
    int32_t partition;
    unsigned int failures;
} test_context_t;

static wt_ffm_runtime_t g_runtime;
static test_context_t g_context;

int32_t client_test_psa_framework_version(caller_security_t caller);
int32_t client_test_psa_version(caller_security_t caller);
int32_t client_test_sid_does_not_exists(caller_security_t caller);
int32_t client_test_strict_policy_higher_version(caller_security_t caller);
int32_t client_test_strict_policy_lower_version(caller_security_t caller);
int32_t client_test_relax_policy_higher_version(caller_security_t caller);
int32_t client_test_secure_access_only_connection(caller_security_t caller);
int32_t client_test_psa_close_with_invalid_handle(caller_security_t caller);
int32_t client_test_psa_call_with_invalid_handle(caller_security_t caller);
int32_t client_test_psa_call_with_null_handle(caller_security_t caller);
int32_t client_test_dynamic_mem_alloc_fn(caller_security_t caller);
int32_t client_test_mem_manipulation_fn(caller_security_t caller);
int32_t client_test_psa_rot_lifecycle_state(caller_security_t caller);

val_api_t* valtest_entry_i001;
psa_api_t* psatest_entry_i001;
val_api_t* valtest_entry_i004;
psa_api_t* psatest_entry_i004;
val_api_t* valtest_entry_i005;
psa_api_t* psatest_entry_i005;
val_api_t* valtest_entry_i006;
psa_api_t* psatest_entry_i006;
val_api_t* valtest_entry_i007;
psa_api_t* psatest_entry_i007;
val_api_t* valtest_entry_i008;
psa_api_t* psatest_entry_i008;
val_api_t* valtest_entry_i012;
psa_api_t* psatest_entry_i012;
val_api_t* valtest_entry_i024;
psa_api_t* psatest_entry_i024;
val_api_t* valtest_entry_i025;
psa_api_t* psatest_entry_i025;
val_api_t* valtest_entry_i067;
psa_api_t* psatest_entry_i067;
val_api_t* valtest_entry_i071;
psa_api_t* psatest_entry_i071;
val_api_t* valtest_entry_i088;
psa_api_t* psatest_entry_i088;

static const wt_service_descriptor_t g_services[] = {
    {
        "SERVER_TEST_DISPATCHER", SERVER_TEST_DISPATCHER_SID,
        SERVER_TEST_DISPATCHER_VERSION, WT_SERVICE_VERSION_RELAXED,
        0x10U, 0U, 1U, 1U
    },
    {
        "SERVER_SECURE_CONNECT_ONLY", SERVER_SECURE_CONNECT_ONLY_SID,
        SERVER_SECURE_CONNECT_ONLY_VERSION, WT_SERVICE_VERSION_RELAXED,
        0x20U, 0U, 0U, 1U
    },
    {
        "SERVER_STRICT_VERSION", SERVER_STRICT_VERSION_SID,
        SERVER_STRICT_VERSION_VERSION, WT_SERVICE_VERSION_STRICT,
        0x40U, 0U, 1U, 1U
    },
    {
        "SERVER_RELAX_VERSION", SERVER_RELAX_VERSION_SID,
        SERVER_RELAX_VERSION_VERSION, WT_SERVICE_VERSION_RELAXED,
        0x80U, 0U, 1U, 1U
    }
};

static const uint32_t g_dependencies[] = {
    SERVER_TEST_DISPATCHER_SID,
    SERVER_SECURE_CONNECT_ONLY_SID,
    SERVER_STRICT_VERSION_SID,
    SERVER_RELAX_VERSION_SID
};

static const wt_partition_manifest_t g_partitions[] = {
    {
        "SERVER_PARTITION", TEST_SERVER_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_services, sizeof(g_services) / sizeof(g_services[0]),
        NULL, 0U, NULL, 0U
    },
    {
        "CLIENT_PARTITION", TEST_CLIENT_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        NULL, 0U, g_dependencies,
        sizeof(g_dependencies) / sizeof(g_dependencies[0]), NULL, 0U
    }
};

static const wt_system_manifest_t g_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "psa-arch-tests",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_partitions,
    .partition_count = sizeof(g_partitions) / sizeof(g_partitions[0])
};

static int32_t report(const char* test_id, const char* name,
                      int32_t status)
{
    if (status == VAL_STATUS_SUCCESS) {
        (void)printf("PASS: Arm PSA FF %s %s\n", test_id, name);
        return VAL_STATUS_SUCCESS;
    }
    if (val_status_step((uint32_t)status) == VAL_STEP_SKIPPED) {
        (void)printf("SKIP: Arm PSA FF %s %s\n", test_id, name);
        return VAL_STATUS_SUCCESS;
    }
    (void)fprintf(stderr, "FAIL: Arm PSA FF %s %s\n", test_id, name);
    return status;
}

static val_status_t test_print(print_verbosity_t verbosity,
                               const char* string, int32_t data)
{
    if (verbosity == ERROR)
        (void)fprintf(stderr, string, data);
    return VAL_STATUS_SUCCESS;
}

static val_status_t test_err_check(uint32_t checkpoint, val_status_t status)
{
    (void)checkpoint;
    return status;
}

static val_status_t test_set_boot_flag(boot_state_t state)
{
    (void)state;
    return VAL_STATUS_SUCCESS;
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

static int test_dispatch(void* context, wt_ffm_runtime_t* runtime,
                         int32_t partition_id)
{
    test_context_t* test = (test_context_t*)context;
    psa_signal_t asserted;
    psa_msg_t message;
    int32_t saved_partition;

    saved_partition = test->partition;
    test->partition = partition_id;
    if (wt_ffm_wait(runtime, partition_id, PSA_WAIT_ANY, &asserted) !=
            WT_FFM_SUCCESS ||
            wt_ffm_get(runtime, partition_id, asserted, &message) !=
                PSA_SUCCESS ||
            wt_ffm_reply(runtime, partition_id, message.handle,
                         PSA_SUCCESS) != WT_FFM_SUCCESS) {
        test->partition = saved_partition;
        return WT_FFM_ERROR_STATE;
    }
    test->partition = saved_partition;
    return WT_FFM_SUCCESS;
}

static void test_panic(void* context, int32_t partition_id)
{
    test_context_t* test = (test_context_t*)context;

    (void)partition_id;
    test->failures++;
}

static psa_client_id_t test_current_client(void* context)
{
    return ((test_context_t*)context)->caller;
}

static int32_t test_current_partition(void* context)
{
    return ((test_context_t*)context)->partition;
}

static const wt_ffm_port_ops_t g_port_ops = {
    test_check_read,
    test_check_write,
    test_dispatch,
    test_panic
};

static const wt_ffm_identity_ops_t g_identity_ops = {
    test_current_client,
    test_current_partition
};

static val_api_t g_val_api = {
    .print = test_print,
    .err_check_set = test_err_check,
    .set_boot_flag = test_set_boot_flag
};

static psa_api_t g_psa_api = {
    .framework_version = psa_framework_version,
    .version = psa_version,
    .connect = psa_connect,
    .call = psa_call,
    .close = psa_close
};

int main(void)
{
    int32_t status;

    (void)memset(&g_context, 0, sizeof(g_context));
    if (wt_ffm_init(&g_runtime, &g_manifest, &g_port_ops, &g_context) !=
            WT_FFM_SUCCESS ||
            wt_ffm_api_bind(&g_runtime, &g_identity_ops, &g_context) !=
                WT_FFM_SUCCESS) {
        return 1;
    }
    valtest_entry_i001 = &g_val_api;
    psatest_entry_i001 = &g_psa_api;
    valtest_entry_i004 = &g_val_api;
    psatest_entry_i004 = &g_psa_api;
    valtest_entry_i005 = &g_val_api;
    psatest_entry_i005 = &g_psa_api;
    valtest_entry_i006 = &g_val_api;
    psatest_entry_i006 = &g_psa_api;
    valtest_entry_i007 = &g_val_api;
    psatest_entry_i007 = &g_psa_api;
    valtest_entry_i008 = &g_val_api;
    psatest_entry_i008 = &g_psa_api;
    valtest_entry_i012 = &g_val_api;
    psatest_entry_i012 = &g_psa_api;
    valtest_entry_i024 = &g_val_api;
    psatest_entry_i024 = &g_psa_api;
    valtest_entry_i025 = &g_val_api;
    psatest_entry_i025 = &g_psa_api;
    valtest_entry_i067 = &g_val_api;
    psatest_entry_i067 = &g_psa_api;
    valtest_entry_i071 = &g_val_api;
    psatest_entry_i071 = &g_psa_api;
    valtest_entry_i088 = &g_val_api;
    psatest_entry_i088 = &g_psa_api;

    g_context.caller = TEST_NS_CLIENT;
    status = client_test_psa_framework_version(CALLER_NONSECURE);
    status = report("i001", "psa_framework_version (NS)", status);
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_psa_version(CALLER_NONSECURE);
        status = report("i001", "psa_version (NS)", status);
    }
    if (status != VAL_STATUS_SUCCESS)
        return 1;
    status = client_test_sid_does_not_exists(CALLER_NONSECURE);
    status = report("i004", "sid_does_not_exists", status);
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_strict_policy_higher_version(CALLER_NONSECURE);
        status = report("i005", "strict_policy_higher_version", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_strict_policy_lower_version(CALLER_NONSECURE);
        status = report("i006", "strict_policy_lower_version", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_relax_policy_higher_version(CALLER_NONSECURE);
        status = report("i007", "relax_policy_higher_version", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_secure_access_only_connection(CALLER_NONSECURE);
        status = report("i008", "secure_access_only_connection (NS)",
                        status);
    }
    if (status != VAL_STATUS_SUCCESS)
        return 1;
    status = client_test_psa_close_with_invalid_handle(CALLER_NONSECURE);
    status = report("i012", "psa_close_with_invalid_handle", status);
    /* NS psa_close(invalid) is spec-permitted to panic; that expected
     * panic must not fail the aggregate g_context.failures check below. */
    g_context.failures = 0U;
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_psa_call_with_invalid_handle(CALLER_NONSECURE);
        status = report("i024", "psa_call_with_invalid_handle", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_psa_call_with_null_handle(CALLER_NONSECURE);
        status = report("i025", "psa_call_with_null_handle", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_dynamic_mem_alloc_fn(CALLER_NONSECURE);
        status = report("i067", "dynamic_mem_alloc_fn", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_mem_manipulation_fn(CALLER_NONSECURE);
        status = report("i071", "mem_manipulation_fn", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_psa_rot_lifecycle_state(CALLER_NONSECURE);
        status = report("i088", "psa_rot_lifecycle_state", status);
    }
    if (status != VAL_STATUS_SUCCESS)
        return 1;

    g_context.caller = TEST_CLIENT_PARTITION;
    g_context.partition = TEST_CLIENT_PARTITION;
    status = client_test_psa_framework_version(CALLER_SECURE);
    status = report("i001", "psa_framework_version (secure)", status);
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_psa_version(CALLER_SECURE);
        status = report("i001", "psa_version (secure)", status);
    }
    if (status == VAL_STATUS_SUCCESS) {
        status = client_test_secure_access_only_connection(CALLER_SECURE);
        status = report("i008", "secure_access_only_connection (secure)",
                        status);
    }
    wt_ffm_api_unbind();
    if (status != VAL_STATUS_SUCCESS || g_context.failures != 0U)
        return 1;

    (void)printf("PASS: Arm PSA FF i001, i004-i008, i012, i024, i025, "
                "i067, i071, i088 on wolfTrust\n");
    return 0;
}
