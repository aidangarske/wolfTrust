/* test_ring.c
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

#include "test_vnet.h"
#include "wolftrust/vnet/vnet_ring.h"
#include "wolftrust/vnet/vnet_errors.h"

#define RING_N 4

static vnet_rx_desc_t g_buf[RING_N];

static vnet_rx_desc_t mk(uint16_t slot, uint16_t gen, uint16_t len)
{
    vnet_rx_desc_t d;
    d.slot = slot;
    d.gen = gen;
    d.len = len;
    d.flags = 0;
    d.src_vm = 0;
    return d;
}

static int test_ring_empty_and_pop(void)
{
    vnet_ring_t r;
    vnet_rx_desc_t out;
    vnet_ring_init(&r, g_buf, RING_N);
    T_CHECK(vnet_ring_empty(&r));
    T_CHECK(!vnet_ring_full(&r));
    T_EQ_INT(vnet_ring_count(&r), 0);
    T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_E_EMPTY);
    T_EQ_INT(vnet_ring_peek(&r, &out), WT_VNET_E_EMPTY);
    return 0;
}

static int test_ring_push_full(void)
{
    vnet_ring_t r;
    vnet_rx_desc_t d;
    vnet_ring_init(&r, g_buf, RING_N);

    T_EQ_INT(vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=0,.gen=1,.len=10}), WT_VNET_OK);
    T_EQ_INT(vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=1,.gen=1,.len=20}), WT_VNET_OK);
    T_EQ_INT(vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=2,.gen=1,.len=30}), WT_VNET_OK);
    T_EQ_INT(vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=3,.gen=1,.len=40}), WT_VNET_OK);
    T_EQ_INT(vnet_ring_count(&r), RING_N);
    T_CHECK(vnet_ring_full(&r));

    d = mk(99, 99, 99);
    T_EQ_INT(vnet_ring_push(&r, &d), WT_VNET_E_QUEUE_FULL);
    return 0;
}

static int test_ring_fifo_order(void)
{
    vnet_ring_t r;
    vnet_rx_desc_t out;
    vnet_ring_init(&r, g_buf, RING_N);

    vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=10,.gen=1,.len=100});
    vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=20,.gen=2,.len=200});
    vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=30,.gen=3,.len=300});

    T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_OK);
    T_EQ_INT(out.slot, 10); T_EQ_INT(out.gen, 1); T_EQ_INT(out.len, 100);
    T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_OK);
    T_EQ_INT(out.slot, 20);
    T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_OK);
    T_EQ_INT(out.slot, 30);
    T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_E_EMPTY);
    return 0;
}

static int test_ring_peek_does_not_consume(void)
{
    vnet_ring_t r;
    vnet_rx_desc_t out;
    vnet_ring_init(&r, g_buf, RING_N);

    vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=7,.gen=7,.len=77});
    T_EQ_INT(vnet_ring_count(&r), 1);
    T_EQ_INT(vnet_ring_peek(&r, &out), WT_VNET_OK);
    T_EQ_INT(out.slot, 7);
    T_EQ_INT(vnet_ring_count(&r), 1);
    T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_OK);
    T_EQ_INT(out.slot, 7);
    T_EQ_INT(vnet_ring_count(&r), 0);
    return 0;
}

static int test_ring_wraparound(void)
{
    vnet_ring_t r;
    vnet_rx_desc_t out;
    int i;
    vnet_ring_init(&r, g_buf, RING_N);

    /* Push 4, pop 4, then push 4 more — exercises head/tail wrap. */
    for (i = 0; i < RING_N; ++i) {
        vnet_rx_desc_t d = mk((uint16_t)i, 1, (uint16_t)(i * 10));
        T_EQ_INT(vnet_ring_push(&r, &d), WT_VNET_OK);
    }
    for (i = 0; i < RING_N; ++i) {
        T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_OK);
        T_EQ_INT(out.slot, i);
    }
    for (i = 0; i < RING_N; ++i) {
        vnet_rx_desc_t d = mk((uint16_t)(100 + i), 2, (uint16_t)(i * 5));
        T_EQ_INT(vnet_ring_push(&r, &d), WT_VNET_OK);
    }
    for (i = 0; i < RING_N; ++i) {
        T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_OK);
        T_EQ_INT(out.slot, 100 + i);
        T_EQ_INT(out.gen, 2);
    }
    T_CHECK(vnet_ring_empty(&r));
    return 0;
}

static int test_ring_drop_head(void)
{
    vnet_ring_t r;
    vnet_rx_desc_t out;
    vnet_ring_init(&r, g_buf, RING_N);

    T_EQ_INT(vnet_ring_drop_head(&r), WT_VNET_E_EMPTY);

    vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=1});
    vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=2});
    vnet_ring_push(&r, &(vnet_rx_desc_t){.slot=3});

    T_EQ_INT(vnet_ring_drop_head(&r), WT_VNET_OK);
    T_EQ_INT(vnet_ring_count(&r), 2);
    T_EQ_INT(vnet_ring_peek(&r, &out), WT_VNET_OK);
    T_EQ_INT(out.slot, 2);
    T_EQ_INT(vnet_ring_drop_head(&r), WT_VNET_OK);
    T_EQ_INT(vnet_ring_pop(&r, &out), WT_VNET_OK);
    T_EQ_INT(out.slot, 3);
    T_EQ_INT(vnet_ring_drop_head(&r), WT_VNET_E_EMPTY);
    return 0;
}

int run_ring_tests(void)
{
    int rc = 0;
    rc |= test_ring_empty_and_pop();
    rc |= test_ring_push_full();
    rc |= test_ring_fifo_order();
    rc |= test_ring_peek_does_not_consume();
    rc |= test_ring_wraparound();
    rc |= test_ring_drop_head();
    return rc;
}
