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

#include "wolftrust/ipc.h"

#include <stdio.h>
#include <string.h>

static unsigned int g_checks;
static unsigned int g_failures;

#define EXPECT_RESULT(actual, expected) \
    do { \
        int actual_result = (actual); \
        int expected_result = (expected); \
        g_checks++; \
        if (actual_result != expected_result) { \
            (void)fprintf(stderr, "line %d: expected %d, received %d\n", \
                          __LINE__, expected_result, actual_result); \
            g_failures++; \
        } \
    } while (0)

#define EXPECT_STATE(connection, expected) \
    do { \
        g_checks++; \
        if ((connection).state != (expected)) { \
            (void)fprintf(stderr, "line %d: unexpected IPC state\n", \
                          __LINE__); \
            g_failures++; \
        } \
    } while (0)

static wt_ipc_handle_t wt_connection_handle(void)
{
    wt_ipc_handle_t handle;

    (void)memset(&handle, 0, sizeof(handle));
    (void)wt_ipc_handle_create(&handle, 0x1001U, 7U, 1U,
                               WT_IPC_HANDLE_CONNECTION);
    return handle;
}

static void wt_test_connection_flow(void)
{
    wt_ipc_connection_t connection;
    wt_ipc_handle_t handle = wt_connection_handle();

    EXPECT_RESULT(wt_ipc_connection_open(&connection, &handle, 1U,
                                         0x1000U, 2U, false), WT_IPC_VALID);
    EXPECT_STATE(connection, WT_IPC_CONNECTION_PENDING_CONNECT);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_CONNECT_ACCEPT),
                  WT_IPC_VALID);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_CONNECT_COMPLETE),
                  WT_IPC_VALID);
    EXPECT_STATE(connection, WT_IPC_CONNECTION_IDLE);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_REQUEST),
                  WT_IPC_VALID);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_REQUEST_ACTIVE),
                  WT_IPC_VALID);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_REPLY),
                  WT_IPC_VALID);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_CLOSE),
                  WT_IPC_VALID);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_CLOSE_COMPLETE),
                  WT_IPC_VALID);
    EXPECT_STATE(connection, WT_IPC_CONNECTION_FREE);
}

static void wt_test_handle_ownership(void)
{
    wt_ipc_connection_t connection;
    wt_ipc_handle_t handle = wt_connection_handle();
    wt_ipc_handle_t forged = handle;
    wt_ipc_handle_t invalid = handle;

    EXPECT_RESULT(wt_ipc_connection_open(&connection, &handle, 1U,
                                         0x1000U, 1U, false), WT_IPC_VALID);
    forged.value++;
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &forged, WT_IPC_EVENT_CONNECT_ACCEPT),
                  WT_IPC_ERROR_HANDLE);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 2U, &handle, WT_IPC_EVENT_CONNECT_ACCEPT),
                  WT_IPC_ERROR_OWNER);
    EXPECT_STATE(connection, WT_IPC_CONNECTION_PENDING_CONNECT);
    EXPECT_RESULT(wt_ipc_handle_create(&forged, 0U, 1U, 1U,
                                       WT_IPC_HANDLE_CONNECTION),
                  WT_IPC_ERROR_HANDLE);
    invalid.type = (wt_ipc_handle_type_t)99;
    EXPECT_RESULT(wt_ipc_handle_matches(&handle, &invalid),
                  WT_IPC_ERROR_HANDLE);
}

static void wt_test_stateless_connection(void)
{
    wt_ipc_connection_t connection;
    wt_ipc_handle_t handle = wt_connection_handle();

    EXPECT_RESULT(wt_ipc_connection_open(&connection, &handle, 1U,
                                         0x2000U, 1U, true), WT_IPC_VALID);
    EXPECT_STATE(connection, WT_IPC_CONNECTION_IDLE);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_CLOSE),
                  WT_IPC_ERROR_STATE);
    EXPECT_RESULT(wt_ipc_connection_transition(
                      &connection, 1U, &handle, WT_IPC_EVENT_REQUEST),
                  WT_IPC_VALID);
}

static void wt_test_vectors(void)
{
    wt_ipc_vector_t inputs[2] = {{ 0x1000U, 4U }, { 0U, 0U }};
    wt_ipc_vector_t outputs[2] = {{ 0x2000U, 8U }, { 0U, 0U }};
    wt_ipc_vector_t invalid = { 0U, 1U };
    size_t total = 0U;

    EXPECT_RESULT(wt_ipc_validate_vectors(inputs, 2U, outputs, 2U, 16U,
                                          &total), WT_IPC_VALID);
    EXPECT_RESULT((int)total, 12);
    EXPECT_RESULT(wt_ipc_validate_vectors(inputs, 4U, outputs, 1U, 0U,
                                          &total), WT_IPC_ERROR_VECTOR_COUNT);
    EXPECT_RESULT(wt_ipc_validate_vectors(&invalid, 1U, NULL, 0U, 0U,
                                          &total), WT_IPC_ERROR_VECTOR_ADDRESS);
    EXPECT_RESULT(wt_ipc_validate_vectors(inputs, 2U, outputs, 2U, 8U,
                                          &total), WT_IPC_ERROR_VECTOR_LIMIT);
    inputs[0].base = UINTPTR_MAX;
    inputs[0].length = 2U;
    EXPECT_RESULT(wt_ipc_validate_vectors(inputs, 1U, NULL, 0U, 0U,
                                          &total), WT_IPC_ERROR_VECTOR_OVERFLOW);
    EXPECT_RESULT(wt_ipc_validate_vectors(inputs, 1U, NULL, 0U, 0U, NULL),
                  WT_IPC_ERROR_ARGUMENT);
}

static void wt_test_arguments(void)
{
    wt_ipc_connection_t connection;
    wt_ipc_handle_t handle = wt_connection_handle();

    EXPECT_RESULT(wt_ipc_connection_open(NULL, &handle, 1U,
                                         0x1000U, 1U, false),
                  WT_IPC_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_ipc_connection_open(&connection, &handle, 0U,
                                         0x1000U, 1U, false),
                  WT_IPC_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_ipc_connection_transition(&connection, 1U, &handle,
                                               (wt_ipc_connection_event_t)99),
                  WT_IPC_ERROR_ARGUMENT);
}

int main(void)
{
    wt_test_connection_flow();
    wt_test_handle_ownership();
    wt_test_stateless_connection();
    wt_test_vectors();
    wt_test_arguments();
    if (g_failures != 0U) {
        (void)fprintf(stderr, "IPC checks failed: %u/%u\n",
                      g_failures, g_checks);
        return 1;
    }
    (void)printf("IPC checks passed: %u\n", g_checks);
    return 0;
}
