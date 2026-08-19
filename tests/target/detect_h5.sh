#!/usr/bin/env bash
# Can we flash + capture a real STM32H563 here? Exit 0 when an ST-Link probe, the
# STM32CubeProgrammer CLI, and a serial VCP are all present; else non-zero with a
# one-line reason on stdout. Mirrors detect_m33mu.sh's contract so the hardware
# harness (make test-hardware) can SKIP cleanly off the board.
set -u

CLI="${STM32_CLI:-$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI}"
SERIAL="${H5_SERIAL:-/dev/ttyACM0}"

if [ ! -x "$CLI" ]; then
    echo "STM32_Programmer_CLI not found — set STM32_CLI=/path/to/STM32_Programmer_CLI"
    exit 1
fi
if [ ! -e "$SERIAL" ]; then
    echo "serial VCP $SERIAL not present — set H5_SERIAL=/dev/ttyACMx"
    exit 1
fi
if command -v lsusb >/dev/null 2>&1 && ! lsusb 2>/dev/null | grep -qiE '0483:37'; then
    echo "no ST-Link on USB (0483:37xx) — is the board connected?"
    exit 1
fi
exit 0
