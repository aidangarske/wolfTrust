#!/usr/bin/env bash
# wolfTrust FF-M target-scenario runner. Builds the authenticated
# wolfBoot -> wolfTrust -> guests chain and boots it under the M33MU emulator,
# asserting one scenario's markers. Run inside ghcr.io/wolfssl/wolfboot-ci-m33mu
# (the same image CI uses); never bare-metal on the host, so every file under
# the repo stays root-owned and consistent across runs.
#
#   run_m33mu_scenario.sh positive     lifecycle green, no faults
#   run_m33mu_scenario.sh restart      guest faults on boot; monitor restarts it
#                                       restart_limit times then leaves it FAULTED
#   run_m33mu_scenario.sh crossdomain  a probe inside the crypto SP reads
#                                       SPM-private RAM and the SP domain faults
#   run_m33mu_scenario.sh confboot     conformance image (Arm server/client SPs
#                                       scheduled, WT_CONFORMANCE=1) boots the
#                                       full positive lifecycle green
#
# This is the single source the local make test-target harness, the box skill
# scripts, and the CI jobs all drive, so each scenario's markers stay identical.
set -euo pipefail
set -o pipefail

scenario="${1:-}"
case "$scenario" in
  positive|restart|crossdomain|confboot) ;;
  *) echo "usage: $0 positive|restart|crossdomain|confboot" >&2; exit 2 ;;
esac

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$repo"

# Manifest guest restart_limit (port/stm32h563/manifest.json domain id 1).
RESTART_LIMIT=3

# The container runs as root while a host clone is owned by the login user;
# without this git rejects every submodule with "dubious ownership".
git config --global --add safe.directory '*'
git config --global --add safe.directory "$repo"

# --- Workflow env (wolfboot-wolftrust-m33mu job). Keep in sync with the yml. ---
export CROSS_COMPILE=/usr/local/bin/arm-none-eabi-
export ZEPHYR_TOOLCHAIN_VARIANT=cross-compile
export WT_SECURE_FLASH_BASE=0x0C060000
export WT_SECURE_FLASH_SIZE=0x00020000
export WT_SECURE_IMAGE_HEADER_SIZE=0x400
export WT_GUEST0_FLASH_BASE=0x08080000
export WT_GUEST1_FLASH_BASE=0x080A0000
export WT_ZEPHYR_DTC_OVERLAY_FILE=boards/wolfboot-stm32h563.overlay
export WT_MAX_GUESTS=2
export ZEPHYR_BOARD=nucleo_h563zi/stm32h563xx/ns
WOLFBOOT_REF=d85fa9dbdf6c36f47b7e96eba5c9df750ad3c963
M33MU_REF=c84792f7f9e9ce24cf94ffc492c36231de1854c2

# --- Build the pinned M33MU emulator. Reuse a caller-supplied or already-built
#     binary so back-to-back scenarios in one container share the build (and a
#     fresh clone never lands in a non-empty /tmp/m33mu_src). ---
if [ -n "${M33MU:-}" ] && [ -x "${M33MU:-}" ]; then
  echo "Using prebuilt M33MU: $M33MU"
elif [ -x /tmp/m33mu_src/build/m33mu ]; then
  M33MU=/tmp/m33mu_src/build/m33mu
  echo "Reusing M33MU from a prior scenario: $M33MU"
else
  rm -rf /tmp/m33mu_src
  git clone --no-checkout https://github.com/danielinux/m33mu.git /tmp/m33mu_src
  cd /tmp/m33mu_src
  git fetch --depth 1 origin "$M33MU_REF"
  git checkout --detach "$M33MU_REF"
  # M33MU-1 (validation-log.md defect register): TB successor chaining used the
  # finished block's security state, mis-decoding across BXNS/SG edges. Local
  # fix until it lands upstream; drop once M33MU_REF includes it.
  git apply "$repo/tests/target/m33mu-tb-sec-chain.patch"
  cmake -S . -B build -DM33MU_ENABLE_WOLFSSL=OFF
  cmake --build build --target m33mu -j"$(nproc)"
  M33MU=/tmp/m33mu_src/build/m33mu
  cd "$repo"
fi

# --- wolfTrust dependencies + a clean secure build (drop any stale build/
#     whose generated wolfhsm_cfg.h bakes in a host-absolute path). The guest
#     CMake cache must go too: scenarios configure guest0_psa with different
#     -D sets and CMake refuses to regenerate over a conflicting cache. ---
git submodule update --init --single-branch
rm -rf build tests/firmware/zephyr-stm32h5/build

# --- Secure-app wolfBoot first stage. ---
rm -rf wolfBoot
git clone --no-checkout https://github.com/aidangarske/wolfBoot.git wolfBoot
cd wolfBoot
git -c protocol.version=2 fetch --depth 1 origin "$WOLFBOOT_REF"
git checkout --detach "$WOLFBOOT_REF"
git submodule update --init --single-branch
cp "$repo/wolfBoot/config/examples/stm32h5-tz-wolftrust.config" .config
# Build keytools serially first: a parallel keytools link races on the shared
# sp_* objects and intermittently fails "undefined reference".
make keytools
make -j"$(nproc)" wolfboot.bin wolfboot_signing_private_key.der
cd "$repo"

# --- Relocated wolfTrust secure runtime, signed for the reserved slot. The
#     crossdomain scenario injects a Secure-side probe; the others build the
#     production secure image. ---
secure_flags=""
if [ "$scenario" = "crossdomain" ]; then
  secure_flags="WT_FFM_NEGATIVE_PROBE=1"
elif [ "$scenario" = "confboot" ]; then
  secure_flags="WT_CONFORMANCE=1"
fi
env $secure_flags make build/wolftrust.bin build/secure_cmse_implib.o
IMAGE_HEADER_SIZE=1024 WOLFBOOT_PARTITION_SIZE=0x20000 WOLFBOOT_SECTOR_SIZE=0x2000 \
  "$repo/wolfBoot/tools/keytools/sign" --ecc256 \
    "$repo/build/wolftrust.bin" \
    "$repo/wolfBoot/wolfboot_signing_private_key.der" 1
test -s "$repo/build/wolftrust_v1_signed.bin"
# Snapshot the elf that matches the signed image: the guest build below can
# relink build/wolftrust.elf, which poisons post-mortem symbolization.
cp "$repo/build/wolftrust.elf" "$repo/build/wolftrust-signed.elf"

WT_EXPECTED_MEASUREMENT_HEX="$(python3 tests/scripts/read_wolfboot_measurement.py \
  build/wolftrust_v1_signed.bin)"

# --- Guests. The restart scenario injects a Non-secure fault probe in the PSA
#     guest; the others build it unmodified. ---
guest_flags=""
if [ "$scenario" = "restart" ]; then
  guest_flags="WT_GUEST_FAULT_PROBE=1"
elif [ "$scenario" = "confboot" ]; then
  guest_flags="WT_RUN_CONFORMANCE=1"
fi
make -C tests/firmware/zephyr-stm32h5 clone
env $guest_flags $secure_flags WT_REUSE_SECURE_BUILD=1 WT_EXPECTED_LIFECYCLE=0x1000u \
  WT_EXPECTED_MEASUREMENT_HEX="$WT_EXPECTED_MEASUREMENT_HEX" \
  WT_ATTESTATION_DEVELOPMENT_PROFILE=1 WT_M33MU_EXPECT_BKPT=1 \
  make -C tests/firmware/zephyr-stm32h5 build-guest0-psa build-freertos-guest1

echo "Guest vector tables (SP, reset PC):"
od -An -tx4 -N8 "$repo/tests/firmware/zephyr-stm32h5/build/guest0_psa/zephyr/zephyr.bin"
od -An -tx4 -N8 "$repo/tests/firmware/zephyr-stm32h5/build/freertos_guest1/freertos_guest1.bin"

# --- Boot. The restart scenario must NOT pass --quit-on-faults: its guest fault
#     is handled by the monitor, which restarts the guest; halting on the fault
#     would defeat the count. The others halt on any unexpected fault. ---
quit_flag="--quit-on-faults"
timeout_s=60
if [ "$scenario" = "restart" ]; then
  quit_flag=""
  timeout_s=40
elif [ "$scenario" = "confboot" ]; then
  # i047's must-panic reset reboots the whole chain mid-suite: two boots.
  timeout_s=90
fi

log="$repo/ci-m33mu-$scenario.log"
set +e
"$M33MU" "$repo/wolfBoot/wolfboot.bin" \
  "$repo/build/wolftrust_v1_signed.bin:0x60000" \
  "$repo/tests/firmware/zephyr-stm32h5/build/guest0_psa/zephyr/zephyr.bin:0x80000" \
  "$repo/tests/firmware/zephyr-stm32h5/build/freertos_guest1/freertos_guest1.bin:0xA0000" \
  --uart-stdout --expect-bkpt 0x7f $quit_flag --timeout "$timeout_s" | tee "$log"
emu_status=${PIPESTATUS[0]}
set -e
echo "wolfBoot/wolfTrust M33MU exit status: $emu_status"

case "$scenario" in
  positive)
    if grep -Eq '^(\[MEMFAULT\]|\[HARDFLT\]|HardFault|SecureFault)' "$log"; then
      echo "FAIL: fault marker in positive boot log"; exit 1
    fi
    grep -Fq "wolfTrust TEE client initialized" "$log"
    grep -Fq "wolfTrust FF-M psa_framework_version=0x0100" "$log"
    grep -Fq "wolfTrust FF-M SERVICE_CRYPTO dispatch verified" "$log"
    grep -Fq "wolfTrust FF-M forged-handle call rejected" "$log"
    grep -Fq "wolfTrust FF-M oversized-vector call rejected" "$log"
    grep -Fq "psa_hash_compute(SHA-256) KAT verified" "$log"
    grep -Fq "psa_initial_attestation st=0" "$log"
    grep -Fq "wolfTrust attestation: COSE_Sign1 verified" "$log"
    grep -Fq "attestation verify=0 challenge=ok identity=ok lifecycle=0x1000 measurement=ok cose=ES256" "$log"
    grep -Fq "[EXPECT BKPT] Success" "$log"
    echo "PASS: target/positive"
    ;;
  confboot)
    # The conformance guest is lean (no deep-stack attestation path) so the val
    # NSPE framework fits guest0's 32 KiB NS window; the full lifecycle is the
    # positive scenario's job. This proves the Arm SPs schedule and val runs.
    if grep -Eq '^(\[MEMFAULT\]|\[HARDFLT\]|HardFault|SecureFault)' "$log"; then
      echo "FAIL: fault marker in confboot boot log"; exit 1
    fi
    grep -Fq "wolfTrust TEE client initialized" "$log"
    grep -Fq "wolfTrust FF-M psa_framework_version=0x0100" "$log"
    grep -Fq "wolfTrust FF-M SERVICE_CRYPTO dispatch verified" "$log"
    grep -Fq "wolfTrust FF-M conformance: val_entry start" "$log"
    # i047/i055/i057 are panic tests: each server commits a must-panic
    # programmer error, the SPM resets (P5 K3), and val resumes across the
    # reboot off its flash-backed boot flag (K2) — 9 total, three reboots in
    # one boot. Needs the M33MU-1 SPSEL patch the emulator build step applies
    # above.
    grep -Fq "TOTAL PASSED    : 9" "$log"
    grep -Fq "TOTAL FAILED    : 0" "$log"
    grep -Fq "[EXPECT BKPT] Success" "$log"
    echo "PASS: target/confboot"
    ;;
  restart)
    expected=$((RESTART_LIMIT + 1))
    banners=$(grep -c "guest0_psa alive" "$log" || true)
    echo "guest0_psa banner count: $banners (expect restart_limit+1 = $expected)"
    if [ "$banners" -eq "$expected" ]; then
      echo "PASS: target/restart — guest restarted $RESTART_LIMIT times then FAULTED"
      exit 0
    fi
    echo "FAIL: expected $expected guest banners, saw $banners"; exit 1
    ;;
  crossdomain)
    if grep -Eq '\[MEMFAULT\].*addr=0x30028000' "$log"; then
      echo "PASS: target/crossdomain — cross-domain read of 0x30028000 denied by SP domain"
      exit 0
    fi
    echo "FAIL: expected cross-domain MEMFAULT at 0x30028000"; exit 1
    ;;
esac
