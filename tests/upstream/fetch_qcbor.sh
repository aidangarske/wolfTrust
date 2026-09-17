#!/bin/sh
# fetch_qcbor.sh
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

# Fetch the reference QCBOR library for the attestation CBOR interop test only.
# wolfTrust itself never depends on QCBOR (it uses wolfCOSE); this is used to
# prove a wolfCOSE-produced COSE_Sign1 token also parses under upstream QCBOR.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
revision=$(sed -n '1p' "$script_dir/qcbor.rev")
destination=${1:-"$script_dir/../../build/upstream/qcbor"}
repository=https://github.com/laurencelundblade/QCBOR.git

case "$revision" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) ;;
    *)
        echo "invalid QCBOR revision" >&2
        exit 1
        ;;
esac

if [ ! -d "$destination/.git" ]; then
    git clone --no-checkout "$repository" "$destination"
fi
git -C "$destination" fetch --depth 1 origin "$revision"
git -C "$destination" checkout --detach "$revision"
actual=$(git -C "$destination" rev-parse HEAD)
if [ "$actual" != "$revision" ]; then
    echo "QCBOR revision mismatch" >&2
    exit 1
fi
printf '%s\n' "$destination"
