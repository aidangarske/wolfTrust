#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SUBTREE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE="${ZEPHYR_WORKSPACE:-$SUBTREE_DIR/.workspace/zephyrproject}"
WEST_BIN="${WEST_BIN:-$SUBTREE_DIR/.venv/bin/west}"
ZEPHYR_TAG="${ZEPHYR_TAG:-v4.2.0}"
ZEPHYR_REPO="${ZEPHYR_REPO:-https://github.com/zephyrproject-rtos/zephyr.git}"
WEST_PROJECTS="${WEST_PROJECTS:-cmsis cmsis_6 hal_stm32}"

mkdir -p "$WORKSPACE"

if [ ! -d "$WORKSPACE/zephyr/.git" ]; then
    git clone --branch "$ZEPHYR_TAG" --depth 1 "$ZEPHYR_REPO" "$WORKSPACE/zephyr"
fi

if [ ! -d "$WORKSPACE/.west" ]; then
    (
        cd "$WORKSPACE"
        "$WEST_BIN" init -l zephyr
    )
fi

(
    cd "$WORKSPACE"
    # Keep the default sync narrow for the STM32H563 guest workflow instead of
    # fetching every vendor HAL from the full Zephyr manifest.
    "$WEST_BIN" update --narrow -o=--depth=1 zephyr $WEST_PROJECTS
)
