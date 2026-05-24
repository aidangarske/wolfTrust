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
 * Cortex-M33 secure-side context switch.
 *
 * Compiled with: -mcpu=cortex-m33 -mthumb -mcmse -mgeneral-regs-only
 *                -ffreestanding -fno-builtin -nostdlib
 *
 * Stack model:
 *   - The bootstrap (monitor) context runs on MSP_S. It is identified by
 *     stack_base == NULL.
 *   - Every other coroutine runs on PSP_S with PSPLIM_S anchored at
 *     stack_base + 4 (immediately above the stack canary). Hitting that
 *     limit triggers UsageFault.STKOF which the platform fault handler
 *     converts into a terminal WT_CO_FAULTED + an NS-visible
 *     WH_ERROR_ABORTED response.
 *
 * wt_co_arch_switch toggles CONTROL.SPSEL on bootstrap↔coroutine
 * transitions; coroutine↔coroutine stays on PSP_S.
 *
 * Saved frame on each context's own stack (descending):
 *
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

/* Field offsets the inline asm depends on. Hard-coded so they expand at
 * preprocessor time; the _Static_asserts catch any struct drift. */
#define WT_CO_SP_OFFSET         0
#define WT_CO_STACK_BASE_OFFSET 4

_Static_assert(offsetof(struct wt_co, sp) == WT_CO_SP_OFFSET,
               "struct wt_co: sp must be at offset 0");
_Static_assert(offsetof(struct wt_co, stack_base) == WT_CO_STACK_BASE_OFFSET,
               "struct wt_co: stack_base must be at offset 4");
_Static_assert(sizeof(uintptr_t) == 4,
               "wt_co struct layout assumes 32-bit pointers");

#define WT_STR2(x) #x
#define WT_STR(x)  WT_STR2(x)

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
 *   r0 = from (struct wt_co *), r1 = to (struct wt_co *)
 *
 * struct wt_co::sp is at offset 0; ::stack_base at +sizeof(uintptr_t).
 * stack_base == NULL identifies the bootstrap context (MSP_S); any
 * non-NULL value identifies a coroutine on PSP_S with PSPLIM_S =
 * stack_base + 4.
 *
 * Both r2 and r3 are caller-clobbered and used freely below.
 * ---------------------------------------------------------------------- */

__attribute__((naked))
void wt_co_arch_switch(struct wt_co *from __attribute__((unused)),
                       struct wt_co *to   __attribute__((unused)))
{
    __asm__ volatile (
        /* Push callee-saved regs + LR on whichever stack is currently
         * active (MSP_S if we were in bootstrap, PSP_S if we were in
         * a coroutine). */
        "push   {r4-r11, lr}                                    \n"

        /* If from == NULL skip saving SP (only on first dispatch). */
        "cbz    r0, 1f                                          \n"

        /* from->stack_base: NULL → bootstrap on MSP_S, else PSP_S. */
        "ldr    r2, [r0, #" WT_STR(WT_CO_STACK_BASE_OFFSET) "]  \n"
        "cbz    r2, 2f                                          \n"

        /* Coroutine: save PSP_S into from->sp. */
        "mrs    r3, psp                                         \n"
        "str    r3, [r0, #0]                                    \n"
        "b      1f                                              \n"

        "2:                                                     \n"
        /* Bootstrap: save MSP_S into from->sp. */
        "mrs    r3, msp                                         \n"
        "str    r3, [r0, #0]                                    \n"

        "1:                                                     \n"
        /* Load to->stack_base again to decide MSP_S vs PSP_S target. */
        "ldr    r2, [r1, #" WT_STR(WT_CO_STACK_BASE_OFFSET) "]  \n"
        "cbz    r2, 3f                                          \n"

        /* Target is a coroutine. Set PSPLIM_S to stack_base + 4 (above
         * the canary), load PSP_S = to->sp, ensure CONTROL.SPSEL=1 so
         * thread mode uses PSP. */
        "adds   r2, r2, #4                                      \n"
        "msr    psplim, r2                                      \n"
        "ldr    r3, [r1, #0]                                    \n"
        "msr    psp, r3                                         \n"
        "mrs    r2, control                                     \n"
        "orrs   r2, r2, #2                                      \n"
        "msr    control, r2                                     \n"
        "isb    0xF                                             \n"
        "b      4f                                              \n"

        "3:                                                     \n"
        /* Target is bootstrap. Restore MSP_S = to->sp, clear
         * CONTROL.SPSEL so thread mode uses MSP, drop PSPLIM_S. */
        "ldr    r3, [r1, #0]                                    \n"
        "msr    msp, r3                                         \n"
        "mrs    r2, control                                     \n"
        "bics   r2, r2, #2                                      \n"
        "msr    control, r2                                     \n"
        "movs   r3, #0                                          \n"
        "msr    psplim, r3                                      \n"
        "isb    0xF                                             \n"

        "4:                                                     \n"
        /* Restore callee-saved regs from the now-active stack and
         * return into `to`'s execution. */
        "pop    {r4-r11, pc}                                    \n"
    );
}

/* -------------------------------------------------------------------------
 * wt_co_fault_recovery_thunk
 *
 * Invoked by the platform fault handler via a fabricated MSP_S
 * exception frame after EXC_RETURN to Secure Thread MSP. At entry,
 * MSP_S points to the bootstrap context's saved {r4-r11, lr} frame
 * from when bootstrap last context-switched into the now-faulted
 * coroutine. The pop unwinds it and returns into wt_co_arch_switch's
 * caller (do_switch in coroutine.c), which proceeds as if the
 * coroutine had voluntarily switched back. The scheduler then sees
 * the FAULTED state and continues with the next runnable coroutine.
 * ---------------------------------------------------------------------- */

__attribute__((naked, noreturn))
void wt_co_fault_recovery_thunk(void)
{
    __asm__ volatile (
        "pop    {r4-r11, pc}                                    \n"
    );
}
