/* mutex.h
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

#ifndef WOLFTRUST_SYNC_MUTEX_H
#define WOLFTRUST_SYNC_MUTEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration — do not include wolftrust/sched/coroutine.h here;
 * the implementation file includes it directly. */
struct wt_co;

/* Sleep mutex for coroutine synchronisation. A coroutine that tries to
 * acquire a held mutex is moved to the mutex's wait queue (via
 * wt_co_block) and is not scheduled until the holder releases.
 * Not safe to use from the bootstrap (monitor) context — those code
 * paths must use plain critical sections instead, since blocking the
 * bootstrap would deadlock the monitor.
 *
 * Treat the fields as opaque. They are exposed here so the struct can
 * be statically allocated by the consumer (e.g. embedded in a
 * whNvmContext via the whLock vtable). */
typedef struct wt_mutex {
    struct wt_co *holder;     /* NULL if not held */
    struct wt_co *wait_head;  /* singly-linked list head, NULL if empty */
    struct wt_co *wait_tail;  /* end-pointer for O(1) enqueue */
    uint32_t      acquire_count; /* statistics; instrumentation only */
    uint32_t      contend_count; /* incremented when a caller had to block */
} wt_mutex_t;

/* Initialise (or re-initialise) a mutex. Safe to call only when no
 * coroutine is holding or waiting on it. Zeroes all fields. */
void wt_mutex_init(wt_mutex_t *m);

/* Acquire `m`. If held by another coroutine, block the caller until
 * released. Returns 0 on success, negative on illegal use (e.g. called
 * from bootstrap context). Recursive acquire by the same coroutine is
 * UNDEFINED — these are not recursive mutexes; do not double-acquire. */
int wt_mutex_acquire(wt_mutex_t *m);

/* Release `m`. Wakes the head of the wait queue (if any). Returns 0 on
 * success, negative if `m` was not held by the caller (a sign of a
 * programming error; do not paper over it). */
int wt_mutex_release(wt_mutex_t *m);

/* Non-blocking try-acquire. Returns true if acquired, false if held
 * (no side effect). */
bool wt_mutex_try_acquire(wt_mutex_t *m);

/* Inspect holder for assertion/debug use. NULL if not held. */
struct wt_co *wt_mutex_holder(const wt_mutex_t *m);

#endif
