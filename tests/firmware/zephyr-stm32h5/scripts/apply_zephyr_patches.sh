#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SUBTREE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE="${ZEPHYR_WORKSPACE:-$SUBTREE_DIR/.workspace/zephyrproject}"
ZEPHYR_TREE="$WORKSPACE/zephyr"
PATCH_DIR="$SUBTREE_DIR/patches/zephyr"

if [ ! -d "$ZEPHYR_TREE/.git" ]; then
    echo "missing Zephyr tree: $ZEPHYR_TREE" >&2
    exit 1
fi

for patch in "$PATCH_DIR"/*.patch; do
    if git -C "$ZEPHYR_TREE" apply --check "$patch" >/dev/null 2>&1; then
        git -C "$ZEPHYR_TREE" apply "$patch"
        continue
    fi
    if git -C "$ZEPHYR_TREE" apply --reverse --check "$patch" >/dev/null 2>&1; then
        continue
    fi
    echo "failed to apply patch: $patch" >&2
    exit 1
done
