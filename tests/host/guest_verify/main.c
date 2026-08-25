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

/* WT-SYS-0002 / WT-FFM-0049: the guest launch predicate accepts only an image
 * whose SHA-256 matches the pinned digest and whose version meets the manifest
 * floor, and fails closed on tamper, rollback, layout, and argument abuse. */

#include "wolftrust/guest_verify.h"

#include "wolfssl/wolfcrypt/hash.h"
#include "wolfssl/wolfcrypt/sha256.h"

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

#define EXPECT_TRUE(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            (void)fprintf(stderr, "line %d: condition failed\n", __LINE__); \
            g_failures++; \
        } \
    } while (0)

static uint8_t g_image[1024];

static wt_guest_measurement_t wt_make_record(uint32_t version,
                                             uint32_t image_size)
{
    wt_guest_measurement_t record;

    (void)memset(&record, 0, sizeof(record));
    record.guest_id = 0u;
    record.version = version;
    record.image_size = image_size;
    (void)wc_Sha256Hash(g_image, image_size, record.digest);
    return record;
}

static void wt_test_accept(void)
{
    wt_guest_measurement_t record = wt_make_record(2u, 512u);

    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_OK);
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 2u),
                  WT_GUEST_VERIFY_OK);
    /* The digest pins exactly image_size bytes, so a full-window image is
     * accepted when hashed at its recorded size. */
    record = wt_make_record(1u, (uint32_t)sizeof(g_image));
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_OK);
}

static void wt_test_tamper(void)
{
    wt_guest_measurement_t record = wt_make_record(2u, 512u);

    g_image[100] ^= 0x01u;
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_ERROR_DIGEST);
    g_image[100] ^= 0x01u;
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_OK);

    /* Tampering the pinned digest itself is also a refusal. */
    record.digest[0] ^= 0x80u;
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_ERROR_DIGEST);

    /* A byte beyond image_size is outside the pin and must not matter. */
    record = wt_make_record(2u, 512u);
    g_image[900] ^= 0xFFu;
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_OK);
    g_image[900] ^= 0xFFu;
}

static void wt_test_version(void)
{
    wt_guest_measurement_t record = wt_make_record(1u, 512u);

    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 2u),
                  WT_GUEST_VERIFY_ERROR_VERSION);
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 0u),
                  WT_GUEST_VERIFY_OK);
}

static void wt_test_layout(void)
{
    wt_guest_measurement_t record = wt_make_record(1u, 512u);

    record.image_size = 0u;
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_ERROR_LAYOUT);

    record.image_size = (uint32_t)sizeof(g_image) + 1u;
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_ERROR_LAYOUT);
}

static void wt_test_arguments(void)
{
    wt_guest_measurement_t record = wt_make_record(1u, 512u);

    EXPECT_RESULT(wt_guest_verify_image(NULL, sizeof(g_image), &record, 1u),
                  WT_GUEST_VERIFY_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_guest_verify_image(g_image, 0u, &record, 1u),
                  WT_GUEST_VERIFY_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_guest_verify_image(g_image, sizeof(g_image), NULL, 1u),
                  WT_GUEST_VERIFY_ERROR_ARGUMENT);
}

static void wt_test_measurement_table(void)
{
    wt_guest_measurement_t record = wt_make_record(3u, 512u);
    const wt_guest_measurement_t* stored;
    const char* name = NULL;
    size_t i;

    wt_guest_measurement_reset();
    EXPECT_TRUE(wt_guest_measurement_count() == 0u);
    EXPECT_TRUE(wt_guest_measurement_get(0u, &name) == NULL);

    EXPECT_RESULT(wt_guest_measurement_record(&record, "guest-a"),
                  WT_GUEST_VERIFY_OK);
    EXPECT_TRUE(wt_guest_measurement_count() == 1u);
    stored = wt_guest_measurement_get(0u, &name);
    EXPECT_TRUE(stored != NULL);
    EXPECT_TRUE(stored != NULL &&
                memcmp(stored->digest, record.digest,
                       WT_GUEST_MEAS_DIGEST_SIZE) == 0);
    EXPECT_TRUE(stored != NULL && stored->version == 3u);
    EXPECT_TRUE(name != NULL && strcmp(name, "guest-a") == 0);

    EXPECT_RESULT(wt_guest_measurement_record(NULL, "guest-a"),
                  WT_GUEST_VERIFY_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_guest_measurement_record(&record, NULL),
                  WT_GUEST_VERIFY_ERROR_ARGUMENT);

    /* A name longer than the slot is truncated, never overflowed. */
    EXPECT_RESULT(wt_guest_measurement_record(&record,
                  "guest-name-way-beyond-the-slot"),
                  WT_GUEST_VERIFY_OK);
    stored = wt_guest_measurement_get(1u, &name);
    EXPECT_TRUE(stored != NULL && name != NULL &&
                strlen(name) == WT_GUEST_MEAS_NAME_LEN - 1u);

    for (i = wt_guest_measurement_count();
         i < WT_GUEST_MEAS_MAX_RECORDS; i++) {
        EXPECT_RESULT(wt_guest_measurement_record(&record, "guest-x"),
                      WT_GUEST_VERIFY_OK);
    }
    EXPECT_RESULT(wt_guest_measurement_record(&record, "guest-x"),
                  WT_GUEST_VERIFY_ERROR_CAPACITY);

    wt_guest_measurement_reset();
    EXPECT_TRUE(wt_guest_measurement_count() == 0u);
}

int main(void)
{
    size_t i;

    for (i = 0u; i < sizeof(g_image); i++) {
        g_image[i] = (uint8_t)(i * 7u);
    }

    wt_test_accept();
    wt_test_tamper();
    wt_test_version();
    wt_test_layout();
    wt_test_arguments();
    wt_test_measurement_table();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "guest-verify checks failed: %u/%u\n",
                      g_failures, g_checks);
        return 1;
    }
    (void)printf("WT-SYS-0002/WT-FFM-0049 guest-verify checks passed: %u\n",
                 g_checks);
    return 0;
}
