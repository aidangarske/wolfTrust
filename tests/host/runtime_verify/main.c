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

/* Host proof of the runtime re-measurement decision (P6-S5, WT-FFM-0052 /
 * WT-SYS-0013). The on-demand re-measure re-hashes a domain's window against
 * its pinned record; a match keeps trust, and every mismatch (tamper,
 * rollback, layout, or a launch-required guest that can no longer be measured)
 * must fail closed so the monitor quarantines the domain instead of trusting
 * the boot-time measurement. The monitor wiring (wt_runtime_verify_guest ->
 * wt_monitor_quarantine_guest) is proven on M33MU; this drives the neutral
 * decision directly. */

#include "wolftrust/guest_verify.h"

#include <wolfssl/wolfcrypt/hash.h>

#include <stdio.h>
#include <string.h>

static int g_failures;

static void check(int ok, const char* what)
{
    if (ok) {
        (void)printf("PASS: %s\n", what);
    } else {
        (void)printf("FAIL: %s\n", what);
        g_failures++;
    }
}

static uint8_t g_image[1024];

static wt_guest_measurement_t make_record(uint32_t version, uint32_t image_size)
{
    wt_guest_measurement_t record;

    (void)memset(&record, 0, sizeof(record));
    record.guest_id = 0u;
    record.version = version;
    record.image_size = image_size;
    (void)wc_Sha256Hash(g_image, image_size, record.digest);
    return record;
}

int main(void)
{
    wt_guest_measurement_t record;
    size_t i;
    int ret;

    for (i = 0u; i < sizeof(g_image); ++i) {
        g_image[i] = (uint8_t)(i * 7u);
    }
    record = make_record(2u, 512u);

    /* WT-FFM-0052: an untampered window re-measures OK and is not quarantined. */
    ret = wt_runtime_verify_decide(g_image, sizeof(g_image), &record, 1u, 1);
    check(ret == WT_GUEST_VERIFY_OK &&
              !wt_runtime_verify_should_quarantine(ret),
          "WT-FFM-0052 untampered re-measure passes, no quarantine");

    /* A post-boot tamper is caught and drives quarantine. */
    g_image[100] ^= 0x01u;
    ret = wt_runtime_verify_decide(g_image, sizeof(g_image), &record, 1u, 1);
    check(ret == WT_GUEST_VERIFY_ERROR_DIGEST &&
              wt_runtime_verify_should_quarantine(ret),
          "WT-FFM-0052 tampered window fails closed and quarantines");
    g_image[100] ^= 0x01u;
    ret = wt_runtime_verify_decide(g_image, sizeof(g_image), &record, 1u, 1);
    check(ret == WT_GUEST_VERIFY_OK, "WT-FFM-0052 restored window re-passes");

    /* A guest with no launch policy has nothing pinned: passes, no quarantine. */
    ret = wt_runtime_verify_decide(g_image, sizeof(g_image), &record, 1u, 0);
    check(ret == WT_GUEST_VERIFY_OK &&
              !wt_runtime_verify_should_quarantine(ret),
          "WT-FFM-0052 a guest with no launch policy is not re-measured");

    /* A launch-required guest with no record cannot be confirmed: fail closed. */
    ret = wt_runtime_verify_decide(g_image, sizeof(g_image), NULL, 1u, 1);
    check(ret == WT_GUEST_VERIFY_ERROR_ARGUMENT &&
              wt_runtime_verify_should_quarantine(ret),
          "WT-FFM-0052 a launch-required guest with no record fails closed");

    /* A window that no longer covers its image size fails closed. */
    ret = wt_runtime_verify_decide(g_image, 256u, &record, 1u, 1);
    check(ret == WT_GUEST_VERIFY_ERROR_LAYOUT &&
              wt_runtime_verify_should_quarantine(ret),
          "WT-FFM-0052 a shrunken window fails closed");

    /* A record below the version floor fails closed at re-measurement too. */
    ret = wt_runtime_verify_decide(g_image, sizeof(g_image), &record, 3u, 1);
    check(ret == WT_GUEST_VERIFY_ERROR_VERSION &&
              wt_runtime_verify_should_quarantine(ret),
          "WT-FFM-0052 a rolled-back record fails closed");

    if (g_failures == 0) {
        (void)printf("runtime_verify host suite: all checks passed\n");
        return 0;
    }
    (void)printf("runtime_verify host suite: %d failure(s)\n", g_failures);
    return 1;
}
