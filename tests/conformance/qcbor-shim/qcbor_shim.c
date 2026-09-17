/* qcbor_shim.c
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
 * QCBOR API implemented over wolfCOSE's CBOR. Decode uses wc_CBOR_DecodeHead
 * (the wolfCOSE head parser) and layers QCBOR's flat depth-first item
 * iteration (labels + nesting) on top. Encode uses wolfCOSE's element encoders
 * with a deferred array-count backpatch (QCBOR opens an array before the count
 * is known). Scope is exactly what test_a001's val/PAL exercise.
 */

#include "qcbor.h"

#include <wolfcose/wolfcose.h>

/* --- decode --- */

/* Decode one CBOR item head at me->idx via wolfCOSE. For bstr/tstr this also
 * fills head->data/head->dataLen and advances me->idx past the content; for
 * ints/arrays/maps/tags it advances past the head only. Returns 0 on success. */
static int wt_read_head(QCBORDecodeContext* me, WOLFCOSE_CBOR_ITEM* head)
{
    WOLFCOSE_CBOR_CTX wc;
    int rc;

    (void)memset(&wc, 0, sizeof(wc));
    wc.cbuf = me->buf;
    wc.bufSz = me->len;
    wc.idx = me->idx;
    rc = wc_CBOR_DecodeHead(&wc, head);
    me->idx = wc.idx;
    return (rc == WOLFCOSE_SUCCESS) ? 0 : -1;
}

void QCBORDecode_Init(QCBORDecodeContext* me, UsefulBufC buf, int mode)
{
    (void)mode;
    (void)memset(me, 0, sizeof(*me));
    me->buf = (const uint8_t*)buf.ptr;
    me->len = buf.len;
    me->idx = 0;
    me->depth = 0;
    me->err = QCBOR_SUCCESS;
}

QCBORError QCBORDecode_GetNext(QCBORDecodeContext* me, QCBORItem* item)
{
    WOLFCOSE_CBOR_ITEM head;
    int inMap;

    (void)memset(item, 0, sizeof(*item));
    item->uDataType = QCBOR_TYPE_NONE;

    if (me->err != QCBOR_SUCCESS) {
        return me->err;
    }

    /* Leave any fully-consumed containers. */
    while ((me->depth > 0) && (me->nest[me->depth - 1].remaining == 0u)) {
        me->depth--;
    }
    if (me->idx >= me->len) {
        me->err = QCBOR_ERR_HIT_END;
        return me->err;
    }

    inMap = (me->depth > 0) && (me->nest[me->depth - 1].isMap != 0u);

    /* Map entries carry a label (the key) before the value. */
    if (inMap) {
        if (wt_read_head(me, &head) != 0) {
            me->err = QCBOR_ERR_HIT_END;
            return me->err;
        }
        if (head.majorType == 0u) {
            item->uLabelType = QCBOR_TYPE_INT64;
            item->label.int64 = (int64_t)head.val;
        }
        else if (head.majorType == 1u) {
            item->uLabelType = QCBOR_TYPE_INT64;
            item->label.int64 = -1 - (int64_t)head.val;
        }
        else if (head.majorType == 3u) {
            item->uLabelType = QCBOR_TYPE_TEXT_STRING;  /* key consumed already */
        }
        else {
            item->uLabelType = QCBOR_TYPE_NONE;
        }
    }

    /* Absorb any tags in front of the value (COSE tag 18 wraps the array). */
    for (;;) {
        if (wt_read_head(me, &head) != 0) {
            me->err = QCBOR_ERR_HIT_END;
            return me->err;
        }
        if (head.majorType == 6u) {
            if (item->uNumTags < WT_QCBOR_MAX_TAGS) {
                item->uTags[item->uNumTags] = head.val;
                item->uNumTags++;
            }
            continue;
        }
        break;
    }

    /* The value. */
    switch (head.majorType) {
        case 0u:
            item->uDataType = QCBOR_TYPE_INT64;
            item->val.int64 = (int64_t)head.val;
            break;
        case 1u:
            item->uDataType = QCBOR_TYPE_INT64;
            item->val.int64 = -1 - (int64_t)head.val;
            break;
        case 2u:
        case 3u:
            /* wolfCOSE already advanced me->idx past the content. */
            item->uDataType = (head.majorType == 2u) ? QCBOR_TYPE_BYTE_STRING
                                                     : QCBOR_TYPE_TEXT_STRING;
            item->val.string.ptr = head.data;
            item->val.string.len = head.dataLen;
            break;
        case 4u:
            item->uDataType = QCBOR_TYPE_ARRAY;
            item->val.uCount = (uint16_t)head.val;
            break;
        case 5u:
            item->uDataType = QCBOR_TYPE_MAP;
            item->val.uCount = (uint16_t)head.val;
            break;
        default:
            me->err = QCBOR_ERR_HIT_END;
            return me->err;
    }

    /* This item fills one slot of the container it lives in. */
    if ((me->depth > 0) && (me->nest[me->depth - 1].remaining > 0u)) {
        me->nest[me->depth - 1].remaining--;
    }

    /* Descend into a non-empty array/map so following calls walk its members. */
    if (((item->uDataType == QCBOR_TYPE_ARRAY) ||
         (item->uDataType == QCBOR_TYPE_MAP)) && (item->val.uCount > 0u)) {
        if (me->depth < WT_QCBOR_MAX_NESTING) {
            me->nest[me->depth].isMap =
                (item->uDataType == QCBOR_TYPE_MAP) ? 1u : 0u;
            me->nest[me->depth].remaining = item->val.uCount;
            me->depth++;
        }
    }

    return QCBOR_SUCCESS;
}

QCBORError QCBORDecode_Finish(QCBORDecodeContext* me)
{
    if (me->err != QCBOR_SUCCESS) {
        return me->err;
    }
    return QCBOR_SUCCESS;
}

int QCBORDecode_IsTagged(QCBORDecodeContext* me, const QCBORItem* item,
                         uint64_t tag)
{
    int i;

    (void)me;
    for (i = 0; i < (int)item->uNumTags; i++) {
        if (item->uTags[i] == tag) {
            return 1;
        }
    }
    return 0;
}

/* --- encode --- */

static void wt_write_bstr_head(QCBOREncodeContext* me, size_t len)
{
    if (len < 24u) {
        if ((me->idx + 1u) > me->cap) { me->err = 1; return; }
        me->buf[me->idx++] = (uint8_t)(0x40u | (uint8_t)len);
    }
    else if (len < 256u) {
        if ((me->idx + 2u) > me->cap) { me->err = 1; return; }
        me->buf[me->idx++] = 0x58u;
        me->buf[me->idx++] = (uint8_t)len;
    }
    else if (len < 65536u) {
        if ((me->idx + 3u) > me->cap) { me->err = 1; return; }
        me->buf[me->idx++] = 0x59u;
        me->buf[me->idx++] = (uint8_t)(len >> 8);
        me->buf[me->idx++] = (uint8_t)len;
    }
    else {
        me->err = 1;
    }
}

void QCBOREncode_Init(QCBOREncodeContext* me, UsefulBuf buf)
{
    (void)memset(me, 0, sizeof(*me));
    me->buf = (uint8_t*)buf.ptr;
    me->cap = buf.len;
    me->idx = 0;
    me->arrDepth = 0;
    me->err = QCBOR_SUCCESS;
}

void QCBOREncode_OpenArray(QCBOREncodeContext* me)
{
    if (me->err != QCBOR_SUCCESS) {
        return;
    }
    if ((me->idx >= me->cap) || (me->arrDepth >= WT_QCBOR_MAX_NESTING)) {
        me->err = 1;
        return;
    }
    if (me->arrDepth > 0) {
        me->arrCount[me->arrDepth - 1]++;
    }
    me->arrHeadPos[me->arrDepth] = me->idx;
    me->arrCount[me->arrDepth] = 0u;
    me->arrDepth++;
    me->buf[me->idx++] = 0x80u;                 /* count backpatched on close */
}

void QCBOREncode_CloseArray(QCBOREncodeContext* me)
{
    uint32_t count;
    size_t pos;

    if (me->err != QCBOR_SUCCESS) {
        return;
    }
    if (me->arrDepth == 0) {
        me->err = 1;
        return;
    }
    me->arrDepth--;
    count = me->arrCount[me->arrDepth];
    pos = me->arrHeadPos[me->arrDepth];
    if (count < 24u) {
        me->buf[pos] = (uint8_t)(0x80u | (uint8_t)count);
    }
    else {
        me->err = 1;                            /* COSE arrays are small */
    }
}

void QCBOREncode_AddSZString(QCBOREncodeContext* me, const char* str)
{
    WOLFCOSE_CBOR_CTX wc;

    if (me->err != QCBOR_SUCCESS) {
        return;
    }
    (void)memset(&wc, 0, sizeof(wc));
    wc.buf = me->buf;
    wc.bufSz = me->cap;
    wc.idx = me->idx;
    if (wc_CBOR_EncodeTstr(&wc, (const uint8_t*)str, strlen(str)) !=
            WOLFCOSE_SUCCESS) {
        me->err = 1;
        return;
    }
    me->idx = wc.idx;
    if (me->arrDepth > 0) {
        me->arrCount[me->arrDepth - 1]++;
    }
}

void QCBOREncode_AddBytes(QCBOREncodeContext* me, UsefulBufC bytes)
{
    WOLFCOSE_CBOR_CTX wc;

    if (me->err != QCBOR_SUCCESS) {
        return;
    }
    (void)memset(&wc, 0, sizeof(wc));
    wc.buf = me->buf;
    wc.bufSz = me->cap;
    wc.idx = me->idx;
    if (wc_CBOR_EncodeBstr(&wc, (const uint8_t*)bytes.ptr, bytes.len) !=
            WOLFCOSE_SUCCESS) {
        me->err = 1;
        return;
    }
    me->idx = wc.idx;
    if (me->arrDepth > 0) {
        me->arrCount[me->arrDepth - 1]++;
    }
}

void QCBOREncode_AddBytesLenOnly(QCBOREncodeContext* me, UsefulBufC bytes)
{
    if (me->err != QCBOR_SUCCESS) {
        return;
    }
    wt_write_bstr_head(me, bytes.len);          /* head only, no content */
    if (me->arrDepth > 0) {
        me->arrCount[me->arrDepth - 1]++;
    }
}

QCBORError QCBOREncode_Finish(QCBOREncodeContext* me, UsefulBufC* out)
{
    if (me->err != QCBOR_SUCCESS) {
        return me->err;
    }
    if (me->arrDepth != 0) {
        return 1;
    }
    out->ptr = me->buf;
    out->len = me->idx;
    return QCBOR_SUCCESS;
}
