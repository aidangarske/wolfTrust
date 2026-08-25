/* qcbor.h
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Minimal QCBOR API implemented over wolfCOSE's CBOR primitives. This lets
 * ARM's unmodified val_attestation.c / pal_attestation_crypto.c reach wolfCOSE
 * as the CBOR backend instead of vendoring the external QCBOR library. Only the
 * surface those two files use for test_a001 is provided; wolfCOSE does the
 * actual CBOR head parsing and element encoding (see qcbor_shim.c).
 */

#ifndef WOLFTRUST_QCBOR_SHIM_H
#define WOLFTRUST_QCBOR_SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* --- UsefulBuf --- */
struct q_useful_buf_c { const void* ptr; size_t len; };
struct q_useful_buf   { void*       ptr; size_t len; };
typedef struct q_useful_buf_c UsefulBufC;
typedef struct q_useful_buf   UsefulBuf;

#define NULLUsefulBufC    ((UsefulBufC){ NULL, 0 })
#define NULL_USEFUL_BUF_C ((UsefulBufC){ NULL, 0 })

#define UsefulBuf_MAKE_STACK_UB(name, size) \
    uint8_t name##__stack_ub[(size)]; \
    UsefulBuf name = { name##__stack_ub, (size) }

/* 0 when equal, non-zero otherwise (the only property the harness relies on). */
static inline int UsefulBuf_Compare(UsefulBufC a, UsefulBufC b)
{
    if (a.len != b.len) {
        return (a.len < b.len) ? -1 : 1;
    }
    if (a.len == 0) {
        return 0;
    }
    return memcmp(a.ptr, b.ptr, a.len);
}

static inline UsefulBufC UsefulBuf_Head(UsefulBufC b, size_t amount)
{
    UsefulBufC r;
    r.ptr = b.ptr;
    r.len = (amount <= b.len) ? amount : b.len;
    return r;
}

/* --- QCBOR item types (values are private; only used self-consistently) --- */
#define QCBOR_TYPE_NONE         0
#define QCBOR_TYPE_INT64        2
#define QCBOR_TYPE_ARRAY        4
#define QCBOR_TYPE_MAP          5
#define QCBOR_TYPE_BYTE_STRING  6
#define QCBOR_TYPE_TEXT_STRING  7

typedef int QCBORError;
#define QCBOR_SUCCESS            0
#define QCBOR_ERR_HIT_END        1
#define QCBOR_ERR_NO_MORE_ITEMS  2

#define QCBOR_DECODE_MODE_NORMAL 0

#ifndef CBOR_TAG_COSE_SIGN1
#define CBOR_TAG_COSE_SIGN1 18
#endif

#define WT_QCBOR_MAX_NESTING 12
#define WT_QCBOR_MAX_TAGS    4

typedef struct QCBORItem {
    uint8_t  uDataType;
    uint8_t  uLabelType;
    union {
        int64_t    int64;
        UsefulBufC string;
        uint16_t   uCount;
    } val;
    union {
        int64_t int64;
    } label;
    uint64_t uTags[WT_QCBOR_MAX_TAGS];
    uint8_t  uNumTags;
} QCBORItem;

typedef struct QCBORDecodeContext {
    const uint8_t* buf;
    size_t         len;
    size_t         idx;
    struct {
        uint8_t  isMap;
        uint32_t remaining;
    } nest[WT_QCBOR_MAX_NESTING];
    int            depth;
    QCBORError     err;
} QCBORDecodeContext;

typedef struct QCBOREncodeContext {
    uint8_t*  buf;
    size_t    cap;
    size_t    idx;
    size_t    arrHeadPos[WT_QCBOR_MAX_NESTING];
    uint32_t  arrCount[WT_QCBOR_MAX_NESTING];
    int       arrDepth;
    QCBORError err;
} QCBOREncodeContext;

void       QCBORDecode_Init(QCBORDecodeContext* me, UsefulBufC buf, int mode);
QCBORError QCBORDecode_GetNext(QCBORDecodeContext* me, QCBORItem* item);
QCBORError QCBORDecode_Finish(QCBORDecodeContext* me);
int        QCBORDecode_IsTagged(QCBORDecodeContext* me, const QCBORItem* item,
                                uint64_t tag);

void       QCBOREncode_Init(QCBOREncodeContext* me, UsefulBuf buf);
void       QCBOREncode_OpenArray(QCBOREncodeContext* me);
void       QCBOREncode_CloseArray(QCBOREncodeContext* me);
void       QCBOREncode_AddBytes(QCBOREncodeContext* me, UsefulBufC bytes);
void       QCBOREncode_AddBytesLenOnly(QCBOREncodeContext* me, UsefulBufC bytes);
void       QCBOREncode_AddSZString(QCBOREncodeContext* me, const char* str);
QCBORError QCBOREncode_Finish(QCBOREncodeContext* me, UsefulBufC* out);

#endif /* WOLFTRUST_QCBOR_SHIM_H */
