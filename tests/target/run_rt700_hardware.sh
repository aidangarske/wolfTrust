#!/usr/bin/env bash
# MIMXRT700-EVK hardware runner (sibling of run_h5_hardware.sh and
# run_m33mu_scenario.sh; one CLI shape, assertions via lib/expect.sh once it
# lands on main). Runs on the host that owns the probe (pi5).
#
#   run_rt700_hardware.sh <scenario>
#
# Scenarios:
#   romsmoke  the BootROM boots our own XIP image from XSPI0: build
#             tests/firmware/mimxrt700-smoke, wrap it (EVK FCB + plain MBI at
#             flash+0x4000), flash with pyOCD, hard-reset through the pi4 line,
#             then assert the SRAM marker and a moving counter over SWD.
set -euo pipefail

scenario="${1:-}"
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="${RT700_WORK:-$repo/build/rt700}"
target="${RT700_TARGET:-mimxrt798sgfob}"
fcb="${RT700_FCB:-$HOME/rt700-boot/fcb.bin}"
xspi0_base=0x28000000
mbi_offset=0x4000

log() { printf '%s\n' "$*"; }
fail() { log "FAIL: $*"; exit 1; }
check() {
    if [ "$1" -eq 0 ]; then log "  [check] PASS  $2"; else log "  [check] FAIL  $2"; fail "$2"; fi
}

dap() {
    timeout 60 pyocd cmd -t "$target" "$@" 2>&1 |
        grep -viE "rom table|APB-AP|coresight|cidr"
}

wrap_xip() {
    local app="$1" out="$2" exec_addr="$3"
    [ -s "$fcb" ] || fail "FCB binary missing at $fcb (RT700_FCB)"
    cat > "$work/mbi.yaml" <<YAML
family: mimxrt798s
revision: latest
outputImageExecutionTarget: xip
outputImageAuthenticationType: plain
masterBootOutputFile: $work/mbi.bin
inputImageFile: $app
outputImageExecutionAddress: $exec_addr
imageVersion: 0
YAML
    nxpimage mbi export -c "$work/mbi.yaml" >/dev/null
    cat > "$work/bootimg.yaml" <<YAML
family: mimxrt798s
revision: latest
memory_type: xspi_nor
output: $out
output_format: bin
init_offset: 0
fcb: $fcb
mbi: $work/mbi.bin
YAML
    nxpimage bootable-image export -c "$work/bootimg.yaml" >/dev/null
}

flash_xspi0() {
    timeout 300 pyocd flash -t "$target" -a "$xspi0_base" -e sector "$1" 2>&1 |
        grep -E "Erased|programmed|rror" || true
}

reset_board() {
    "$here/lib/rt700_reset.sh" reset
}

case "$scenario" in
romsmoke)
    mkdir -p "$work"
    make -s -C "$repo/tests/firmware/mimxrt700-smoke" BUILD="$work/smoke" all
    wrap_xip "$work/smoke/smoke.bin" "$work/flash_smoke.bin" \
        "$(printf '0x%08x' $((xspi0_base + mbi_offset)))"
    flash_xspi0 "$work/flash_smoke.bin"
    reset_board
    s1="$(dap -c 'read32 0x20180000 8' | tail -1)"
    sleep 1
    s2="$(dap -c 'read32 0x20180000 8' | tail -1)"
    m1="$(printf '%s' "$s1" | awk '{print $2}')"
    c1="$(printf '%s' "$s1" | awk '{print $3}')"
    c2="$(printf '%s' "$s2" | awk '{print $3}')"
    check "$([ "$m1" = "52543030" ]; echo $?)" "ROM booted the XIP image: marker RT00 at 0x20180000 ($m1)"
    check "$([ "$c1" != "$c2" ]; echo $?)" "smoke loop alive: counter $c1 -> $c2"
    pc="$(dap -c halt -c 'reg pc' -c go | sed -n 's/^pc = //p')"
    check "$(case "$pc" in 0x2800[4-9]*|0x2800[a-f]*) echo 0;; *) echo 1;; esac)" "PC inside the XIP image ($pc)"
    log "PASS: hardware/romsmoke"
    ;;
*)
    log "usage: $0 romsmoke"
    exit 2
    ;;
esac
