/* benchmark_hooks.h
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

#ifndef WOLFTRUST_NS_BENCHMARK_HOOKS_H
#define WOLFTRUST_NS_BENCHMARK_HOOKS_H

#include <stddef.h>

int wt_benchmark_printf(const char *fmt, ...);
int wt_benchmark_snprintf(char *buf, size_t len, const char *fmt, ...);
int wt_benchmark_atoi(const char *s);
double current_time(int reset);

#endif /* WOLFTRUST_NS_BENCHMARK_HOOKS_H */
