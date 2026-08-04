#!/usr/bin/env python3
# read_wolfboot_measurement.py
#
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

import argparse
import struct
from pathlib import Path


WOLFBOOT_MAGIC = 0x464C4F57
HEADER_OFFSET = 8
HEADER_SHA256 = 0x03


def read_measurement(image_path: Path, header_size: int) -> bytes:
    image = image_path.read_bytes()
    if len(image) < header_size or header_size < HEADER_OFFSET:
        raise ValueError("wolfBoot image header is truncated")
    magic, _ = struct.unpack_from("<II", image, 0)
    if magic != WOLFBOOT_MAGIC:
        raise ValueError("wolfBoot image magic is invalid")

    offset = HEADER_OFFSET
    while offset + 4 <= header_size:
        if image[offset] == 0xFF or (offset & 1) != 0:
            offset += 1
            continue
        tag, length = struct.unpack_from("<HH", image, offset)
        offset += 4
        if tag == 0:
            break
        if length > header_size - offset:
            raise ValueError("wolfBoot image TLV exceeds the header")
        if tag == HEADER_SHA256:
            if length != 32:
                raise ValueError("wolfBoot SHA-256 measurement has wrong size")
            return image[offset:offset + length]
        offset += length

    raise ValueError("wolfBoot SHA-256 measurement was not found")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--header-size", type=lambda value: int(value, 0),
                        default=0x400)
    args = parser.parse_args()
    print(read_measurement(args.image, args.header_size).hex())


if __name__ == "__main__":
    main()
