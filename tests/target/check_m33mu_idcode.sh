#!/bin/sh
# Refuse an M33MU binary without the STM32H5 DBGMCU_IDCODE model from
# m33mu-tb-sec-chain.patch: the Secure image halts on its zero IDCODE.
emu=$(command -v "${1:-m33mu}" 2>/dev/null) || {
    echo "FAIL: M33MU emulator '${1:-m33mu}' not found" >&2
    exit 1
}
if ! grep -aq M33MU_STM32H5_IDCODE "$emu"; then
    echo "FAIL: $emu lacks the STM32H5 DBGMCU_IDCODE model; rebuild it with tests/target/m33mu-tb-sec-chain.patch" >&2
    exit 1
fi
