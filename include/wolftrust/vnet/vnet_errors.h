/* vnet_errors.h
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

#ifndef WOLFTRUST_VNET_ERRORS_H
#define WOLFTRUST_VNET_ERRORS_H

#define WT_VNET_OK                 0

#define WT_VNET_E_BADARG          -3000
#define WT_VNET_E_NOTREADY        -3001
#define WT_VNET_E_ACCESS          -3002
#define WT_VNET_E_NOT_OPEN        -3003

#define WT_VNET_E_INVAL_MAC       -3010
#define WT_VNET_E_DUP_MAC         -3011
#define WT_VNET_E_NO_MAC          -3012
#define WT_VNET_E_SPOOF           -3013

#define WT_VNET_E_FRAME_LEN       -3020
#define WT_VNET_E_MALFORMED       -3021

#define WT_VNET_E_POOL_FULL       -3030
#define WT_VNET_E_QUEUE_FULL      -3031
#define WT_VNET_E_EMPTY           -3032
#define WT_VNET_E_STALE_COOKIE    -3033
#define WT_VNET_E_NOT_OWNER       -3034
#define WT_VNET_E_DOUBLE_RELEASE  -3035

#define WT_VNET_E_DROPPED_UNKNOWN -3040
#define WT_VNET_E_DROPPED_POLICY  -3041

#endif
