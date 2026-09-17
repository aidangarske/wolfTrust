#!/usr/bin/env bash
# MP3 lock-model preflight for the STM32H563 board: READ-ONLY. Dumps the
# option-byte state, asserts the expected pre-lock baseline (Open product
# state, TZEN on, OEM-iRoT boot, SECWM over the secure region), and reports
# whether the Debug-Authentication tooling needed for a REVERSIBLE lock is
# present. Writes nothing — the provisioning/transition steps live in the lock
# workflow and each requires an explicit operator go (DA credential first).
set -euo pipefail

CLI="${STM32_CLI:-$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI}"

pass() { printf '  [check] PASS  %s\n' "$1"; }
fail() { printf '  [check] FAIL  %s  (%s)\n' "$1" "$2"; rc=1; }
rc=0

[ -x "$CLI" ] || { echo "SKIP: STM32_Programmer_CLI not found"; exit 0; }

ob=$("$CLI" -c port=SWD mode=HotPlug -ob displ 2>&1 | sed -e 's/\x1b\[[0-9;]*[A-Za-z]//g')

# Field extractor: first "NAME : 0x.." occurrence.
field() { printf '%s\n' "$ob" | grep -oiE "$1[[:space:]]*:[[:space:]]*0x[0-9A-Fa-f]+" | head -1 | grep -oE "0x[0-9A-Fa-f]+"; }

product_state=$(field "PRODUCT_STATE") || true
tzen=$(field "TZEN") || true
boot_ube=$(field "BOOT_UBE") || true
secboot=$(field "SECBOOTADD0?") || true

echo "PRODUCT_STATE=$product_state TZEN=$tzen BOOT_UBE=$boot_ube SECBOOTADD=$secboot"

# RM0481 product-state codes: Open=0xED Provisioning=0x17 iRoT-Prov=0x2E
# TZ-Closed=0xC6 Closed=0x72 Locked=0x5C.
case "${product_state:-}" in
  0xED|0xed) pass "product state Open (0xED) — reflashable baseline" ;;
  0x5C|0x5c) fail "product state" "LOCKED (0x5C) — permanent, no regression possible" ;;
  "") fail "product state" "could not read PRODUCT_STATE" ;;
  *) fail "product state" "unexpected $product_state — regress before lock work" ;;
esac
case "${tzen:-}" in
  0xB4|0xb4) pass "TrustZone enabled (TZEN=0xB4)" ;;
  *) fail "TZEN" "expected 0xB4, got ${tzen:-none}" ;;
esac
case "${boot_ube:-}" in
  0xB4|0xb4) pass "boot path OEM-iRoT flash (BOOT_UBE=0xB4)" ;;
  *) fail "BOOT_UBE" "expected 0xB4 (OEM-iRoT), got ${boot_ube:-none}" ;;
esac

printf '%s\n' "$ob" | grep -qiE "SECWM" && pass "SECWM watermarks present (see dump)" \
  || fail "SECWM" "no SECWM fields in option-byte dump"

# Debug-Authentication tooling: needed to PROVE regression before any
# product-state advance. TrustedPackageCreator builds the DA .obk blobs.
tpc="$(dirname "$CLI")/STM32TrustedPackageCreator_CLI"
if [ -x "$tpc" ]; then pass "STM32TrustedPackageCreator_CLI present (DA provisioning)"; \
else fail "DA tooling" "STM32TrustedPackageCreator_CLI not found next to CubeProgrammer"; fi
"$CLI" -c port=SWD mode=HotPlug -h 2>&1 | grep -qi "debugauth\|-da\b" \
  && pass "CubeProgrammer supports debug-auth commands" \
  || printf '  [info] CubeProgrammer -da support not confirmed from help text\n'

echo "----- full option-byte dump -----"
printf '%s\n' "$ob"
[ "$rc" -eq 0 ] && echo "PASS: lock preflight (board at reversible baseline)" \
  || { echo "FAIL: lock preflight"; exit 1; }
