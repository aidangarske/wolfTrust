/* coroutine.c
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

#include "wolftrust/sched/coroutine_internal.h"
#include "wolftrust/platform.h"

#include <stddef.h>
#include <stdint.h>

/* Static coroutine table; slot 0 is reserved for g_co_bootstrap. */
static struct wt_co g_co_table[WT_CO_MAX];

/* Implicit bootstrap (monitor) context — never on the runqueue. */
static struct wt_co g_co_bootstrap;

/* Currently executing coroutine. Points to &g_co_bootstrap while the
 * monitor is running.  Updated by the C scheduler before every switch. */
static struct wt_co *g_co_current;

/* FIFO runqueue of RUNNABLE coroutines. */
static struct wt_co *g_runqueue_head;
static struct wt_co *g_runqueue_tail;

/* Number of coroutine slots consumed (including bootstrap). */
static uint32_t g_co_count;

/* -------------------------------------------------------------------------
 * Runqueue helpers
 * ---------------------------------------------------------------------- */

static void runqueue_enqueue(struct wt_co *co)
{
    co->next_run = (struct wt_co *)0;
    if (g_runqueue_tail != (struct wt_co *)0) {
        g_runqueue_tail->next_run = co;
    } else {
        g_runqueue_head = co;
    }
    g_runqueue_tail = co;
}

static struct wt_co *runqueue_dequeue(void)
{
    struct wt_co *co = g_runqueue_head;
    if (co != (struct wt_co *)0) {
        g_runqueue_head = co->next_run;
        if (g_runqueue_head == (struct wt_co *)0) {
            g_runqueue_tail = (struct wt_co *)0;
        }
        co->next_run = (struct wt_co *)0;
    }
    return co;
}

/* -------------------------------------------------------------------------
 * Canary check helper — only coroutines with a real stack are checked.
 * ---------------------------------------------------------------------- */

static void check_canary(struct wt_co *co)
{
    if (co->stack_base == (uint8_t *)0) {
        return; /* bootstrap has no stack buffer */
    }
    if (*(volatile uint32_t *)(void *)co->stack_base != WT_CO_STACK_CANARY) {
        wt_platform_panic();
    }
}

/* -------------------------------------------------------------------------
 * Switch helper: validate canary, update state bookkeeping, call arch.
 * ---------------------------------------------------------------------- */

static void do_switch(struct wt_co *from, struct wt_co *to)
{
    check_canary(from);
    g_co_current = to;
    to->state = WT_CO_RUNNING;
    wt_co_arch_switch(from, to);
    /* Execution resumes here when `from` is switched back in. */
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void wt_co_init(void)
{
    uint32_t i;

    for (i = 0u; i < WT_CO_MAX; i++) {
        struct wt_co *co = &g_co_table[i];
        co->sp         = 0u;
        co->stack_base = (uint8_t *)0;
        co->stack_size = 0u;
        co->entry      = (wt_co_entry_fn)0;
        co->arg        = (void *)0;
        co->state      = WT_CO_BLOCKED;
        co->id         = 0u;
        co->next_run   = (struct wt_co *)0;
        co->next_wait  = (struct wt_co *)0;
    }

    g_co_bootstrap.sp         = 0u;
    g_co_bootstrap.stack_base = (uint8_t *)0;
    g_co_bootstrap.stack_size = 0u;
    g_co_bootstrap.entry      = (wt_co_entry_fn)0;
    g_co_bootstrap.arg        = (void *)0;
    g_co_bootstrap.state      = WT_CO_RUNNING;
    g_co_bootstrap.id         = 0u;
    g_co_bootstrap.next_run   = (struct wt_co *)0;
    g_co_bootstrap.next_wait  = (struct wt_co *)0;

    g_co_current      = &g_co_bootstrap;
    g_runqueue_head   = (struct wt_co *)0;
    g_runqueue_tail   = (struct wt_co *)0;
    g_co_count        = 1u; /* bootstrap counts as slot 0 */
}

static wt_co_t *wt_co_create_common(uint8_t *stack, size_t stack_size,
                                    wt_co_entry_fn entry, void *arg,
                                    uint8_t initial_state)
{
    struct wt_co *co;
    uint32_t      id;

    /* Validate arguments. */
    if (entry == (wt_co_entry_fn)0) {
        return (wt_co_t *)0;
    }
    if (stack == (uint8_t *)0) {
        return (wt_co_t *)0;
    }
    if (stack_size < WT_CO_STACK_SIZE) {
        return (wt_co_t *)0;
    }
    /* 8-byte alignment check. */
    if (((uintptr_t)stack & 7u) != 0u) {
        return (wt_co_t *)0;
    }
    if (g_co_count >= WT_CO_MAX) {
        return (wt_co_t *)0;
    }

    /* Assign a table slot (0 is taken by bootstrap). */
    id = g_co_count;
    g_co_count++;
    co = &g_co_table[id - 1u]; /* table[0..WT_CO_MAX-1], id starts at 1 */

    co->id         = id;
    co->stack_base = stack;
    co->stack_size = stack_size;
    co->entry      = entry;
    co->arg        = arg;
    co->state      = (wt_co_state_t)initial_state;
    co->next_run   = (struct wt_co *)0;
    co->next_wait  = (struct wt_co *)0;

    /* Plant stack canary at the very bottom of the caller-provided buffer. */
    *(volatile uint32_t *)(void *)stack = WT_CO_STACK_CANARY;

    /* Let the arch code set up the initial register frame on this stack. */
    wt_co_arch_init_stack(co, entry, arg);

    if (initial_state == WT_CO_RUNNABLE) {
        runqueue_enqueue(co);
    }

    return (wt_co_t *)co;
}

wt_co_t *wt_co_create(uint8_t *stack, size_t stack_size,
                       wt_co_entry_fn entry, void *arg)
{
    return wt_co_create_common(stack, stack_size, entry, arg, WT_CO_RUNNABLE);
}

wt_co_t *wt_co_create_blocked(uint8_t *stack, size_t stack_size,
                              wt_co_entry_fn entry, void *arg)
{
    return wt_co_create_common(stack, stack_size, entry, arg, WT_CO_BLOCKED);
}

void wt_co_yield(void)
{
    struct wt_co *from;
    struct wt_co *to;

    from = g_co_current;

    if (from == &g_co_bootstrap) {
        /* Bootstrap yielding: just drain one budget tick. */
        wt_co_tick(1u);
        return;
    }

    /* Enqueue self at the tail and mark RUNNABLE. */
    from->state = WT_CO_RUNNABLE;
    runqueue_enqueue(from);

    /* Pick next runnable, or fall back to bootstrap. */
    to = runqueue_dequeue();
    if (to == (struct wt_co *)0) {
        to = &g_co_bootstrap;
    }

    do_switch(from, to);
}

void wt_co_block(void)
{
    struct wt_co *from;
    struct wt_co *to;

    from = g_co_current;

    if (from == &g_co_bootstrap) {
        /* Blocking the bootstrap would deadlock the monitor. */
        wt_platform_panic();
    }

    from->state = WT_CO_BLOCKED;
    /* Do NOT enqueue — coroutine disappears from the runqueue. */

    to = runqueue_dequeue();
    if (to == (struct wt_co *)0) {
        to = &g_co_bootstrap;
    }

    do_switch(from, to);
}

void wt_co_wake(wt_co_t *co)
{
    if (co == (wt_co_t *)0) {
        return;
    }
    if (co->state == WT_CO_RUNNABLE || co->state == WT_CO_RUNNING) {
        return; /* already active, nothing to do */
    }
    if (co->state == WT_CO_FAULTED) {
        return; /* terminal — never wake again */
    }
    co->state = WT_CO_RUNNABLE;
    runqueue_enqueue(co);
}

void wt_co_mark_faulted(wt_co_t *co)
{
    struct wt_co **link;
    struct wt_co  *node;

    if (co == (wt_co_t *)0) {
        return;
    }

    /* Unlink from the runqueue if present. The faulted coroutine is
     * almost always WT_CO_RUNNING (we caught it mid-execution), so this
     * walk is defensive — but cheap, and keeps the invariant simple. */
    link = &g_runqueue_head;
    while (*link != (struct wt_co *)0) {
        node = *link;
        if (node == co) {
            *link = node->next_run;
            if (g_runqueue_tail == node) {
                /* Walk again to find the new tail. */
                struct wt_co *tail = g_runqueue_head;
                if (tail == (struct wt_co *)0) {
                    g_runqueue_tail = (struct wt_co *)0;
                } else {
                    while (tail->next_run != (struct wt_co *)0) {
                        tail = tail->next_run;
                    }
                    g_runqueue_tail = tail;
                }
            }
            node->next_run = (struct wt_co *)0;
            break;
        }
        link = &(*link)->next_run;
    }

    co->next_wait = (struct wt_co *)0;
    co->state     = WT_CO_FAULTED;

    if (g_co_current == co) {
        g_co_current = &g_co_bootstrap;
    }
}

wt_co_t *wt_co_current(void)
{
    if (g_co_current == &g_co_bootstrap) {
        return (wt_co_t *)0;
    }
    return (wt_co_t *)g_co_current;
}

wt_co_state_t wt_co_state(const wt_co_t *co)
{
    return co->state;
}

uint32_t wt_co_tick(uint32_t budget_iterations)
{
    uint32_t      switches = 0u;
    struct wt_co *to;
    struct wt_co *from;

    if (g_co_current != &g_co_bootstrap) {
        /* Called recursively (e.g. an interrupt handler called us
         * while a coroutine was mid-switch). Treat as a no-op rather
         * than panicking; the in-flight coroutine continues when the
         * interrupting handler returns. */
        return 0u;
    }

    if (budget_iterations == 0u) {
        return 0u;
    }

    from = &g_co_bootstrap;

    while (switches < budget_iterations) {
        to = runqueue_dequeue();
        if (to == (struct wt_co *)0) {
            break; /* runqueue empty */
        }

        do_switch(from, to);
        /* Execution resumes here after `to` yields or blocks back to
         * bootstrap. g_co_current has been restored to &g_co_bootstrap
         * by the time we return (do_switch updated it before switching
         * in, and when bootstrap is switched back in it restores itself). */
        switches++;
    }

    return switches;
}
