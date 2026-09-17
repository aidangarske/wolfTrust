/* mimxrt798_regs.h
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

#ifndef WOLFTRUST_MIMXRT798_REGS_H
#define WOLFTRUST_MIMXRT798_REGS_H

#include <stdint.h>

/* Secure-alias peripheral bases (MIMXRT798S cm33_core0, NS alias = base -
 * 0x10000000). */
#define WT_PERIPH_NS_BASE        0x40000000u
#define WT_PERIPH_S_BASE         0x50000000u
#define WT_PERIPH_ALIAS_SIZE     0x10000000u

#define WT_RSTCTL0_BASE_S        0x50000000u
#define WT_CLKCTL0_BASE_S        0x50001000u
#define WT_SYSCON0_BASE_S        0x50002000u
#define WT_IOPCTL0_BASE_S        0x50004000u
#define WT_ITRC_BASE_S           0x50012000u
#define WT_OCOTP_BASE_S          0x50018000u
#define WT_XCACHE0_BASE_S        0x50033000u
#define WT_XCACHE1_BASE_S        0x50034000u
#define WT_GPIO0_BASE_S          0x50100000u
#define WT_LPUART0_BASE_S        0x50110000u
#define WT_LPUART0_BASE_NS       0x40110000u
#define WT_LPUART1_BASE_S        0x50111000u
#define WT_AHBSC0_BASE_S         0x5017C000u
#define WT_GLIKEY0_BASE_S        0x5017CC00u
#define WT_XSPI0_BASE_S          0x50184000u
#define WT_XSPI1_BASE_S          0x50185000u
#define WT_TRNG_BASE_S           0x50187000u
#define WT_ELS_BASE_S            0x50190000u
#define WT_PUF_BASE_S            0x50194000u
#define WT_AHBSC3_BASE_S         0x50220000u
#define WT_GLIKEY1_BASE_S        0x50220C00u
#define WT_AHBSC4_BASE_S         0x50400000u
#define WT_GLIKEY2_BASE_S        0x50400C00u

#define WT_REG32(address)        (*(volatile uint32_t*)(address))

/* TRNG (NXP TRNG block). */
#define WT_TRNG_MCTL             WT_REG32(WT_TRNG_BASE_S + 0x000u)
#define WT_TRNG_MCTL_RST_DEF     (1u << 6)
#define WT_TRNG_MCTL_ENT_VAL     (1u << 10)
#define WT_TRNG_MCTL_ERR         (1u << 12)
#define WT_TRNG_MCTL_PRGM        (1u << 16)
#define WT_TRNG_ENT(index)       WT_REG32(WT_TRNG_BASE_S + 0x040u + 4u * (index))
#define WT_TRNG_ENT_COUNT        16u

/* GLIKEY write-enable state machine guarding each AHBSC instance. */
#define WT_GLIKEY_CTRL_0(base)   WT_REG32((base) + 0x0u)
#define WT_GLIKEY_CTRL_1(base)   WT_REG32((base) + 0x4u)
#define WT_GLIKEY_STATUS(base)   WT_REG32((base) + 0xCu)
#define WT_GLIKEY_CODEWORD_STEP1 0xF0C10F3Eu
#define WT_GLIKEY_CODEWORD_STEP2 0x0F1DF0E2u
#define WT_GLIKEY_CODEWORD_STEP3 0xF0B00F4Fu
#define WT_GLIKEY_CODEWORD_EN    0x0FFFF000u

/* AHB secure controller: per-bus-master SRAM access enables and the global
 * check switch (secure control register at the end of each instance). */
#define WT_AHBSC_MISC_CTRL_REG(base)     WT_REG32((base) + 0xFFCu)
#define WT_AHBSC_MISC_CTRL_DP_REG(base)  WT_REG32((base) + 0xFF8u)
#define WT_AHBSC3_COMPUTE_APB_ACCESS     WT_REG32(WT_AHBSC3_BASE_S + 0xFA0u)
#define WT_AHBSC3_SENSE_APB_ACCESS       WT_REG32(WT_AHBSC3_BASE_S + 0xFA4u)
#define WT_AHBSC3_COMPUTE_AIPS_ACCESS    WT_REG32(WT_AHBSC3_BASE_S + 0xFB0u)
#define WT_AHBSC3_SENSE_AIPS_ACCESS      WT_REG32(WT_AHBSC3_BASE_S + 0xFB4u)

#endif /* WOLFTRUST_MIMXRT798_REGS_H */
