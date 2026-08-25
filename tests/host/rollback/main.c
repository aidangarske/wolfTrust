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

/* WT-FFM-0050: a wolfTrust or guest image below the monotonic version floor
 * is refused before launch; a successful authenticated boot advances the
 * floor; floors never retreat; the unlocked provisioning lifecycles bypass
 * refusal so a version floor cannot brick development flows. */

#include "wolftrust/rollback.h"

#include "psa/lifecycle.h"

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

#define EXPECT_U32(actual, expected) \
    do { \
        uint32_t actual_value = (actual); \
        uint32_t expected_value = (expected); \
        g_checks++; \
        if (actual_value != expected_value) { \
            (void)fprintf(stderr, "line %d: expected %u, received %u\n", \
                          __LINE__, expected_value, actual_value); \
            g_failures++; \
        } \
    } while (0)

static void wt_test_enforced_lifecycles(void)
{
    static const uint32_t locked[] = {
        PSA_LIFECYCLE_UNKNOWN,
        PSA_LIFECYCLE_SECURED,
        PSA_LIFECYCLE_NON_PSA_ROT_DEBUG,
        PSA_LIFECYCLE_RECOVERABLE_PSA_ROT_DEBUG,
        PSA_LIFECYCLE_DECOMMISSIONED
    };
    size_t i;

    for (i = 0u; i < sizeof(locked) / sizeof(locked[0]); i++) {
        EXPECT_RESULT(wt_rollback_check(locked[i], 2u, 2u), WT_ROLLBACK_OK);
        EXPECT_RESULT(wt_rollback_check(locked[i], 3u, 2u), WT_ROLLBACK_OK);
        EXPECT_RESULT(wt_rollback_check(locked[i], 1u, 2u),
                      WT_ROLLBACK_REFUSED);
        EXPECT_RESULT(wt_rollback_check(locked[i], 0u, 1u),
                      WT_ROLLBACK_REFUSED);
    }
}

static void wt_test_unlocked_lifecycles(void)
{
    EXPECT_RESULT(wt_rollback_check(PSA_LIFECYCLE_ASSEMBLY_AND_TEST, 1u, 5u),
                  WT_ROLLBACK_OK);
    EXPECT_RESULT(wt_rollback_check(PSA_LIFECYCLE_PSA_ROT_PROVISIONING,
                                    0u, 5u),
                  WT_ROLLBACK_OK);
}

static void wt_test_floor_advance(void)
{
    uint32_t floor = 0u;

    EXPECT_RESULT(wt_rollback_advance(1u, &floor), 1);
    EXPECT_U32(floor, 1u);
    EXPECT_RESULT(wt_rollback_advance(1u, &floor), 0);
    EXPECT_U32(floor, 1u);
    EXPECT_RESULT(wt_rollback_advance(3u, &floor), 1);
    EXPECT_U32(floor, 3u);
    /* Floors never retreat. */
    EXPECT_RESULT(wt_rollback_advance(2u, &floor), 0);
    EXPECT_U32(floor, 3u);
    EXPECT_RESULT(wt_rollback_advance(3u, NULL), 0);
}

static void wt_test_boot_sequence(void)
{
    wt_rollback_table_t table;
    size_t i;

    /* First boot: absent table initializes to zero floors, version 1 boots
     * and advances the image floor. */
    wt_rollback_table_init(&table);
    EXPECT_RESULT(wt_rollback_table_valid(&table), 1);
    EXPECT_U32(table.image_floor, 0u);
    EXPECT_RESULT(wt_rollback_check(PSA_LIFECYCLE_SECURED, 1u,
                                    table.image_floor), WT_ROLLBACK_OK);
    EXPECT_RESULT(wt_rollback_advance(1u, &table.image_floor), 1);

    /* Guests bind their pinned record versions to per-guest floors. */
    for (i = 0u; i < WT_GUEST_MEAS_MAX_RECORDS; i++) {
        EXPECT_RESULT(wt_rollback_check(PSA_LIFECYCLE_SECURED, 1u,
                                        table.guest_floor[i]),
                      WT_ROLLBACK_OK);
        EXPECT_RESULT(wt_rollback_advance(1u, &table.guest_floor[i]), 1);
    }

    /* Same-version reboot: accepted, no floor movement (no NVM write). */
    EXPECT_RESULT(wt_rollback_check(PSA_LIFECYCLE_SECURED, 1u,
                                    table.image_floor), WT_ROLLBACK_OK);
    EXPECT_RESULT(wt_rollback_advance(1u, &table.image_floor), 0);

    /* Downgraded image after an upgrade: refused before launch. */
    EXPECT_RESULT(wt_rollback_advance(2u, &table.image_floor), 1);
    EXPECT_RESULT(wt_rollback_check(PSA_LIFECYCLE_SECURED, 1u,
                                    table.image_floor),
                  WT_ROLLBACK_REFUSED);

    /* A missing handoff reports version zero: refused once any floor is
     * armed, fail closed. */
    EXPECT_RESULT(wt_rollback_check(PSA_LIFECYCLE_UNKNOWN, 0u,
                                    table.image_floor),
                  WT_ROLLBACK_REFUSED);
}

static void wt_test_table_validity(void)
{
    wt_rollback_table_t table;

    (void)memset(&table, 0xA5, sizeof(table));
    EXPECT_RESULT(wt_rollback_table_valid(&table), 0);
    wt_rollback_table_init(&table);
    EXPECT_RESULT(wt_rollback_table_valid(&table), 1);
    EXPECT_RESULT(wt_rollback_table_valid(NULL), 0);
    wt_rollback_table_init(NULL);
}

int main(void)
{
    wt_test_enforced_lifecycles();
    wt_test_unlocked_lifecycles();
    wt_test_floor_advance();
    wt_test_boot_sequence();
    wt_test_table_validity();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "rollback checks failed: %u/%u\n",
                      g_failures, g_checks);
        return 1;
    }
    (void)printf("WT-FFM-0050 rollback checks passed: %u\n", g_checks);
    return 0;
}
