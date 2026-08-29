# wolfTrust on STM32H5 - Secure-Manager guide (with real runs)

This is the consolidated, evidence-backed guide to running **wolfTrust as a
drop-in TrustZone-M Secure Manager / TF-M replacement** on the STM32H5
(NUCLEO-H563ZI). Every state, transition, and test result below is quoted from
an actual run on the lab board - the raw logs live under `docs/evidence/`.

For the *portable contract* (what a new SoC port must implement) see
[`port-contract.md`](port-contract.md) and [`adding-a-port.md`](adding-a-port.md).
For the requirement traceability see [`requirements/validation-log.md`](requirements/validation-log.md).
Every register/option-byte value below is cross-checked against RM0481 and the
Arm Cortex-M33 architecture in [`rm0481-encoding-crosscheck.md`](rm0481-encoding-crosscheck.md).

---

## 1. What this proves

wolfTrust runs the **unmodified Arm PSA `psa-arch-tests` FF-M IPC conformance
suite** (pinned `e17d294`, val NSPE + test bodies compiled as-is, zero test
edits) against its production SPM on real STM32H563 silicon, deterministically:

```
REGRESSION REPORT:
==========================
   TOTAL TESTS     : 89
   TOTAL PASSED    : 85
   TOTAL FAILED    : 0
   TOTAL SKIPPED   : 4
   TOTAL SIM ERROR : 0
==========================
******* END OF ACS *******
```
*(`docs/evidence/2026-08-19-h5-mp5-confboot/`, and reproduced 20/20 clean after
the #83 fix - see §7.)*

The suite drives the chain through **92 real `SYSRESETREQ` panic-reboots per
run**, val resuming off its flash-backed boot flag each time - i.e. this is the
full authenticated wolfBoot → wolfTrust → Non-secure guest lifecycle, not a
host stub.

**The 4 skips are correct, not a gap.** They are the heap-isolation probes
(i074 NSPE→APP-RoT heap, i078 NSPE→PSA-RoT heap, i082 APP-RoT→PSA-RoT heap,
i086 SP→other-SP heap) plus build-excluded i067 (dynamic heap). wolfTrust is a
**zero-allocation** design - no partition heaps exist - so these self-skip with
skip code 42 ("not applicable, no heap region configured"), the same outcome
TF-M produces with `heap_size: 0`. The **stack** equivalents (i081, i085) pass,
which is the positive proof that per-domain isolation is enforced.

---

## 2. Security memory map (STM32H563, 2 MB dual-bank)

wolfTrust boots as the secure world; the Non-secure guests (Zephyr + FreeRTOS)
are the "applications". The split is enforced by SECWM flash watermarks + SAU +
per-partition MPU.

| Region | Alias | Owner |
| --- | --- | --- |
| `0x0C000000` wolfBoot first stage | Secure | wolfBoot |
| `0x0C060000` wolfTrust secure image (signed) | Secure | wolfTrust SPM |
| `0x0C1FA000` conformance NVM (survive-reset boot flag) | Secure | driver SP |
| `0x0C1FC000` wolfHSM NVM | Secure | wolfHSM |
| `0x080A0000` guest0 (Zephyr/PSA) | Non-secure | NS app |
| `0x080C0000` guest1 (FreeRTOS) | Non-secure | NS app |

**The one silicon rule you must not get wrong:** `SECWM1_END` must span the
*whole* boot partition (through `0x0809FFFF`). See §3 and the SECWM gotcha in §7.

---

## 3. Provisioning the security perimeter

wolfTrust runs **TZEN enabled** with the OEM-iRoT boot path. The option-byte
set is applied by [`tests/target/provisioning_ctrl.sh`](../tests/target/provisioning_ctrl.sh):

```sh
WT_OB=(TZEN=0xB4 BOOT_UBE=0xB4 SWAP_BANK=0x0
       SECWM1_STRT=0x0 SECWM1_END=0x4F SECWM2_STRT=0x0 SECWM2_END=0x7F)
```

- `TZEN=0xB4` - TrustZone on (this is why DA uses the *certificate* OBK, per ST
  AN6008 pairing - see §4).
- `BOOT_UBE=0xB4` - OEM-iRoT boot path (so `SECBOOTADD` is unused).
- `SECWM1 0x00–0x4F` - secure watermark over the entire wolfBoot+wolfTrust boot
  partition. **`0x4F`, not `0x3F`**: the earlier `0x3F` ended the watermark at
  `0x08080000`, so secure-alias writes past it were *silently dropped* and any
  secure image over 128 KiB was truncated on flash → wolfBoot integrity-reject
  (`hdr_ok=1, sha_ok=0`, panic). See §7.
- `SECWM2 0x00–0x7F` - secure bank-2 window (NVM sectors).

Guests live at `0x080A0000+` (sector `0x50`), past the watermark, so they stay
Non-secure - exactly the isolation boundary a Secure Manager needs.

---

## 4. The product-state lock ladder (real transitions)

STM32H5 has a one-way OEM-iRoT product-state ladder (RM0481; codes cross-checked
in [`rm0481-encoding-crosscheck.md`](rm0481-encoding-crosscheck.md) §1).
wolfTrust seals into each locked state and reverses it with a Debug
Authentication (DA) certificate Full Regression. Full walkthrough + raw console:
[`docs/evidence/2026-08-18-h5-mp3-lock/2026-08-19-lock-ladder-walkthrough.md`](evidence/2026-08-18-h5-mp3-lock/2026-08-19-lock-ladder-walkthrough.md).

| State | Code | Debug | Reversible |
| --- | --- | --- | --- |
| Open | `0xED` | all open, free reflash | baseline |
| Provisioning | `0x17` | secure debug closed, NS open | DA regression |
| TZ-Closed | `0xC6` | TrustZone sealed | DA regression |
| Closed | `0x72` | debug fully closed | DA full regression |
| Locked | `0x5C` | permanent | **never - brick** |

**Safety gate (non-negotiable):** before each advance, a non-destructive
`discover` (DA `debugauth=2`) must show integrity `0xeaeaeaea` and a Full
Regression path. `provisioning_ctrl.sh advance` refuses Locked (`0x5C`) outright.

Real advance Open → Provisioning:
```
$ provisioning_ctrl.sh advance 0x17
ADVANCING product state 0xED -> 0x17 (regress is the only way back)
Option Bytes successfully programmed
now: 0x17
```

Real GATE check after provisioning the certificate OBK:
```
$ provisioning_ctrl.sh discover
discovery: PSA lifecycle...................:ST_LIFECYCLE_PROVISIONING
discovery: ST provisioning integrity status:0xeaeaeaea
discovery: permission if authorized........:(a/14) ==> Full Regression
Debug Authentication: Discovery Success
```

Real advance to TZ-Closed (note: closing debug drops the connection - benign):
```
$ provisioning_ctrl.sh advance 0xc6
ADVANCING product state 0x17 -> 0xc6 (regress is the only way back)
discovery: PSA lifecycle...................:ST_LIFECYCLE_TZ_CLOSED
discovery: ST provisioning integrity status:0xeaeaeaea
```

And the money shot - **wolfTrust boots and runs with debug fully sealed**
(Closed / TZ-Closed), then a DA Full Regression mass-erases back to Open for
re-test. The dev board is *never* advanced to Locked (`0x5C`), which is
permanent.

Workflow driver: [`tests/target/h5_lock_workflow.sh`](../tests/target/h5_lock_workflow.sh)
(`status` / `advance <hexstate>` / `provision` / `regress`).

---

## 5. Build and the four hardware scenarios

The board runner is [`tests/target/run_h5_hardware.sh`](../tests/target/run_h5_hardware.sh)
(`build` / `flash` / `all` × `positive` / `restart` / `crossdomain` / `confboot`).
It builds wolfBoot → wolfTrust → the two guests, flashes via STM32CubeProgrammer,
resets via pyocd, and asserts per-scenario markers over `/dev/ttyACM0`.

| Scenario | Proves |
| --- | --- |
| `positive` | full lifecycle green: FF-M dispatch, SHA-256 KAT, `psa_initial_attestation`, COSE_Sign1 verify |
| `restart` | a Non-secure guest faults on boot; the monitor restarts it; the *other* guest keeps running |
| `crossdomain` | a probe inside the crypto SP reading another domain faults (isolation) |
| `confboot` | the unmodified Arm conformance suite (§1) |

Same scenarios run on the **M33MU emulator** via
[`tests/target/run_m33mu_scenario.sh`](../tests/target/run_m33mu_scenario.sh)
(no board needed); the M33MU gate is the CI equivalent. Emulator = functional
proof; hardware = the silicon-real proof (SECWM, physical reset line, persistent
flash - none of which M33MU models).

---

## 6. The conformance run on H5, end to end

`run_h5_hardware.sh flash confboot` on a provisioned Open-state board:

1. **erase** the conformance NVM boot-flag sector (`pyocd erase -s 0x0C1FA000`)
   so stale cross-run counters don't misresume (the board keeps flash between
   runs; the emulator starts fresh - this bit us, see §7).
2. **flash** wolfBoot + signed wolfTrust + both guests via CubeProgrammer.
3. **reset** via pyocd (CubeProgrammer `-hardRst` proved unreliable - §7).
4. **capture** `/dev/ttyACM0` until the ACS report block appears.
5. the suite runs all 89 tests; each panic test writes its boot flag, fires
   `SYSRESETREQ`, and val resumes off the flag on reboot - **~92 authenticated
   reboots per run**.
6. assert the report block (§1) - only the quiet-window report is asserted, not
   the interleaved boot banners (both guests share one UART - §7).

Official assertions end with:
```
[check] PASS  Arm suite TOTAL TESTS : 89
[check] PASS  Arm suite TOTAL PASSED : 85
[check] PASS  Arm suite TOTAL FAILED : 0
[check] PASS  ACS run completed
PASS: hardware/h5/confboot
```

---

## 7. Everything you should know (silicon gotchas)

Learned the hard way on the board; each is a real defect fixed in-tree.

1. **SECWM watermark truncation.** Secure-alias writes past `SECWM1_END` are
   *silently dropped* (no error). A short watermark (`0x3F`) truncated the
   140 KiB signed conformance image → wolfBoot integrity-reject. Fix:
   `SECWM1_END=0x4F` covers the whole boot partition. (§3)

2. **`-hardRst` is unreliable.** CubeProgrammer's hardware-reset flag
   intermittently didn't reset the part (the captured frame was bit-identical
   incl. uninitialized scratch). Fix: explicit `pyocd reset` after flash. (§6)

3. **Stale cross-run NVM.** The board keeps flash between runs; a stale val
   boot flag makes ~2 tests misresume as SIM ERROR. Fix: `pyocd erase` the
   boot-flag sector before each confboot. (§6)

4. **Shared-UART interleave.** Both guests raw-write USART3; boot banners splice
   mid-word every reboot. Only the quiet-window ACS report block is assertable.

5. **The #83 gate flake - a Non-secure guest resetting the SoC.** ~1/4 runs
   reported a single SIM ERROR. Root cause (found with a reset-survival SRAM
   "black box", since a reset defeats both the UART log and the ST-Link):
   guest0's Zephyr (`CONFIG_REBOOT` + `sys_reboot`) intermittently issued a
   **Non-secure `SYSRESETREQ`** mid-suite, resetting the whole SoC while val had
   armed `BOOT_NOT_EXPECTED`. **Fix (commit `52f13bb`):** set
   `AIRCR.SYSRESETREQS` in secure init so a Non-secure `SYSRESETREQ` can no
   longer reset the SoC - the Secure world is the sole reset authority (correct
   Secure-Manager policy; TF-M does the same). Two latent bugs fell out of the
   same hunt and were fixed alongside: SysTick defaulted to priority 0 and could
   preempt PendSV mid-coroutine-switch (INVPC faults) - now equal-lowest with
   PendSV; and the HSM tasklet preempt is now SPSEL-gated (PSP-only). Result:
   **20/20 clean**. Full write-up: `requirements/validation-log.md` (#83).

6. **Diag-trap-as-reset hazard.** The emulator's hang-probe deliberately faults
   for a register dump; on silicon that fault becomes a conformance-monitor
   reset that can eat the report window. Gated off on hardware
   (`WT_CONF_DIAG_TRAP=0`).

---

## 8. Reproduce it

Board bring-up, provisioning, the 4 scenarios, the lock workflow, and this
gotcha list are packaged as the **`wolftrust-h5-hardware`** skill; the emulator
equivalent is **`wolftrust-m33mu`**. Evidence archives:

- `docs/evidence/2026-08-18-h5-mp1-mp2/` - bring-up + functional equivalence.
- `docs/evidence/2026-08-18-h5-mp3-lock/` - the full lock ladder walkthrough.
- `docs/evidence/2026-08-19-h5-mp5-confboot/` - the conformance run + 3 silicon
  defects.
