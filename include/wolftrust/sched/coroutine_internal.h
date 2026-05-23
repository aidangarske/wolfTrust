/* coroutine_internal.h
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

#ifndef WOLFTRUST_SCHED_COROUTINE_INTERNAL_H
#define WOLFTRUST_SCHED_COROUTINE_INTERNAL_H

#include "wolftrust/sched/coroutine.h"

#define WT_CO_MAX 8u

/* Magic sentinel placed at the bottom of the stack to detect overflow.
 * Checked at every switch; mismatch causes wt_platform_panic(). */
#define WT_CO_STACK_CANARY 0xC0C0DEADu

struct wt_co {
    /* Saved MSP for this coroutine. Set to top-of-stack on creation,
     * updated on every switch-out, consumed on switch-in. Architecture-
     * specific code (coroutine_armv8m.c) is the ONLY writer once the
     * coroutine has started running. */
    uintptr_t sp;

    /* Stack region the caller gave us. base[0] must hold STACK_CANARY. */
    uint8_t *stack_base;
    size_t   stack_size;

    /* Entry function + arg (cleared once the coroutine has actually
     * started executing — useful for debug). */
    wt_co_entry_fn entry;
    void          *arg;

    wt_co_state_t state;
    uint32_t      id;          /* 0 = bootstrap, 1..N = created order */

    /* Linked-list link for the runqueue (RUNNABLE coroutines, FIFO). */
    struct wt_co *next_run;

    /* Linked-list link for wait queues (used by wt_mutex_t and friends).
     * Owned by whichever wait queue currently holds this coroutine.
     * NULL when not waiting. */
    struct wt_co *next_wait;
};

/* Architecture hook. Saves callee-saved regs + LR on `from`'s stack,
 * stores resulting SP into from->sp, loads to->sp into MSP, restores
 * callee-saved regs + LR, returns into `to`'s execution. Must be a
 * naked function. If `from` is NULL (only on the very first dispatch),
 * the current MSP is not saved. */
void wt_co_arch_switch(struct wt_co *from, struct wt_co *to);

/* Architecture hook. Initialise `co`'s stack so that a subsequent
 * wt_co_arch_switch(NULL, co) (or any other from->co) returns into
 * `entry(arg)`. Sets co->sp to the prepared top-of-stack. Coroutine
 * must never return from `entry`; if it does, wt_platform_panic is
 * invoked via a trampoline frame the arch code installs at the
 * bottom of the saved frame. */
void wt_co_arch_init_stack(struct wt_co *co, wt_co_entry_fn entry, void *arg);

#endif
