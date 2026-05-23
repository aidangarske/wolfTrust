/* coroutine_armv8m.c
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
 * Cortex-M33 (secure mode, MSP-only) context switch.
 *
 * Compiled with: -mcpu=cortex-m33 -mthumb -mcmse -mgeneral-regs-only
 *                -ffreestanding -fno-builtin -nostdlib -O0
 *
 * All coroutines and the bootstrap monitor share MSP_S.  PSP is never
 * touched.  This keeps the same scheduler callable from both Thread Mode
 * and Handler Mode (SysTick).
 *
 * Register frame saved/restored on each context's own MSP stack:
 *
 *   SP+28 : LR (return address into caller / trampoline)
 *   SP+24 : r11
 *   SP+20 : r10
 *   SP+16 : r9
 *   SP+12 : r8
 *   SP+08 : r7
 *   SP+04 : r6
 *   SP+00 : r5   (r4 is the lowest, but we push {r4-r11,lr} in one insn,
 *                 so r4 ends up at SP+0 after the push)
 *
 * The actual push {r4-r11, lr} layout on a descending stack:
 *   before push, sp points above the frame.
 *   After push, sp = old_sp - 9*4:
 *     [sp+0]  = r4
 *     [sp+4]  = r5
 *     [sp+8]  = r6
 *     [sp+12] = r7
 *     [sp+16] = r8
 *     [sp+20] = r9
 *     [sp+24] = r10
 *     [sp+28] = r11
 *     [sp+32] = lr
 *
 * The trampoline stuffs `arg` into r4 and `entry` into r5 in
 * wt_co_arch_init_stack so the first pop restores them correctly.
 */

#include "wolftrust/sched/coroutine_internal.h"

#include <stddef.h>
#include <stdint.h>

extern void wt_platform_panic(void);

/* sp is the first field in struct wt_co — offset must be 0. */
_Static_assert(offsetof(struct wt_co, sp) == 0,
               "struct wt_co: sp must be the first field (offset 0)");

/* -------------------------------------------------------------------------
 * Trampoline — entered by the first pop {r4-r11,lr} + bx lr that lands
 * here.  r4 holds arg, r5 holds entry (set up by wt_co_arch_init_stack).
 * ---------------------------------------------------------------------- */

__attribute__((naked))
static void wt_co_trampoline(void)
{
    __asm__ volatile (
        "mov  r0, r4        \n" /* arg  -> first argument */
        "blx  r5            \n" /* call entry(arg)        */
        /* entry() must never return; if it does, fall through to panic. */
        "bl   wt_platform_panic \n"
        "1: b 1b            \n" /* infinite loop safety net */
    );
}

/* -------------------------------------------------------------------------
 * wt_co_arch_init_stack
 *
 * Prepares `co`'s stack so that the first wt_co_arch_switch(*, co) lands
 * in wt_co_trampoline which then calls entry(arg).
 *
 * Frame layout written (top = highest address = initial sp):
 *
 *   initial_sp - 4  : lr  = address of wt_co_trampoline
 *   initial_sp - 8  : r11 = 0
 *   initial_sp - 12 : r10 = 0
 *   initial_sp - 16 : r9  = 0
 *   initial_sp - 20 : r8  = 0
 *   initial_sp - 24 : r7  = 0
 *   initial_sp - 28 : r6  = 0
 *   initial_sp - 32 : r5  = entry  (recovered by trampoline in r5)
 *   initial_sp - 36 : r4  = arg    (recovered by trampoline in r4)
 *
 * co->sp is set to (initial_sp - 36), i.e. the new top of stack.
 * ---------------------------------------------------------------------- */

void wt_co_arch_init_stack(struct wt_co *co,
                            wt_co_entry_fn entry,
                            void *arg)
{
    /* Start at the top of the caller-provided buffer (8-byte aligned). */
    uintptr_t sp = (uintptr_t)co->stack_base + co->stack_size;
    /* Align down to 8 bytes just in case. */
    sp &= ~(uintptr_t)7u;

    uint32_t *frame = (uint32_t *)sp;

    /* Push 9 words (descending): r4, r5, r6, r7, r8, r9, r10, r11, lr */
    frame--;  *frame = (uint32_t)(uintptr_t)(void *)wt_co_trampoline; /* lr  */
    frame--;  *frame = 0u;                                             /* r11 */
    frame--;  *frame = 0u;                                             /* r10 */
    frame--;  *frame = 0u;                                             /* r9  */
    frame--;  *frame = 0u;                                             /* r8  */
    frame--;  *frame = 0u;                                             /* r7  */
    frame--;  *frame = 0u;                                             /* r6  */
    frame--;  *frame = (uint32_t)(uintptr_t)(void *)entry;            /* r5  */
    frame--;  *frame = (uint32_t)(uintptr_t)arg;                      /* r4  */

    co->sp = (uintptr_t)frame;
}

/* -------------------------------------------------------------------------
 * wt_co_arch_switch(from, to)
 *
 * Arguments:  r0 = from (struct wt_co *), r1 = to (struct wt_co *)
 *
 * If from == NULL (r0 == 0) the current MSP is not saved — used only on
 * the very first dispatch where there is no meaningful "from" context to
 * preserve.
 *
 * Since struct wt_co::sp is at offset 0:
 *   from->sp  =>  [r0 + #0]
 *   to->sp    =>  [r1 + #0]
 * ---------------------------------------------------------------------- */

__attribute__((naked))
void wt_co_arch_switch(struct wt_co *from __attribute__((unused)),
                       struct wt_co *to   __attribute__((unused)))
{
    __asm__ volatile (
        /* Save callee-saved registers and LR onto the current MSP stack. */
        "push   {r4-r11, lr}            \n"

        /* If from == NULL skip saving the stack pointer. */
        "cbz    r0, 1f                  \n"

        /* from->sp = sp  (struct wt_co::sp is at offset 0) */
        "mrs    r2, msp                 \n"
        "str    r2, [r0, #0]            \n"

        "1:                             \n"
        /* sp = to->sp */
        "ldr    r2, [r1, #0]            \n"
        "msr    msp, r2                 \n"

        /* Restore callee-saved registers and return into `to`. */
        "pop    {r4-r11, pc}            \n"
    );
}
