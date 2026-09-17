/* wolfpsa_no_trace.h
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

#ifndef WOLFTRUST_WOLFPSA_NO_TRACE_H
#define WOLFTRUST_WOLFPSA_NO_TRACE_H

/*
 * wolfPSA master at the pinned revision does not yet act on
 * WOLFPSA_NO_TRACE. Its host trace helper otherwise pulls getenv(), stderr,
 * and formatted I/O into these freestanding guest images. Skip that helper
 * and compile every trace call away until wolfPSA provides the guard itself.
 */
#if defined(WOLFPSA_NO_TRACE)
    #ifndef WOLFPSA_PSA_TRACE_H
        #define WOLFPSA_PSA_TRACE_H
    #endif
    #define wolfpsa_trace(...) ((void)0)
#endif

#endif /* WOLFTRUST_WOLFPSA_NO_TRACE_H */
