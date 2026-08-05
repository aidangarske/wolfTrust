/* generated_main.c
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

#include "wolftrust_manifest_generated.h"

#include <stdio.h>

int main(void)
{
    const uint32_t supported_features = WT_MANIFEST_FEATURE_IPC |
                                        WT_MANIFEST_FEATURE_STATELESS;
    int result = wt_manifest_validate(wt_generated_manifest_get(),
                                      supported_features);

    if (result != WT_MANIFEST_VALID) {
        (void)fprintf(stderr, "generated manifest validation failed: %d\n",
                      result);
        return 1;
    }

    (void)printf("generated manifest validation passed\n");
    return 0;
}
