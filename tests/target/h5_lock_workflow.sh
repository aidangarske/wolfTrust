#!/usr/bin/env bash
# MP3 STM32H563 reversible lock workflow. Each stage is a separate invocation
# so nothing chains automatically into an irreversible product-state advance.
# Board-writing stages refuse to run without WT_LOCK_CONFIRM=1; the permanent
# Locked state is refused outright on this dev board.
#
# VERIFIED ON THE BOARD (2026-08-18):
# DA provisioning is REJECTED in the Open product state ("Provisioning with
# password is not supported by the current device") for both password and
# certificate configs. That matches the STM32H5 OEM-iRoT sequence: DA keys are
# provisioned in the Provisioning product state, not Open. So the safe order is
#   advance Open->Provisioning (0x17)  [regress-to-Open still available here]
#   provision DA + discover            [prove the credential before Closing]
#   advance Provisioning->Closed (0x72)[GATED: DA regress is the only way back]
#   regress -> Open                    [mass-erase] then reflash + re-test
# Do NOT advance past a state whose regression path is not first proven. Uses
# ST's default password credential (guaranteed OBK/password match) so
# regression can never fail on a mismatch.
#
#   h5_lock_workflow.sh status              read product state + option bytes
#   h5_lock_workflow.sh advance <hexstate>  set product state (GATED, brick risk)
#   h5_lock_workflow.sh provision           write the DA config OBK (Provisioning)
#   h5_lock_workflow.sh discover            prove the DA credential authenticates
#   h5_lock_workflow.sh regress             DA-authenticate + full regression
set -euo pipefail

CP="${STM32_CP:-$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin}"
CLI="${STM32_CLI:-$CP/STM32_Programmer_CLI}"
OBK="${WT_DA_OBK:-$CP/DA_Default_Config/STM32H5/NonCrypto/DA_Config_Password.obk}"
PWD_BIN="${WT_DA_PWD:-$CP/DA_Default_Config/STM32H5/password.bin}"
# RM0481 product-state codes.
PS_OPEN=0xED; PS_PROVISIONING=0x17; PS_TZCLOSED=0xC6; PS_CLOSED=0x72; PS_LOCKED=0x5C

stage="${1:-status}"
strip() { sed -e 's/\x1b\[[0-9;]*[A-Za-z]//g'; }
need_confirm() {
  [ "${WT_LOCK_CONFIRM:-0}" = "1" ] || {
    echo "REFUSED: '$stage' writes to the board. Re-run with WT_LOCK_CONFIRM=1." >&2
    exit 2; }
}

case "$stage" in
  status)
    "$CLI" -c port=SWD mode=HotPlug -psrss displ 2>&1 | strip | grep -iE "product state|0x" | head
    "$CLI" -c port=SWD mode=HotPlug -ob displ 2>&1 | strip | grep -iE "PRODUCT_STATE|TZEN|BOOT_UBE|SECWM" | head
    ;;
  provision)
    need_confirm
    [ -s "$OBK" ] || { echo "FAIL: DA OBK not found: $OBK" >&2; exit 1; }
    echo "Provisioning DA config (reversible while Open): $OBK"
    "$CLI" -c port=SWD mode=HotPlug -sdp "$OBK" 2>&1 | strip
    ;;
  discover)
    # Non-destructive: authenticate-discovery lists granted permissions. Proves
    # the DA credential works BEFORE any state advance — the escape-hatch check.
    echo "DA discovery with $PWD_BIN (no erase, no state change):"
    "$CLI" -c port=SWD mode=HotPlug -pwd path="$PWD_BIN" debugauth=2 2>&1 | strip
    ;;
  advance)
    need_confirm
    state="${2:-}"
    case "$state" in
      "$PS_LOCKED"|0x5c) echo "REFUSED: Locked (0x5C) is permanent — never on a dev board." >&2; exit 2 ;;
      "$PS_PROVISIONING"|"$PS_TZCLOSED"|"$PS_CLOSED"|0x17|0xc6|0x72) ;;
      *) echo "REFUSED: advance needs a known reversible state (Provisioning/TZ-Closed/Closed), got '${state:-none}'." >&2; exit 2 ;;
    esac
    echo "ADVANCING product state to $state — regression via 'regress' is the only way back."
    "$CLI" -c port=SWD mode=HotPlug -psrss "$state" 2>&1 | strip
    ;;
  regress)
    need_confirm
    echo "Full Regression via DA password (mass-erase back to Open):"
    "$CLI" -c port=SWD mode=HotPlug -pwd path="$PWD_BIN" debugauth=1 per=14 2>&1 | strip
    ;;
  *) echo "usage: $0 status|provision|discover|advance <hexstate>|regress" >&2; exit 2 ;;
esac
