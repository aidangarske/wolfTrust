# STM32H563 lock/provisioning findings & discrepancies (MP3, 2026-08-18)

Hard-won, board-verified learnings from taking wolfTrust through the STM32H563
OEM-iRoT lock lifecycle. These feed the wolfTrust lock-workflow docs (MP6) and
explain why `provisioning_ctrl.sh` is shaped the way it is. Authoritative
source for the mechanism is ST **AN6008** (Getting started with Debug
Authentication) + RM0481 product state; verify against them before external
claims.

## 1. Option-byte perimeter: wolfBoot's helper is wrong for wolfTrust
`wolfBoot/tools/scripts/set-stm32-tz-option-bytes.sh` computes SECWM from
`(BOOT_ADDRESS - 0x08000000)/sector`. wolfTrust's config uses the **secure
alias** `0x0C060000`, so the math yields garbage sectors, and the script never
sets `BOOT_UBE`. The real, verified wolfTrust perimeter (read from a known-good
board, CubeProgrammer confirms "unchanged"): `TZEN=0xB4 BOOT_UBE=0xB4
SWAP_BANK=0x0 SECWM1 0x0-0x3F SECWM2 0x0-0x7F`, `SECBOOTADD=0x0` (OEM-iRoT boot
does not use SECBOOTADD). `provisioning_ctrl.sh set-perimeter` supersedes the
wolfBoot script with these exact values.

## 2. DA OBK provisioning is rejected in the Open product state
`-sdp DA_*.obk` in Open fails ("Not able to load pConfig to MCU RAM" /
"Provisioning with password is not supported by the current device"). ST's
`provisioning.sh` sets `-ob PRODUCT_STATE=0x17` (Provisioning) FIRST, then
`-sdp`. Confirmed on-board: provisioning succeeds only in Provisioning
("OBKey Provisioned successfully").

## 3. THE BIG ONE — password DA vs TrustZone are coupled
Per ST community + AN6008: **password DA is only valid when TZEN is DISABLED
(0xC3); certificate DA is required when TZEN is ENABLED (0xB4).** ST warns that
provisioning a password hash while TZ is enabled (or a cert-root hash while TZ
is disabled) is a mismatch that **can permanently lock the device if advanced
to Closed.** Our Stage A provisioned the *password* OBK
(`DA_ConfigWithPassword.obk`) while TZEN stayed **enabled** → mismatch. With
TZEN on, CubeProgrammer authenticates with the certificate, the device
(password-provisioned) has no matching cert root, and the DA handshake times
out ("signing token / submitted permissions: a / Found 3 certificates /
Timeout while receiving response / Debug Authentication Failed"). We are in
**Provisioning, NOT Closed**, DA integrity is **good** (`0xeaeaeaea`, not the
corrupted `0xf5f5f5f5` seen in stuck-device threads), and discovery still
succeeds — so the device is **recoverable**, and we never advance to Closed
until the credential/TZEN pairing is correct.

Correct pairings going forward:
- TZEN **enabled** (wolfTrust's OEM-iRoT default) → **certificate** DA
  (`DA_Config.obk` + cert chain), OR
- TZEN **disabled** → **password** DA (`DA_ConfigWithPassword.obk` + password.bin).

## 4. Regression connection mode
ST's `dbg_auth.sh` authenticates on a bare `-c port=SWD speed=fast` (default
NORMAL / under-reset) — this halts the running firmware so the RSS can answer
the DA handshake. `mode=Hotplug` leaves wolfTrust running and the device
response times out. (Not the root cause of our failure — #3 is — but a real
requirement.)

## 5. ST's `regression.sh` is Closed-oriented
Its `debugauth=3` + `-ob TZEN=0xC3` + re-`sdp` prefix is the Closed→Open path.
From Provisioning it wedged the session (TZEN left untouched, auth timed out).
The Provisioning→Open regression is the plain `dbg_auth.sh` auth with `per=a`.

## Current board state (safe, recoverable)
`PRODUCT_STATE=0x17 (Provisioning)`, `TZEN=0xB4`, password OBK provisioned,
DA integrity `0xeaeaeaea`, `Discovery Success` with `(a/14) Full Regression`
available, wolfTrust chain still in flash. Recovery options (pick per ST
AN6008): (a) disable TZEN (`-ob TZEN=0xC3` / TZEN regression → mass-erase to
Open, then password path), or (b) re-provision a **certificate** OBK and
authenticate with the cert chain (TZEN-enabled path). Do NOT advance to Closed
with the current mismatch.
