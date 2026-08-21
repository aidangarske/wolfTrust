#!/usr/bin/env bash
# wolfTrust STM32H563/H5 hardware smoke: build the authenticated
# wolfBoot -> wolfTrust -> guests chain, flash it to a real Nucleo-H563ZI via
# STM32CubeProgrammer, capture the board UART, and assert the production
# lifecycle markers. REAL hardware evidence, distinct from the M33MU emulator.
#
#   run_h5_hardware.sh build [scenario]  build images (needs container toolchain)
#   run_h5_hardware.sh flash [scenario]  flash + capture + assert (needs the host
#                                        that owns the ST-Link + serial VCP)
#   run_h5_hardware.sh all   [scenario]  both, if one env has toolchain AND board
#
# scenario (default positive) selects the on-silicon evidence, mirroring the
# M33MU run_m33mu_scenario.sh set so hardware and emulator prove the same thing:
#   positive     full PSA/FF-M lifecycle green, no faults
#   restart      guest faults on boot; the monitor restarts it restart_limit
#                times then leaves it FAULTED — the system keeps running
#   crossdomain  a probe inside the crypto SP reads SPM-private RAM; the SP
#                domain denies it (fault captured), the SP is quarantined, and
#                the rest of the system survives (L3 isolation on silicon)
#   confboot     the unmodified Arm val NSPE FF-M IPC suite (85/4) against the
#                production SPM; panic tests reboot the chain via real
#                SYSRESETREQ and val resumes off its flash boot flag.
#                Requires SECWM1 to cover the whole boot partition (0x00-0x4F).
#
# The build and flash steps run in different environments (container vs host)
# because the box's ARM toolchain lives only in the CI container while the
# ST-Link + /dev/ttyACM0 belong to the host — so the normal flow is
# `build` in the container, then `flash` on the host, sharing the mounted repo.
#
# Hardware variants drop the emulator-only WT_M33MU_EXPECT_BKPT (a bare-metal
# BKPT with no debugger would fault). The board must be TrustZone-provisioned
# already (TZEN on, SECBOOTADD=0x0C000000, SECWM over the secure region).
# Flashing is reversible; option bytes are not touched here.
set -euo pipefail
set -o pipefail

mode="${1:-all}"
scenario="${2:-positive}"
case "$mode" in build|flash|all) ;; *) echo "usage: $0 build|flash|all [scenario]" >&2; exit 2 ;; esac
case "$scenario" in positive|restart|crossdomain|confboot) ;; *) echo "usage: $0 $mode positive|restart|crossdomain|confboot" >&2; exit 2 ;; esac

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo"

# Manifest guest restart_limit (port/stm32h563/manifest.json domain id 1).
RESTART_LIMIT=3
# Secure ELF that matches the flashed image (for cross-domain fault forensics).
self="$repo/build/wolftrust-signed.elf"
NM="${ARM_NM:-/usr/bin/arm-none-eabi-nm}"
PYOCD_TARGET="${PYOCD_TARGET:-stm32h563zitx}"

CLI="${STM32_CLI:-$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI}"
SERIAL="${H5_SERIAL:-/dev/ttyACM0}"
# restart reboots guest0 restart_limit times; give the banners time to land.
# confboot reboots the whole chain once per panic test (real SYSRESETREQ, each
# re-running wolfBoot), so it needs a long ceiling; the capture stops early on
# the suite report.
case "$scenario" in restart) cap_default=32 ;; confboot) cap_default=900 ;; *) cap_default=25 ;; esac
CAP_S="${H5_CAPTURE_SECONDS:-$cap_default}"
LOGFILE="${WT_SCENARIO_LOG:-ci-h5-hardware-$scenario.log}"
case "$LOGFILE" in /*) ;; *) LOGFILE="$repo/$LOGFILE" ;; esac
mkdir -p "$(dirname "$LOGFILE")"
# The container build leaves LOGFILE root-owned; fall back to /tmp so the
# host-side flash step's tee cannot fail (pipefail would skip the checks).
if ! : >> "$LOGFILE" 2>/dev/null; then
  LOGFILE="/tmp/$(basename "$LOGFILE").$$"
fi
uart="$repo/h5-uart-capture.log"
cli_log="/tmp/h5-flash-cli.$$.log"

stage()      { printf '  ... %s\n' "$1"; }
check_pass() { printf '  [check] PASS  %s\n' "$1"; }
check_fail() { printf '  [check] FAIL  %s  (%s)\n' "$1" "$2"; exit 1; }
# Both guests raw-write USART3, so another guest's burst can splice into the
# middle of a marker; fall back to a word-gap regex that tolerates bounded
# interleaved fragments between the marker's words.
expect()     { local tol
               if grep -Faq "$2" "$uart"; then check_pass "$1"; return; fi
               tol=$(printf "%s" "$2" | \
                 sed -e 's/[][\\.^$*+?(){}|/]/\\&/g' -e 's/ /.{0,160}/g')
               if grep -zaEq "$tol" "$uart"; then check_pass "$1"; \
               else check_fail "$1" "missing: $2"; fi; }
refute_re()  { if grep -Eq "$2" "$uart"; then check_fail "$1" "unexpected: $2"; \
               else check_pass "$1"; fi; }

# Flash addresses: secure images to the secure alias (SECWM-covered), guests to
# the Non-secure alias (beyond the watermark). Match WT_*_FLASH_BASE.
WOLFBOOT_ADDR=0x0C000000
WOLFTRUST_ADDR=0x0C060000
GUEST0_ADDR=0x080A0000
GUEST1_ADDR=0x080C0000
guest0="$repo/tests/firmware/zephyr-stm32h5/build/guest0_psa/zephyr/zephyr.bin"
guest1="$repo/tests/firmware/zephyr-stm32h5/build/freertos_guest1/freertos_guest1.bin"

if [ "$mode" != "flash" ]; then
  : > "$LOGFILE"
  git config --global --add safe.directory '*'
  git config --global --add safe.directory "$repo"

  export CROSS_COMPILE=/usr/local/bin/arm-none-eabi-
  export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
  export WT_SECURE_FLASH_BASE=0x0C060000
  export WT_SECURE_FLASH_SIZE=0x00040000
  export WOLFBOOT_PARTITION_SIZE=0x40000
  export WOLFBOOT_PARTITION_SWAP_ADDRESS=0x0C140000
  export WT_SECURE_IMAGE_HEADER_SIZE=0x400
  export WT_GUEST0_FLASH_BASE=0x080A0000
  export WT_GUEST1_FLASH_BASE=0x080C0000
  export WT_GUEST_RAM_SIZE=0x00010000
  export WT_GUEST1_RAM_BASE=0x20010000
  export WT_ZEPHYR_DTC_OVERLAY_FILE=boards/wolfboot-stm32h563.overlay
  export WT_MAX_GUESTS=2
  export ZEPHYR_BOARD=nucleo_h563zi/stm32h563xx/ns
  WOLFBOOT_REF=d85fa9dbdf6c36f47b7e96eba5c9df750ad3c963

  stage "fetching wolfTrust dependencies"
  {
    git submodule update --init --single-branch
    rm -rf build tests/firmware/zephyr-stm32h5/build
  } >> "$LOGFILE" 2>&1

  # The wolfBoot first stage is identical across scenarios (same WOLFBOOT_REF);
  # reuse it so back-to-back scenario builds don't re-clone. WT_H5_FRESH_DEPS=1
  # forces a clean rebuild.
  if [ "${WT_H5_FRESH_DEPS:-0}" = "1" ] || \
     [ ! -s "$repo/wolfBoot/wolfboot.bin" ] || \
     [ ! -s "$repo/wolfBoot/wolfboot_signing_private_key.der" ]; then
    stage "building wolfBoot first stage"
    {
      rm -rf wolfBoot
      git clone --no-checkout https://github.com/aidangarske/wolfBoot.git wolfBoot
      cd wolfBoot
      git -c protocol.version=2 fetch --depth 1 origin "$WOLFBOOT_REF"
      git checkout --detach "$WOLFBOOT_REF"
      git submodule update --init --single-branch
      cp "$repo/wolfBoot/config/examples/stm32h5-tz-wolftrust.config" .config
      make keytools
      make -j"$(nproc)" wolfboot.bin wolfboot_signing_private_key.der
      cd "$repo"
    } >> "$LOGFILE" 2>&1
  else
    stage "reusing existing wolfBoot first stage"
  fi

  # crossdomain injects a Secure-side probe; restart injects a NS guest fault
  # probe; positive builds the production images. Mirrors run_m33mu_scenario.sh.
  secure_flags=""; guest_flags=""
  [ "$scenario" = "crossdomain" ] && secure_flags="WT_FFM_NEGATIVE_PROBE=1"
  [ "$scenario" = "restart" ] && guest_flags="WT_GUEST_FAULT_PROBE=1"
  # WT_CONF_DIAG_TRAP=0: the emulator-only hang-probe fault would become a
  # conformance-monitor reset on silicon and can eat the suite's report window.
  [ "$scenario" = "confboot" ] && { secure_flags="WT_CONFORMANCE=1 WT_CONF_DIAG_TRAP=0"; guest_flags="WT_RUN_CONFORMANCE=1"; }

  stage "building wolfTrust secure image ($scenario)"
  {
    env $secure_flags make build/wolftrust.bin build/secure_cmse_implib.o
    IMAGE_HEADER_SIZE=1024 WOLFBOOT_PARTITION_SIZE=0x40000 WOLFBOOT_SECTOR_SIZE=0x2000 \
      "$repo/wolfBoot/tools/keytools/sign" --ecc256 \
        "$repo/build/wolftrust.bin" \
        "$repo/wolfBoot/wolfboot_signing_private_key.der" 1
    test -s "$repo/build/wolftrust_v1_signed.bin"
    cp "$repo/build/wolftrust.elf" "$repo/build/wolftrust-signed.elf"
  } >> "$LOGFILE" 2>&1

  WT_EXPECTED_MEASUREMENT_HEX="$(python3 tests/scripts/read_wolfboot_measurement.py \
    build/wolftrust_v1_signed.bin 2>>"$LOGFILE")"

  # Hardware guest build: NO WT_M33MU_EXPECT_BKPT (emulator-only breakpoint).
  stage "building guests (hardware variant, no emulator BKPT, $scenario)"
  {
    make -C tests/firmware/zephyr-stm32h5 clone
    env $guest_flags $secure_flags WT_REUSE_SECURE_BUILD=1 WT_EXPECTED_LIFECYCLE=0x1000u \
      WT_EXPECTED_MEASUREMENT_HEX="$WT_EXPECTED_MEASUREMENT_HEX" \
      WT_ATTESTATION_DEVELOPMENT_PROFILE=1 \
      make -C tests/firmware/zephyr-stm32h5 build-guest0-psa build-freertos-guest1
  } >> "$LOGFILE" 2>&1

  test -s "$repo/build/wolftrust_v1_signed.bin"
  test -s "$repo/wolfBoot/wolfboot.bin"
  test -s "$guest0"; test -s "$guest1"
  echo "BUILD OK: images ready for flash"
  echo "  wolfboot.bin            -> $WOLFBOOT_ADDR"
  echo "  wolftrust_v1_signed.bin -> $WOLFTRUST_ADDR"
  echo "  guest0_psa zephyr.bin   -> $GUEST0_ADDR"
  echo "  freertos_guest1.bin     -> $GUEST1_ADDR"
fi

if [ "$mode" != "build" ]; then
  if ! "$repo/tests/target/detect_h5.sh" >/dev/null 2>&1; then
    echo "SKIP: H5 hardware ($("$repo/tests/target/detect_h5.sh" 2>&1))"
    exit 0
  fi
  test -s "$repo/wolfBoot/wolfboot.bin" || { echo "FAIL: wolfboot.bin missing — run build first" >&2; exit 1; }
  test -s "$repo/build/wolftrust_v1_signed.bin" || { echo "FAIL: signed secure image missing — run build first" >&2; exit 1; }
  test -s "$guest0" || { echo "FAIL: guest0 image missing — run build first" >&2; exit 1; }
  test -s "$guest1" || { echo "FAIL: guest1 image missing — run build first" >&2; exit 1; }

  # confboot's panic tests resume off a flash-backed boot flag in a reserved
  # secure sector; unlike the emulator (fresh flash each run) the board keeps
  # last run's counters, and stale state makes ~2 tests misresume as SIM ERROR.
  # Erase it so every run starts emulator-fresh.
  if [ "$scenario" = "confboot" ]; then
    stage "erasing conformance boot-flag NVM sector (0x0C1FA000)"
    pyocd erase -t "$PYOCD_TARGET" -s 0x0C1FA000 >/dev/null 2>&1 || true
  fi

  stage "capturing $SERIAL @ 115200 (max ${CAP_S}s)"
  stty -F "$SERIAL" 115200 raw -echo -echoe -echok -onlcr 2>/dev/null || true
  : > "$uart"
  cat "$SERIAL" >> "$uart" 2>/dev/null &
  cap_pid=$!
  sleep 1

  stage "flashing wolfTrust chain via STM32CubeProgrammer"
  # CubeProgrammer exits nonzero after -hardRst even on success; gate on the
  # verify text instead so set -e does not kill the marker checks below.
  "$CLI" -c port=SWD mode=UR \
    -d "$repo/wolfBoot/wolfboot.bin" "$WOLFBOOT_ADDR" \
    -d "$repo/build/wolftrust_v1_signed.bin" "$WOLFTRUST_ADDR" \
    -d "$guest0" "$GUEST0_ADDR" \
    -d "$guest1" "$GUEST1_ADDR" \
    --verify -hardRst > "$cli_log" 2>&1 || true
  cat "$cli_log" >> "$LOGFILE" || true
  grep -aq "verified successfully" "$cli_log" || {
    echo "FAIL: flash verify did not complete" >&2; exit 1; }

  # CubeProgrammer -hardRst is unreliable (observed: board left parked in the
  # pre-flash state); always follow with an explicit debug-port reset.
  pyocd cmd -t "$PYOCD_TARGET" -c reset >/dev/null 2>&1 || true

  # confboot reboots the chain once per panic test and val resumes off its flash
  # boot flag; the run is done when the suite prints its report, so stop early on
  # it rather than wait the whole ceiling. The others have a fixed settling window.
  if [ "$scenario" = "confboot" ]; then
    stage "waiting for the Arm suite report (max ${CAP_S}s)"
    waited=0
    while [ "$waited" -lt "$CAP_S" ]; do
      grep -aq "TOTAL FAILED" "$uart" && break
      sleep 5; waited=$((waited + 5))
    done
  else
    sleep "$CAP_S"
  fi
  kill "$cap_pid" 2>/dev/null || true
  wait "$cap_pid" 2>/dev/null || true
  echo "----- UART capture -----" >> "$LOGFILE"
  cat "$uart" >> "$LOGFILE"

  # After the run, read a secure global by ELF symbol via a non-secure pyocd
  # halt (Open board exposes secure RAM to the debugger). Prints its uint32 as
  # an 8-hex-digit string (no 0x); callers apply 0x for arithmetic.
  read_secure_u32() {
    local sym addr
    sym="$1"
    addr=$("$NM" "$self" 2>/dev/null | awk -v s="$sym" '$3==s{print $1}')
    [ -n "$addr" ] || { echo ""; return; }
    pyocd cmd -t "$PYOCD_TARGET" -c halt -c "read32 0x$addr 4" 2>/dev/null \
      | awk -v a="$addr" 'tolower($1)==a":"{print $2}'
  }

  case "$scenario" in
    positive)
      refute_re "no fault markers in UART" \
        '^(\[MEMFAULT\]|\[HARDFLT\]|HardFault|SecureFault|BusFault|UsageFault)'
      expect "TEE client initialized" "wolfTrust TEE client initialized"
      expect "FF-M psa_framework_version=0x0100" \
        "wolfTrust FF-M psa_framework_version=0x0100"
      expect "SERVICE_CRYPTO dispatch verified" \
        "wolfTrust FF-M SERVICE_CRYPTO dispatch verified"
      expect "ITS set/get verified" \
        "wolfTrust ITS set/get verified"
      expect "PS sealed set/get verified" \
        "wolfTrust PS sealed set/get verified"
      expect "psa_hash_compute(SHA-256) KAT verified" \
        "psa_hash_compute(SHA-256) KAT verified"
      expect "psa_initial_attestation st=0" "psa_initial_attestation st=0"
      expect "attestation COSE_Sign1 verified" \
        "wolfTrust attestation: COSE_Sign1 verified"
      expect "guest0_psa done" "guest0_psa done"
      ;;
    restart)
      # The guest faults on boot each cycle; the monitor restarts it
      # RESTART_LIMIT times then quarantines it FAULTED — the restart budget
      # resets only after a crash-free window, so a crash loop cannot evade the
      # limit. Counted via the monitor's own event counters read over the debug
      # port: UART banners can interleave-split, secure RAM cannot.
      refute_re "no unhandled fault markers" \
        '^(\[HARDFLT\]|HardFault|SecureFault)'
      restarts=$(read_secure_u32 g_wt_restart_events)
      quarantines=$(read_secure_u32 g_wt_quarantine_events)
      if [ -n "$restarts" ] && [ $((0x$restarts)) -eq "$RESTART_LIMIT" ]; then
        check_pass "monitor restarted the guest exactly $RESTART_LIMIT times"
      else
        check_fail "guest restart count" "restart events 0x${restarts:-none}, expected $RESTART_LIMIT"
      fi
      if [ -n "$quarantines" ] && [ $((0x$quarantines)) -eq 1 ]; then
        check_pass "guest quarantined FAULTED after the limit (events=1)"
      else
        check_fail "quarantine" "quarantine events 0x${quarantines:-none}, expected 1"
      fi
      expect "guest1 alive after guest0 FAULTED" "freertos_guest1: heartbeat"
      ;;
    crossdomain)
      # The unprivileged crypto SP reads SPM-private RAM (WT_RAM_S_BASE) on
      # entry; its MPU domain denies it. With graceful quarantine the fault
      # never escalates to HardFault — proof is the captured fault address in
      # the SPM RAM band, and guest1 surviving.
      refute_re "isolation fault did not escalate to HardFault" \
        '^(\[HARDFLT\]|HardFault|SecureFault)'
      fault_cnt=$(read_secure_u32 g_tasklet_fault_count)
      fault_addr=$(read_secure_u32 g_last_fault_address)
      if [ -n "$fault_cnt" ] && [ $((0x$fault_cnt)) -ge 1 ]; then
        check_pass "crypto SP faulted on the cross-domain read (count=0x$fault_cnt)"
      else
        check_fail "cross-domain fault" "SP fault count not captured (count=${fault_cnt:-none})"
      fi
      if [ -n "$fault_addr" ] && \
         [ $((0x$fault_addr)) -ge $((0x30028000)) ] && \
         [ $((0x$fault_addr)) -lt $((0x30093000)) ]; then
        check_pass "denied read targeted SPM-private RAM (addr=0x$fault_addr)"
      else
        check_fail "cross-domain isolation" "fault addr 0x$fault_addr not in the SPM RAM band"
      fi
      expect "guest1 alive after SP quarantined" "freertos_guest1: heartbeat"
      ;;
    confboot)
      # The unmodified Arm val NSPE drives the FF-M IPC suite against wolfTrust
      # on real silicon. Panic tests reboot the chain via real SYSRESETREQ and
      # val resumes off its flash boot flag (K2/K3) — no emulator, no BKPT. No
      # fault-marker refute: the panics are by design. Both guests raw-write
      # USART3 and every reboot splices the boot banners mid-word, so the gate
      # is the ACS report block alone: it prints once in the quiet end window
      # and cannot exist unless val ran the suite end to end.
      expect "Arm suite TOTAL TESTS : 89" "TOTAL TESTS     : 89"
      expect "Arm suite TOTAL PASSED : 85" "TOTAL PASSED    : 85"
      expect "Arm suite TOTAL SKIPPED : 4" "TOTAL SKIPPED   : 4"
      expect "Arm suite TOTAL FAILED : 0" "TOTAL FAILED    : 0"
      expect "ACS run completed" "END OF ACS"
      ;;
  esac
  echo "PASS: hardware/h5/$scenario"
fi
