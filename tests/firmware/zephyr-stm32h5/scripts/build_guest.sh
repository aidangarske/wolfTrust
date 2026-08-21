#!/bin/sh
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 guest0|guest0_psa|guest1" >&2
    exit 2
fi

APP_NAME=$1
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SUBTREE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
ROOT=$(CDPATH= cd -- "$SUBTREE_DIR/../../.." && pwd)
WORKSPACE="${ZEPHYR_WORKSPACE:-$SUBTREE_DIR/.workspace/zephyrproject}"
WEST_BIN="${WEST_BIN:-$SUBTREE_DIR/.venv/bin/west}"
ZEPHYR_BASE="$WORKSPACE/zephyr"
APP_DIR="$SUBTREE_DIR/apps/$APP_NAME"
BUILD_DIR="$SUBTREE_DIR/build/$APP_NAME"
MODULE_DIR="$SUBTREE_DIR/module"
BOARD="${ZEPHYR_BOARD:-nucleo_h563zi/stm32h563xx/ns}"
SECURE_BIN="${SECURE_BIN:-$ROOT/build/wolftrust.bin}"
SECURE_CMSE_IMPLIB="${SECURE_CMSE_IMPLIB:-$ROOT/build/secure_cmse_implib.o}"
WT_ZEPHYR_DTC_OVERLAY_FILE="${WT_ZEPHYR_DTC_OVERLAY_FILE:-}"
WT_EXPECTED_MEASUREMENT_HEX="${WT_EXPECTED_MEASUREMENT_HEX:-}"
WT_EXPECTED_LIFECYCLE="${WT_EXPECTED_LIFECYCLE:-0x3000u}"
WT_ATTESTATION_DEVELOPMENT_PROFILE="${WT_ATTESTATION_DEVELOPMENT_PROFILE:-0}"
WT_M33MU_EXPECT_BKPT="${WT_M33MU_EXPECT_BKPT:-0}"
WT_GUEST_FAULT_PROBE="${WT_GUEST_FAULT_PROBE:-0}"

if [ ! -d "$APP_DIR" ]; then
    echo "unknown guest app: $APP_NAME" >&2
    exit 2
fi

make -C "$ROOT" build/wolftrust.bin build/secure_cmse_implib.o

if [ ! -d "$ZEPHYR_BASE" ]; then
    echo "missing Zephyr workspace: $ZEPHYR_BASE" >&2
    exit 1
fi

if [ ! -f "$SECURE_BIN" ] || [ ! -f "$SECURE_CMSE_IMPLIB" ]; then
    echo "missing secure build artifacts under $ROOT/build" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"

export ZEPHYR_BASE

set -- \
    "-DZEPHYR_EXTRA_MODULES=$MODULE_DIR/wolftrust-tee;$MODULE_DIR/wolfhsm-client;$MODULE_DIR/wolfpsa;$ROOT/lib/wolfSSL" \
    "-DWOLFTRUST_CMSE_IMPLIB=$SECURE_CMSE_IMPLIB" \
    "-DWT_EXPECTED_LIFECYCLE=$WT_EXPECTED_LIFECYCLE" \
    "-DWT_ATTESTATION_DEVELOPMENT_PROFILE=$WT_ATTESTATION_DEVELOPMENT_PROFILE" \
    "-DWT_M33MU_EXPECT_BKPT=$WT_M33MU_EXPECT_BKPT" \
    "-DWT_GUEST_FAULT_PROBE=$WT_GUEST_FAULT_PROBE"

if [ -n "${ZEPHYR_TOOLCHAIN_VARIANT:-}" ]; then
    set -- "$@" "-DZEPHYR_TOOLCHAIN_VARIANT=$ZEPHYR_TOOLCHAIN_VARIANT"
fi
if [ -n "${CROSS_COMPILE:-}" ]; then
    set -- "$@" "-DCROSS_COMPILE=$CROSS_COMPILE"
fi

if [ -n "$WT_ZEPHYR_DTC_OVERLAY_FILE" ]; then
    case "$WT_ZEPHYR_DTC_OVERLAY_FILE" in
        /*) ;;
        *) WT_ZEPHYR_DTC_OVERLAY_FILE="$SUBTREE_DIR/$WT_ZEPHYR_DTC_OVERLAY_FILE" ;;
    esac
    set -- "$@" "-DEXTRA_DTC_OVERLAY_FILE=$WT_ZEPHYR_DTC_OVERLAY_FILE"
fi

if [ -n "$WT_EXPECTED_MEASUREMENT_HEX" ]; then
    set -- "$@" "-DWT_EXPECTED_MEASUREMENT_HEX=$WT_EXPECTED_MEASUREMENT_HEX"
fi

if [ "${WT_RUN_CONFORMANCE:-0}" = "1" ]; then
    # Reclaim the deep attestation stack (skipped in the conformance guest) so
    # the Arm val NSPE framework fits guest0's 32 KiB NS RAM window.
    set -- "$@" "-DWT_RUN_CONFORMANCE=1" "-DCONFIG_MAIN_STACK_SIZE=10240"
fi

if [ -n "${WT_CONF_SUITE:-}" ]; then
    set -- "$@" "-DWT_CONF_SUITE=$WT_CONF_SUITE"
fi

"$WEST_BIN" build -p auto \
    -d "$BUILD_DIR" \
    -b "$BOARD" \
    "$APP_DIR" \
    -- "$@"
