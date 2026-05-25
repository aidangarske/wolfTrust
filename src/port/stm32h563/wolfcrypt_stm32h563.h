/* wolfcrypt_stm32h563.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 *
 * wolfTrust is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WOLFTRUST_PORT_STM32H563_WOLFCRYPT_H
#define WOLFTRUST_PORT_STM32H563_WOLFCRYPT_H

#include <stdint.h>

#ifndef __IO
#define __IO volatile
#endif

typedef struct {
    __IO uint32_t CR;
    __IO uint32_t DIN;
    __IO uint32_t STR;
    __IO uint32_t HR[5];
    __IO uint32_t IMR;
    __IO uint32_t SR;
    uint32_t RESERVED[52];
    __IO uint32_t CSR[103];
} HASH_TypeDef;

typedef struct {
    __IO uint32_t HR[16];
} HASH_DIGEST_TypeDef;

#define HASH_BASE_S              0x520A0400u
#define HASH_DIGEST_BASE_S       0x520A0710u
#define HASH                     ((HASH_TypeDef*)HASH_BASE_S)
#define HASH_DIGEST              ((HASH_DIGEST_TypeDef*)HASH_DIGEST_BASE_S)

#define HASH_CR_INIT             (1u << 2)
#define HASH_DATATYPE_8B         0u
#define HASH_ALGOMODE_HASH       0u
#define HASH_ALGOMODE_HMAC       (1u << 6)
#define HASH_ALGOSELECTION_SHA1  0u
#define HASH_ALGOSELECTION_SHA224 (2u << 17)
#define HASH_ALGOSELECTION_SHA256 ((1u << 17) | (1u << 18))
#define HASH_ALGOSELECTION_SHA384 ((0u << 17) | (1u << 20))
#define HASH_ALGOSELECTION_SHA512 ((1u << 17) | (1u << 20))
#define HASH_ALGOSELECTION_SHA512_224 ((2u << 17) | (1u << 20))
#define HASH_ALGOSELECTION_SHA512_256 ((3u << 17) | (1u << 20))
#define HASH_AlgoSelection_SHA1  HASH_ALGOSELECTION_SHA1
#define HASH_AlgoSelection_SHA224 HASH_ALGOSELECTION_SHA224
#define HASH_AlgoSelection_SHA256 HASH_ALGOSELECTION_SHA256
#define HASH_AlgoSelection_SHA384 HASH_ALGOSELECTION_SHA384
#define HASH_AlgoSelection_SHA512 HASH_ALGOSELECTION_SHA512

#define HASH_STR_NBLW            0x1Fu
#define HASH_STR_DCAL            (1u << 8)
#define HASH_IMR_DINIE           (1u << 0)
#define HASH_IMR_DCIE            (1u << 1)
#define HASH_SR_DINIS            (1u << 0)
#define HASH_SR_DCIS             (1u << 1)
#define HASH_SR_BUSY             (1u << 3)

#define WT_RCC_AHB2ENR_HASHEN    (1u << 17)

static inline void wt_stm32h563_hash_clock_enable(void)
{
    *(volatile uint32_t*)0x54020C8Cu |= WT_RCC_AHB2ENR_HASHEN;
}

static inline void wt_stm32h563_hash_clock_disable(void)
{
    /* Keep HASH clocked for context-switched secure services. */
}

#define STM32_HASH_CLOCK_ENABLE(ctx)  do { (void)(ctx); wt_stm32h563_hash_clock_enable(); } while (0)
#define STM32_HASH_CLOCK_DISABLE(ctx) do { (void)(ctx); wt_stm32h563_hash_clock_disable(); } while (0)

#endif /* WOLFTRUST_PORT_STM32H563_WOLFCRYPT_H */
