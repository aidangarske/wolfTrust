/* rng_entropy.c
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

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mimxrt798_regs.h"

#include "wolftrust/spm_transport.h"
#include "wolftrust/spm_gate.h"
#include "wolftrust/arch.h"

int wolftrust_rng_generate_block(unsigned char *output, unsigned int sz);
int wolftrust_rng_generate_block_direct(unsigned char *output,
                                        unsigned int sz);

#define WT_TRNG_ENT_SPINS 0x00200000u

static bool s_rng_ready;

static int wt_trng_init(void)
{
    uint32_t spins = 0u;

    /* Leave the ROM's ring-oscillator tuning in place; a generation is
     * requested by dropping PRGM and waiting for the entropy-valid flag. */
    WT_TRNG_MCTL &= ~WT_TRNG_MCTL_PRGM;
    while ((WT_TRNG_MCTL & WT_TRNG_MCTL_ENT_VAL) == 0u) {
        if ((WT_TRNG_MCTL & WT_TRNG_MCTL_ERR) != 0u ||
                ++spins > WT_TRNG_ENT_SPINS) {
            return -1;
        }
    }
    return 0;
}

static int wt_trng_fill(unsigned char *output, unsigned int sz)
{
    unsigned int done = 0u;
    uint32_t index = 0u;
    uint32_t word;
    uint32_t spins;
    unsigned int chunk;

    while (done < sz) {
        if (index == 0u) {
            spins = 0u;
            while ((WT_TRNG_MCTL & WT_TRNG_MCTL_ENT_VAL) == 0u) {
                if ((WT_TRNG_MCTL & WT_TRNG_MCTL_ERR) != 0u ||
                        ++spins > WT_TRNG_ENT_SPINS) {
                    return -1;
                }
            }
        }
        word = WT_TRNG_ENT(index);
        chunk = sz - done;
        if (chunk > sizeof(word)) {
            chunk = sizeof(word);
        }
        (void)memcpy(output + done, &word, chunk);
        done += chunk;
        /* Reading the last word restarts the generation, clearing ENT_VAL. */
        index = (index + 1u) % WT_TRNG_ENT_COUNT;
    }
    return 0;
}

int wolftrust_rng_generate_block_direct(unsigned char *output, unsigned int sz)
{
    if (output == NULL && sz != 0u) {
        return -1;
    }
    if (!s_rng_ready) {
        if (wt_trng_init() != 0) {
            return -1;
        }
        s_rng_ready = true;
    }
    if (sz == 0u) {
        return 0;
    }
    return wt_trng_fill(output, sz);
}

int wolftrust_rng_generate_block(unsigned char *output, unsigned int sz)
{
    /* A confined keystore partition cannot touch the TRNG; the SVC
     * dispatcher runs the direct half privileged. */
    if (wt_arch_thread_unprivileged()) {
        wt_spm_call_t call;

        (void)memset(&call, 0, sizeof(call));
        call.op = WT_SPM_OP_KEYSTORE_ENTROPY;
        call.buffer = output;
        call.num_bytes = sz;
        if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS) {
            return -1;
        }
        return call.ret_int;
    }
    return wolftrust_rng_generate_block_direct(output, sz);
}
