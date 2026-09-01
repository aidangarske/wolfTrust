#!/usr/bin/env python3
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfTrust.
#
# wolfTrust is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTrust is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

# Seed corpus for the Non-secure to SPM fuzzer. Each seed exercises one shape
# of the wt_spm_fuzz operation stream so the fuzzer starts from valid coverage
# of connect, call, close, forged handles, and vector edge cases rather than
# discovering the framing from scratch.

import os
import struct

OUT = os.path.join(os.path.dirname(__file__), "corpus")

FUZZ_CONN_SID = 0x2000
FUZZ_STRICT_SID = 0x2001


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def vecs(in_lens, out_lens):
    # Per op, the harness reads PSA_MAX_IOVEC (4) pairs of (in take len byte +
    # in bytes, out len byte). Emit a compact, self-consistent block.
    b = b""
    for i in range(4):
        take = in_lens[i] if i < len(in_lens) else 0
        b += bytes([take]) + (b"\xa5" * take)
        b += bytes([out_lens[i] if i < len(out_lens) else 0])
    return b


def op_connect(sid_selector, version):
    # selector low 3 bits = 0 (connect); bit 4 picks CONN vs STRICT sid.
    sel = 0x00 | (0x10 if sid_selector else 0x00)
    return bytes([sel]) + u32(version)


def op_call(use_held, handle, type_, in_len, out_len, in_lens, out_lens):
    # selector low 3 bits = 1 (call held) or 2 (call forged); bit0 held flag.
    sel = 0x01 if use_held else 0x02
    b = bytes([sel])
    if not use_held:
        b += u32(handle)
    else:
        b += bytes([0])  # index into held handles
    b += u32(type_) + bytes([in_len, out_len])
    b += vecs(in_lens, out_lens)
    return b


def op_close_forged(handle):
    return bytes([0x03]) + u32(handle)


def op_version(sid):
    return bytes([0x04]) + u32(sid)


def seeds():
    # ops-count byte prefixes each stream.
    out = {}

    # 1. Clean connect + one call + close on the connection-based service.
    out["connect_call_close"] = (
        bytes([3])
        + op_connect(True, 2)
        + op_call(True, 0, 0, 1, 1, [3], [4])
        + bytes([0x03, 0])
    )

    # 2. Call on a forged handle with maxed vector counts.
    out["forged_call"] = bytes([1]) + op_call(
        False, 0xDEADBEEF, 0, 6, 6, [32, 32, 32, 32], [64, 64, 64, 64]
    )

    # 3. Negative call type (a programmer error per FF-M).
    out["neg_type"] = bytes([1]) + op_call(
        False, 0x1, 0xFFFFFFFF, 1, 0, [1], [0]
    )

    # 4. Version probes on real and fuzzed sids.
    out["versions"] = (
        bytes([3])
        + op_version(FUZZ_CONN_SID)
        + op_version(FUZZ_STRICT_SID)
        + op_version(0x41414141)
    )

    # 5. Strict-service connect refusal then a forged close.
    out["strict_refuse"] = (
        bytes([2]) + op_connect(False, 5) + op_close_forged(0)
    )

    # 6. A long mixed stream so the fuzzer has a large starting shape.
    body = bytes([20])
    for i in range(20):
        body += op_connect(i % 2 == 0, i)
    out["long_connects"] = body

    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, blob in seeds().items():
        with open(os.path.join(OUT, name + ".bin"), "wb") as f:
            f.write(blob)
    print("wrote {} seeds to {}".format(len(seeds()), OUT))


if __name__ == "__main__":
    main()
