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

## 3. THE BIG ONE - password DA vs TrustZone are coupled
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
succeeds - so the device is **recoverable**, and we never advance to Closed
until the credential/TZEN pairing is correct.

Correct pairings going forward:
- TZEN **enabled** (wolfTrust's OEM-iRoT default) → **certificate** DA
  (`DA_Config.obk` + cert chain), OR
- TZEN **disabled** → **password** DA (`DA_ConfigWithPassword.obk` + password.bin).

## 4. Regression connection mode
ST's `dbg_auth.sh` authenticates on a bare `-c port=SWD speed=fast` (default
NORMAL / under-reset) - this halts the running firmware so the RSS can answer
the DA handshake. `mode=Hotplug` leaves wolfTrust running and the device
response times out. (Not the root cause of our failure - #3 is - but a real
requirement.)

## 5. CORRECTION - ST's `regression.sh` IS the Provisioning→Open recovery
An earlier note here called `regression.sh` "Closed-oriented"; reading the
actual **`.sh`** (not the `.bat`) proves that wrong. ST's `regression.sh`
carries a `.sh`-only prefix with the literal comment *"In case of Provisioning
Product state, try to disable TZEN and provision DA with password"* and does,
in order:
1. `debugauth=3` (close any stale session), Hotplug.
2. `-ob TZEN=0xC3` - **disable TrustZone** (Hotplug + `-hardRst`). This flips
   the credential rule: with TZEN now disabled, the **password** is the correct
   DA pairing (AN6008 §3).
3. a bare reset connect, then `-sdp DA_ConfigWithPassword.obk` - re-provision
   the password OBK (freely overwritable in Provisioning; `obk_provisioning.sh`
   has no already-provisioned guard).
4. `-c port=SWD per=a key=… cert=… pwd=password.bin debugauth=1` - full
   regression; CubeProgrammer auto-picks password because TZEN is now disabled →
   mass-erase → **Open**.

Our `provisioning_ctrl.sh regress` omitted step 2 (kept TZEN enabled and forced
`cert=` certificate auth against a password OBK) - that is the exact cause of
the timeout. Fix `regress` to mirror ST's `.sh`.

## 6. CubeProgrammer may prompt for DA args instead of taking them from the CLI
Documented ST-community gotcha: some CubeProgrammer builds ignore
`key=/cert=/pwd=/per=` on the command line for `debugauth=1` and prompt for them
interactively. A non-interactive (piped/`nohup`) run then hangs - reads as a
"wedge"/timeout even when the credentials are right. Run the regression step in
a real interactive shell (or confirm the installed CLI honors the CLI args)
before concluding the credential is wrong.

## 7. RESOLVED on-board (2026-08-19) - certificate regression recovered it
The TZEN-disable path in ST's `regression.sh` does NOT work from our
Provisioning state: disabling TZEN is a *secure* option-byte write, which needs
secure debug open, which needs a *matching* DA credential - the very thing we
lacked. `-ob TZEN=0xC3` failed with *"Cannot connect to access port 1"*. The
recovery that actually worked keeps TZEN enabled and uses the **certificate**
(the correct TZEN-enabled pairing), re-provisioning the OBK to match:

1. `-rst` (HotPlug) to clear any debug lock. Note: `debugauth=3` LOCKS the
   session and then blocks `-sdp`/AP access - do not run it before provisioning.
2. `-c port=SWD speed=fast ap=1 mode=Hotplug -sdp ./Binary/DA_Config.obk` -
   re-provision the **certificate** OBK over the password one (freely
   overwritable in Provisioning; integrity stayed `0xeaeaeaea`).
3. `-c port=SWD per=a key=./Keys/key_3_leaf.pem cert=./Certificates/cert_leaf_chain.b64 pwd=./Binary/password.bin debugauth=1`
   - certificate auth (bare `-c port=SWD` = NORMAL/under-reset so the RSS
   answers) → *Authentication successful* → Full Regression → mass-erase.

Result: `PRODUCT_STATE=0xED (Open)`, `TZEN=0xC3`. Then
`provisioning_ctrl.sh set-perimeter` re-enabled `TZEN=0xB4` + perimeter and
`flash`+`verify` restored wolfTrust (`[check] PASS wolfTrust chain boots on
silicon`). Full log:
`docs/evidence/2026-08-18-h5-mp3-lock/2026-08-19-recovery-cert-regression.log`.

`provisioning_ctrl.sh` now defaults DA to the certificate OBK and its `regress`
uses this proven sequence. The reversible-lock round-trip (seal → DA regression
→ restore) is proven on real STM32H563 silicon.
