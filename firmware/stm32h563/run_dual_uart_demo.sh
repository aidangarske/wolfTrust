#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build"
LOG_FILE=$(mktemp)
EMU_CMD="/home/dan/src/m33mu/build/m33mu"
EMU_TIMEOUT="${EMU_TIMEOUT:-120}"

cleanup() {
    if [ -n "${EMU_PID:-}" ] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [ -n "${UART2_CAT_PID:-}" ]; then
        kill "$UART2_CAT_PID" 2>/dev/null || true
    fi
    if [ -n "${UART3_CAT_PID:-}" ]; then
        kill "$UART3_CAT_PID" 2>/dev/null || true
    fi
    rm -f "$LOG_FILE"
}

trap cleanup EXIT INT TERM

make -C "$SCRIPT_DIR" all >/dev/null

stdbuf -oL -eL "$EMU_CMD" --cpu stm32h563 --expect-bkpt 0x7f --timeout "$EMU_TIMEOUT" --quit-on-faults \
    "$BUILD_DIR/secure.bin" "$BUILD_DIR/guest0.bin:0x2000" "$BUILD_DIR/guest1.bin:0x12000" \
    >"$LOG_FILE" 2>&1 &
EMU_PID=$!

UART2_PTY=""
UART3_PTY=""
TRIES=0
while [ "$TRIES" -lt 100 ]; do
    if [ -z "$UART2_PTY" ]; then
        UART2_PTY=$(sed -n 's/.*\[UART\] 40004400 attached to \(.*\)$/\1/p' "$LOG_FILE" | tail -n 1)
    fi
    if [ -z "$UART3_PTY" ]; then
        UART3_PTY=$(sed -n 's/.*\[UART\] 40004800 attached to \(.*\)$/\1/p' "$LOG_FILE" | tail -n 1)
    fi
    if [ -n "$UART2_PTY" ] && [ -n "$UART3_PTY" ]; then
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        break
    fi
    TRIES=$((TRIES + 1))
    sleep 0.1
done

if [ -n "$UART2_PTY" ]; then
    stdbuf -oL cat "$UART2_PTY" 2>/dev/null | sed 's/^/[USART2] /' &
    UART2_CAT_PID=$!
fi

if [ -n "$UART3_PTY" ]; then
    stdbuf -oL cat "$UART3_PTY" 2>/dev/null | sed 's/^/[USART3] /' &
    UART3_CAT_PID=$!
fi

wait "$EMU_PID"
EMU_STATUS=$?

sed -n '/^\[UART\]/p;/^\[BKPT\]/p;/^\[EXPECT BKPT\]/p;/^Execution stopped/p' "$LOG_FILE"

exit "$EMU_STATUS"
