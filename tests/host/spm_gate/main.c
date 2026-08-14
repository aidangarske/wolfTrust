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

/* WT-FFM-0014: every SP-side psa_* call funnels through the one privileged
 * gate wt_spm_gate. These tests prove the gate is a faithful drop-in for the
 * inline wt_ffm_* calls on the real dispatch path, that it classifies an empty
 * psa_wait as "suspend this partition", and that it rejects an SP-supplied
 * pointer outside the partition's protection domain before the SPM touches it.
 * The physical suspend/resume, SVC trap, and unprivileged execution the gate is
 * built for live on Armv8-M and are proven on M33MU (P1t-2). */

#include "wolftrust/spm_gate.h"

#include <stdio.h>
#include <string.h>

#define TEST_SERVICE_SID    0x1000U
#define TEST_SERVICE_SIGNAL 0x10U
#define TEST_PARTITION_ID   1
#define TEST_NS_CLIENT      (-1)
#define TEST_RHANDLE        ((void*)(uintptr_t)0xA5A5U)

static unsigned int g_checks;
static unsigned int g_failures;

#define EXPECT_INT(actual, expected) \
    do { \
        int actual_value = (int)(actual); \
        int expected_value = (int)(expected); \
        g_checks++; \
        if (actual_value != expected_value) { \
            (void)fprintf(stderr, \
                "line %d: expected %d, received %d\n", __LINE__, \
                expected_value, actual_value); \
            g_failures++; \
        } \
    } while (0)

#define EXPECT_SIZE(actual, expected) \
    do { \
        size_t actual_value = (actual); \
        size_t expected_value = (expected); \
        g_checks++; \
        if (actual_value != expected_value) { \
            (void)fprintf(stderr, \
                "line %d: expected %zu, received %zu\n", __LINE__, \
                expected_value, actual_value); \
            g_failures++; \
        } \
    } while (0)

#define EXPECT_TRUE(condition) \
    do { \
        g_checks++; \
        if (!(condition)) { \
            (void)fprintf(stderr, "line %d: condition failed\n", __LINE__); \
            g_failures++; \
        } \
    } while (0)

static const wt_service_descriptor_t g_services[] = {
    {
        "test_service", TEST_SERVICE_SID, 1U,
        WT_SERVICE_VERSION_RELAXED, TEST_SERVICE_SIGNAL, 0U, 1U, 1U
    }
};

static const wt_partition_manifest_t g_partitions[] = {
    {
        "test_partition", TEST_PARTITION_ID, WT_FFM_VERSION_1_1,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_services, sizeof(g_services) / sizeof(g_services[0]),
        NULL, 0U, NULL, 0U
    }
};

static const wt_system_manifest_t g_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "host-test",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_partitions,
    .partition_count = sizeof(g_partitions) / sizeof(g_partitions[0])
};

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

/* A Secure Partition server body, but every psa_* primitive routed through the
 * gate instead of calling wt_ffm_* directly. A green connect/call round trip
 * proves the gate is behaviorally identical to the inline path. */
static int gate_dispatch(void* context, wt_ffm_runtime_t* runtime,
                         int32_t partition_id)
{
    static const uint8_t response[] = { 'O', 'K' };
    psa_signal_t asserted = 0U;
    psa_msg_t message;
    wt_spm_call_t call;
    uint8_t input[3];

    (void)context;

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = partition_id;
    call.signal_mask = PSA_WAIT_ANY;
    call.asserted = &asserted;
    EXPECT_INT(wt_spm_gate(runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    EXPECT_INT(wt_spm_call_would_block(&call), 0);
    EXPECT_INT(asserted, TEST_SERVICE_SIGNAL);

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_GET;
    call.partition_id = partition_id;
    call.signal = asserted;
    call.msg = &message;
    EXPECT_INT(wt_spm_gate(runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_status, PSA_SUCCESS);

    if (message.type == PSA_IPC_CONNECT) {
        (void)memset(&call, 0, sizeof(call));
        call.op = WT_SPM_OP_SET_RHANDLE;
        call.partition_id = partition_id;
        call.msg_handle = message.handle;
        call.rhandle = TEST_RHANDLE;
        EXPECT_INT(wt_spm_gate(runtime, NULL, &call), WT_FFM_SUCCESS);
        EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    }
    else if (message.type != PSA_IPC_DISCONNECT) {
        EXPECT_TRUE(message.rhandle == TEST_RHANDLE);
        (void)memset(&call, 0, sizeof(call));
        call.op = WT_SPM_OP_READ;
        call.partition_id = partition_id;
        call.msg_handle = message.handle;
        call.vec_idx = 0U;
        call.buffer = input;
        call.num_bytes = sizeof(input);
        EXPECT_INT(wt_spm_gate(runtime, NULL, &call), WT_FFM_SUCCESS);
        EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
        EXPECT_SIZE(call.ret_size, 3U);
        EXPECT_INT(input[0], 'a');

        (void)memset(&call, 0, sizeof(call));
        call.op = WT_SPM_OP_WRITE;
        call.partition_id = partition_id;
        call.msg_handle = message.handle;
        call.vec_idx = 0U;
        call.buffer = (void*)(uintptr_t)response;
        call.num_bytes = sizeof(response);
        EXPECT_INT(wt_spm_gate(runtime, NULL, &call), WT_FFM_SUCCESS);
        EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    }

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_REPLY;
    call.partition_id = partition_id;
    call.msg_handle = message.handle;
    call.status = PSA_SUCCESS;
    EXPECT_INT(wt_spm_gate(runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);

    return WT_FFM_SUCCESS;
}

static void test_panic(void* context, int32_t partition_id)
{
    (void)context;
    (void)partition_id;
}

static const wt_ffm_port_ops_t g_port_ops = {
    test_check_read,
    test_check_write,
    gate_dispatch,
    test_panic
};

static void test_gate_equivalence(void)
{
    static const uint8_t request[] = { 'a', 'b', 'c' };
    wt_ffm_runtime_t runtime;
    psa_invec in_vec[1];
    psa_outvec out_vec[1];
    uint8_t output[2];
    psa_handle_t handle;

    EXPECT_INT(wt_ffm_init(&runtime, &g_manifest, &g_port_ops, NULL),
               WT_FFM_SUCCESS);
    EXPECT_INT(wt_ffm_register_partition(&runtime, TEST_PARTITION_ID,
                                         gate_dispatch, NULL), WT_FFM_SUCCESS);

    handle = wt_ffm_connect(&runtime, TEST_NS_CLIENT, TEST_SERVICE_SID, 1U);
    EXPECT_TRUE(PSA_HANDLE_IS_VALID(handle));

    in_vec[0].base = request;
    in_vec[0].len = sizeof(request);
    out_vec[0].base = output;
    out_vec[0].len = sizeof(output);
    (void)memset(output, 0, sizeof(output));
    EXPECT_INT(wt_ffm_call(&runtime, TEST_NS_CLIENT, handle, PSA_IPC_CALL,
                           in_vec, 1U, out_vec, 1U), PSA_SUCCESS);
    EXPECT_INT(output[0], 'O');
    EXPECT_INT(output[1], 'K');

    EXPECT_INT(wt_ffm_close(&runtime, TEST_NS_CLIENT, handle), WT_FFM_SUCCESS);
    (void)printf("PASS: WT-FFM-0014 gate routes the SP dispatch path\n");
}

static void test_gate_would_block(void)
{
    wt_ffm_runtime_t runtime;
    psa_signal_t asserted = 0U;
    wt_spm_call_t call;

    EXPECT_INT(wt_ffm_init(&runtime, &g_manifest, &g_port_ops, NULL),
               WT_FFM_SUCCESS);

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = TEST_PARTITION_ID;
    call.signal_mask = PSA_DOORBELL;
    call.timeout = PSA_BLOCK;
    call.asserted = &asserted;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_NOT_READY);
    EXPECT_INT(wt_spm_call_would_block(&call), 1);

    /* PSA_POLL on the same unasserted signal must return, never block. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = TEST_PARTITION_ID;
    call.signal_mask = PSA_DOORBELL;
    call.timeout = PSA_POLL;
    call.asserted = &asserted;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_NOT_READY);
    EXPECT_INT(wt_spm_call_would_block(&call), 0);

    EXPECT_INT(wt_ffm_notify(&runtime, TEST_PARTITION_ID), WT_FFM_SUCCESS);

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = TEST_PARTITION_ID;
    call.signal_mask = PSA_DOORBELL;
    call.timeout = PSA_BLOCK;
    call.asserted = &asserted;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    EXPECT_INT(wt_spm_call_would_block(&call), 0);
    EXPECT_INT(asserted, PSA_DOORBELL);

    /* Doorbell asserted but masked out: a blocking wait must stay blocking,
     * not spuriously wake the partition on an unrequested signal. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = TEST_PARTITION_ID;
    call.signal_mask = TEST_SERVICE_SIGNAL;
    call.timeout = PSA_BLOCK;
    call.asserted = &asserted;
    asserted = 0xFFFFFFFFU;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_NOT_READY);
    EXPECT_INT(wt_spm_call_would_block(&call), 1);
    EXPECT_INT(asserted, 0);
    (void)printf("PASS: WT-FFM-0014 gate blocks PSA_BLOCK waits, polls return\n");
}

/* i058 Check 1 through the gate: a notified doorbell stays asserted across a
 * second psa_wait and only psa_clear drops it (WT-FFM-0027). */
static void test_gate_doorbell_state_machine(void)
{
    wt_ffm_runtime_t runtime;
    psa_signal_t asserted = 0U;
    wt_spm_call_t call;

    EXPECT_INT(wt_ffm_init(&runtime, &g_manifest, &g_port_ops, NULL),
               WT_FFM_SUCCESS);

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_NOTIFY;
    call.partition_id = TEST_PARTITION_ID;
    call.notify_partition = TEST_PARTITION_ID;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = TEST_PARTITION_ID;
    call.signal_mask = PSA_DOORBELL;
    call.timeout = PSA_BLOCK;
    call.asserted = &asserted;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    EXPECT_INT(wt_spm_call_would_block(&call), 0);
    EXPECT_INT(asserted, PSA_DOORBELL);

    /* Second wait: doorbell must remain asserted (psa_wait does not clear). */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = TEST_PARTITION_ID;
    call.signal_mask = PSA_DOORBELL;
    call.timeout = PSA_BLOCK;
    call.asserted = &asserted;
    asserted = 0U;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    EXPECT_INT(asserted, PSA_DOORBELL);

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_CLEAR;
    call.partition_id = TEST_PARTITION_ID;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);

    /* Poll after clear: no signal, and it must not block. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = TEST_PARTITION_ID;
    call.signal_mask = PSA_DOORBELL;
    call.timeout = PSA_POLL;
    call.asserted = &asserted;
    asserted = 0xFFFFFFFFU;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_NOT_READY);
    EXPECT_INT(wt_spm_call_would_block(&call), 0);
    EXPECT_INT(asserted, 0);
    (void)printf("PASS: WT-FFM-0027 gate doorbell asserts until psa_clear\n");
}

/* i063 topology: a server partition exporting three connection-based services
 * (two the server consumes, one starved irritator) and a client partition that
 * may connect to the irritator. Mirrors SERVER_UNSPECIFIED/RELAX plus the
 * SECURE_CONNECT_ONLY irritator, and CLIENT_PARTITION. */
#define I063_SERVER_ID    10
#define I063_CLIENT_ID    11
#define I063_SVC_A_SID    0x2000U
#define I063_SVC_B_SID    0x2001U
#define I063_SVC_IRR_SID  0x2002U
#define I063_SIG_A        0x10U
#define I063_SIG_B        0x20U
#define I063_SIG_IRR      0x40U
#define I063_NS_CLIENT    (-1)

static const wt_service_descriptor_t g_i063_services[] = {
    { "svc_a",   I063_SVC_A_SID,   1U, WT_SERVICE_VERSION_RELAXED, I063_SIG_A,
      0U, 1U, 1U },
    { "svc_b",   I063_SVC_B_SID,   1U, WT_SERVICE_VERSION_RELAXED, I063_SIG_B,
      0U, 1U, 1U },
    { "svc_irr", I063_SVC_IRR_SID, 1U, WT_SERVICE_VERSION_RELAXED, I063_SIG_IRR,
      0U, 1U, 1U }
};

static const uint32_t g_i063_client_deps[] = { I063_SVC_IRR_SID };

static const wt_partition_manifest_t g_i063_partitions[] = {
    { "i063_server", I063_SERVER_ID, WT_FFM_VERSION_1_1,
      WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
      g_i063_services, sizeof(g_i063_services) / sizeof(g_i063_services[0]),
      NULL, 0U, NULL, 0U },
    { "i063_client", I063_CLIENT_ID, WT_FFM_VERSION_1_0,
      WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
      NULL, 0U, g_i063_client_deps,
      sizeof(g_i063_client_deps) / sizeof(g_i063_client_deps[0]), NULL, 0U }
};

static const wt_system_manifest_t g_i063_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "host-test",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_i063_partitions,
    .partition_count = sizeof(g_i063_partitions) /
                       sizeof(g_i063_partitions[0])
};

static void i063_server_serve(wt_ffm_runtime_t* runtime, psa_signal_t signal,
                              psa_status_t reply)
{
    psa_msg_t msg;

    EXPECT_INT(wt_ffm_get(runtime, I063_SERVER_ID, signal, &msg), PSA_SUCCESS);
    EXPECT_INT(wt_ffm_reply(runtime, I063_SERVER_ID, msg.handle, reply),
               WT_FFM_SUCCESS);
}

/* Phase C (WT-FFM-0014/0027): doorbell-driven origination with masked
 * starvation, the portable core of Arm test i063. A doorbell-woken client
 * partition originates a fresh outbound connect through the SP-as-client gate;
 * that connect asserts a server signal the server masks out, so it stays
 * starved across the server's masked waits and is only delivered once the
 * server waits on it explicitly. The coroutine choreography this drives lives
 * on Armv8-M (wt_spm_sched_dispatch) and is proven on M33MU; here the gate and
 * runtime state machine it relies on are proven directly. */
static void test_doorbell_origination(void)
{
    wt_ffm_runtime_t runtime;
    wt_spm_call_t client;
    psa_signal_t asserted;
    uint16_t ns_a_msg;
    uint16_t ns_b_msg;
    psa_handle_t handle;

    EXPECT_INT(wt_ffm_init(&runtime, &g_i063_manifest, &g_port_ops, NULL),
               WT_FFM_SUCCESS);

    /* Server doorbells the client (server_test's psa->notify(CLIENT)). */
    EXPECT_INT(wt_ffm_notify(&runtime, I063_CLIENT_ID), WT_FFM_SUCCESS);
    asserted = 0U;
    EXPECT_INT(wt_ffm_wait(&runtime, I063_CLIENT_ID, PSA_WAIT_ANY, &asserted),
               WT_FFM_SUCCESS);
    EXPECT_INT(asserted, PSA_DOORBELL);

    /* Doorbell-woken client originates the irritator connect: gate CONNECT
     * pass 1 enqueues on the server and blocks the client. */
    (void)memset(&client, 0, sizeof(client));
    client.op = WT_SPM_OP_CONNECT;
    client.partition_id = I063_CLIENT_ID;
    client.sid = I063_SVC_IRR_SID;
    client.version = 1U;
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &client), WT_FFM_SUCCESS);
    EXPECT_INT(client.ret_int, WT_FFM_ERROR_NOT_READY);
    EXPECT_INT(client.pending_valid, 1U);
    EXPECT_INT(wt_spm_call_would_block(&client), 1);

    /* Starvation: the irritator asserted the server's IRR signal, but a wait
     * masking it out must not observe it. */
    asserted = 0xFFFFFFFFU;
    EXPECT_INT(wt_ffm_wait(&runtime, I063_SERVER_ID, I063_SIG_A | I063_SIG_B,
                           &asserted), WT_FFM_ERROR_NOT_READY);
    EXPECT_INT(asserted, 0);

    /* Two NS connects feed the server's masked loop; the irritator stays
     * starved through both iterations. */
    ns_a_msg = 0xFFFFU;
    handle = wt_ffm_connect_begin(&runtime, I063_NS_CLIENT, I063_SVC_A_SID, 1U,
                                  &ns_a_msg);
    EXPECT_TRUE(PSA_HANDLE_IS_VALID(handle));
    asserted = 0U;
    EXPECT_INT(wt_ffm_wait(&runtime, I063_SERVER_ID, I063_SIG_A | I063_SIG_B,
                           &asserted), WT_FFM_SUCCESS);
    EXPECT_INT(asserted, I063_SIG_A);
    i063_server_serve(&runtime, I063_SIG_A, PSA_ERROR_CONNECTION_REFUSED);
    EXPECT_INT((int)wt_ffm_connect_finish(&runtime, ns_a_msg),
               (int)PSA_ERROR_CONNECTION_REFUSED);
    EXPECT_INT(wt_ffm_msg_complete(&runtime, client.pending_msg), 0);

    ns_b_msg = 0xFFFFU;
    handle = wt_ffm_connect_begin(&runtime, I063_NS_CLIENT, I063_SVC_B_SID, 1U,
                                  &ns_b_msg);
    EXPECT_TRUE(PSA_HANDLE_IS_VALID(handle));
    asserted = 0U;
    EXPECT_INT(wt_ffm_wait(&runtime, I063_SERVER_ID, I063_SIG_A | I063_SIG_B,
                           &asserted), WT_FFM_SUCCESS);
    EXPECT_INT(asserted, I063_SIG_B);
    i063_server_serve(&runtime, I063_SIG_B, PSA_ERROR_CONNECTION_REFUSED);
    EXPECT_INT((int)wt_ffm_connect_finish(&runtime, ns_b_msg),
               (int)PSA_ERROR_CONNECTION_REFUSED);
    EXPECT_INT(wt_ffm_msg_complete(&runtime, client.pending_msg), 0);

    /* Server finishes its loop and finally waits on the irritator signal. */
    asserted = 0U;
    EXPECT_INT(wt_ffm_wait(&runtime, I063_SERVER_ID, I063_SIG_IRR, &asserted),
               WT_FFM_SUCCESS);
    EXPECT_INT(asserted, I063_SIG_IRR);
    i063_server_serve(&runtime, I063_SIG_IRR, PSA_ERROR_CONNECTION_REFUSED);

    /* The starved origination now completes: gate CONNECT pass 2 harvests the
     * refused connect, matching i063's psa_connect == CONNECTION_REFUSED. */
    EXPECT_INT(wt_ffm_msg_complete(&runtime, client.pending_msg), 1);
    EXPECT_INT(wt_spm_gate(&runtime, NULL, &client), WT_FFM_SUCCESS);
    EXPECT_INT(client.ret_int, WT_FFM_SUCCESS);
    EXPECT_INT(client.pending_valid, 0U);
    EXPECT_INT(wt_spm_call_would_block(&client), 0);
    EXPECT_INT((int)client.ret_handle, (int)PSA_ERROR_CONNECTION_REFUSED);

    /* Client clears the doorbell (client_main's psa_clear after the connect). */
    EXPECT_INT(wt_ffm_clear(&runtime, I063_CLIENT_ID), WT_FFM_SUCCESS);
    asserted = 0xFFFFFFFFU;
    EXPECT_INT(wt_ffm_wait(&runtime, I063_CLIENT_ID, PSA_WAIT_ANY, &asserted),
               WT_FFM_ERROR_NOT_READY);

    (void)printf("PASS: WT-FFM-0014 doorbell-driven origination, masked "
                 "starvation (i063)\n");
}

static void test_gate_validates_buffers(void)
{
    static uint8_t scratch[64];
    static uint8_t outside[16];
    wt_ffm_runtime_t runtime;
    wt_secure_domain_t domain;
    wt_spm_call_t call;

    EXPECT_INT(wt_ffm_init(&runtime, &g_manifest, &g_port_ops, NULL),
               WT_FFM_SUCCESS);

    (void)memset(&domain, 0, sizeof(domain));
    domain.regions[0].base = (uintptr_t)scratch;
    domain.regions[0].size = sizeof(scratch);
    domain.regions[0].attributes = WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE;
    domain.region_count = 1U;

    /* Read destination inside the domain: validation passes, so the op runs
     * (an invalid handle then returns zero bytes, not a rejection). */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_READ;
    call.partition_id = TEST_PARTITION_ID;
    call.buffer = scratch;
    call.num_bytes = 16U;
    EXPECT_INT(wt_spm_gate(&runtime, &domain, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    EXPECT_SIZE(call.ret_size, 0U);

    /* Read destination outside the domain: rejected before wt_ffm_read runs. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_READ;
    call.partition_id = TEST_PARTITION_ID;
    call.buffer = outside;
    call.num_bytes = 16U;
    EXPECT_INT(wt_spm_gate(&runtime, &domain, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_BUFFER);

    /* Write source outside the domain is rejected the same way. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WRITE;
    call.partition_id = TEST_PARTITION_ID;
    call.buffer = outside;
    call.num_bytes = 16U;
    EXPECT_INT(wt_spm_gate(&runtime, &domain, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_BUFFER);

    /* NULL SP pointer with a domain present is rejected. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_READ;
    call.partition_id = TEST_PARTITION_ID;
    call.buffer = NULL;
    call.num_bytes = 16U;
    EXPECT_INT(wt_spm_gate(&runtime, &domain, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_BUFFER);

    /* FF-M zero-length write: passes the gate check even off-domain (i003's
     * server writes 0 bytes); the dead handle then fails ARGUMENT, not
     * BUFFER, proving the rejection no longer happens at the gate. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WRITE;
    call.partition_id = TEST_PARTITION_ID;
    call.buffer = outside;
    call.num_bytes = 0U;
    EXPECT_INT(wt_spm_gate(&runtime, &domain, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_ERROR_ARGUMENT);

    /* Zero-length read likewise reaches the message layer and returns 0. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_READ;
    call.partition_id = TEST_PARTITION_ID;
    call.buffer = NULL;
    call.num_bytes = 0U;
    EXPECT_INT(wt_spm_gate(&runtime, &domain, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    EXPECT_SIZE(call.ret_size, 0U);

    /* No domain: validation is bypassed and the op runs on the raw pointer.
     * The direct transport is exactly this shape — assert it matches. */
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_READ;
    call.partition_id = TEST_PARTITION_ID;
    call.buffer = outside;
    call.num_bytes = 16U;
    EXPECT_INT(wt_spm_transport_direct(&runtime, &call), WT_FFM_SUCCESS);
    EXPECT_INT(call.ret_int, WT_FFM_SUCCESS);
    EXPECT_SIZE(call.ret_size, 0U);
    (void)printf("PASS: WT-FFM-0014 gate bounds SP pointers to the domain\n");
}

int main(void)
{
    test_gate_equivalence();
    test_gate_would_block();
    test_gate_doorbell_state_machine();
    test_doorbell_origination();
    test_gate_validates_buffers();

    if (g_failures != 0U) {
        (void)fprintf(stderr, "FAIL: %u/%u checks failed\n", g_failures,
                      g_checks);
        return 1;
    }
    (void)printf("PASS: spm_gate (%u checks)\n", g_checks);
    return 0;
}
