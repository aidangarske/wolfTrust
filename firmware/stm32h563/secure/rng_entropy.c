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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/*
 * SECURE-SIDE ENTROPY STUB.
 *
 * This file provides wolftrust_rng_generate_block() for development
 * bring-up of the wolfHSM server inside wolfTrust. The output is
 * derived from SysTick CVR (24-bit counter) XOR an LCG counter. This
 * is NOT cryptographically secure entropy: an attacker who can observe
 * SysTick and time the boot can substantially predict the output.
 *
 * wolfCrypt's HashDRBG layer (HAVE_HASHDRBG in user_settings.h) whitens
 * the raw bytes from this function via SHA-256 chaining, which makes
 * the DRBG state non-trivial to invert in offline analysis, but it
 * does NOT compensate for low source entropy: a DRBG seeded from a
 * predictable source produces predictable output.
 *
 * Replace this with a TRNG-backed implementation (STM32H5 RNG
 * peripheral at 0x420C0800) before shipping anything that uses these
 * keys to attest, sign, or encrypt.
 */

#warning "wolfTrust RNG entropy stub in use — NOT CRYPTOGRAPHICALLY SECURE"

#include <stdint.h>
#include <stddef.h>

/* Prototype matches the declaration in user_settings.h (CUSTOM_RAND_GENERATE_BLOCK). */
int wolftrust_rng_generate_block(unsigned char *output, unsigned int sz);

static uint32_t s_counter = 0xA5A5A5A5u;

int wolftrust_rng_generate_block(unsigned char *output, unsigned int sz)
{
    volatile uint32_t *syst_cvr = (volatile uint32_t *)0xE000E018u;

    if (output == NULL && sz != 0)
        return -1;

    while (sz != 0) {
        s_counter = (s_counter * 1103515245u) + 12345u;
        uint32_t mix = s_counter ^ *syst_cvr;
        unsigned int n = sz < 4u ? sz : 4u;
        for (unsigned int i = 0; i < n; ++i) {
            *output++ = (unsigned char)(mix >> (8u * i));
        }
        sz -= n;
    }
    return 0;
}
