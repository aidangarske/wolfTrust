# wolfTrust P2 — Real STM32H563/H5 hardware + TF-M drop-in replacement (2026-08-18)

## Where this sits (numbering)

- **P1 (DONE):** clean-room wolfTrust SPM + FF-M IPC + PSA RoT services, proven on
  the M33MU emulator — full Arm FF-M IPC v1.8 suite **85/4**, positive/restart/
  crossdomain green, host suites green, validation on `49f7dad`. **Emulator
  evidence only.**
- **P2 (THIS PLAN) — the first big hardware phase.** Make it literally true on
  silicon and deliver the actual TF-M drop-in replacement. Two arcs:
  - **Arc A** — bring wolfTrust up on a real Nucleo-H563ZI and get the same tests
    green on hardware, fixing whatever silicon surfaces.
  - **Arc B** — replace an in-place TF-M with wolfTrust and prove a TF-M
    Non-secure app runs unmodified against our secure side.

P2 supersedes the emulator-era P8/P9 placeholders. **Hardware evidence is a
separate ledger — never implied by emulator results.**

## Baseline from the box probe (2026-08-18, board connected)

- Board: **NUCLEO-H563ZI**, target `stm32h563zitx`, via **STLINK-V3**
  (`0483:374e`). pyocd recognizes it; VCP at `/dev/ttyACM0` (`plugdev`, no sudo).
- **STM32CubeProgrammer present:**
  `~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI`.
- **Board is ALREADY TrustZone-provisioned** (`-ob displ`): `TZEN=0xB4` (enabled),
  `BOOT_UBE=OEM-iRoT (user flash)`, `SECBOOTADD=0x0C000000` (matches wolfBoot's
  secure base), `NSBOOTADD=0x08000000`, SECWM1 pages 0x00–0x4F (the whole boot partition; 0x3F truncated >128K images), SECWM2 pages
  0x00–0x7F. The risky TZEN-enable is already done and the boot addresses already
  match wolfBoot — this board was previously provisioned for an OEM secure boot,
  **not** running ST's TF-M / Secure-Manager.
- Net: **P2.0 shrinks to** confirming SECWM covers the wolfTrust secure region and
  standing up the flash → reset → capture path; we can go straight to P2.1.

---

## Arc A — Hardware bring-up

### P2.0 — Prerequisites & a safe flashing path (GATE, do first)
- Stand up the flash tool for TrustZone: install STM32CubeProgrammer CLI on the
  box, or prove a pyocd/OpenOCD recipe that can set TZEN + SECWM. Prove a
  **known-good flash first** — erase + flash wolfBoot alone, confirm its UART
  banner — before touching the full chain.
- Identify the board VCP (`/dev/ttyACM0`, 115200 8N1) and confirm it is the UART
  wolfTrust prints on (LPUART1/USART3 on the Nucleo).
- **Brick-safety, up front:** RDP stays **Level 0**; set **SECWM before TZEN=1**;
  keep the exact `-ob` option-byte commands and an RDP-regression/unlock recipe
  in this doc; **never RDP Level 2**.
- Deliverable: a documented, repeatable flash → reset → UART-capture path.

### P2.1 — Flash the signed chain + first boot (smoke)
- Reuse the exact images the scenario runner already builds: `wolfboot.bin`,
  `wolftrust_v1_signed.bin`, `guest0_psa/zephyr.bin`, `freertos_guest1.bin`.
  Their addresses (`0x0C000000` / `0x0C060000` / `0x080A0000` / `0x080C0000`) are
  real H5 addresses — no rebuild-for-target needed.
- Set option bytes (TZEN=1, SECWM over the secure region, boot `0x0C000000`),
  flash each image at its offset, reset, capture UART.
- **Assert the real markers** (the positive scenario minus the emulator-only
  `[EXPECT BKPT] Success`): TEE client initialized, `psa_framework_version=
  0x0100`, SERVICE_CRYPTO dispatch verified, forged-handle + oversized-vector
  rejected, `psa_hash_compute(SHA-256) KAT`, `psa_initial_attestation st=0`,
  COSE_Sign1 verified, `attestation verify=0 …`, `guest0_psa done`, and the
  freertos_guest1 wolfHSM/wolfPKCS11 markers. Real completion = guest-done
  banners + no fault marker (no BKPT on silicon).

### P2.2 — Fix hardware-only issues (the real bring-up)
Silicon surfaces what the emulator abstracts; expect to iterate:
- Clock/PLL (real ~250 MHz vs the emulator's fixed clock), flash wait states, VOS.
- GTZC/SAU: reconcile the runtime `wt_gtzc_init` split against the option-byte
  SECWM watermark.
- I/D cache (H5 has cache the emulator likely omits) — coherency after flashing.
- Real UART baud/pin mux, RNG/entropy peripheral, SysTick, real MPU region
  limits, real NVIC/IRQ (the LPUART1 route P4.2 added).
- Each fix: a commit + a **hardware** validation-log entry (capture fault →
  root-cause → fix → reflash).

### P2.3 — Repeatable hardware smoke harness
- `tests/target/detect_h5.sh` (ST-Link/serial present?) + `tests/target/
  run_h5_hardware.sh` + a `make test-hardware` target: flash → reset → capture
  UART → run the same `[check]` checklist (reuse the runner's helpers) against
  real output. Honest PASS/FAIL + log, detect-or-skip, like `make test-target`.
- Separate hardware ledger in `validation-log.md`.

---

## Arc B — The TF-M drop-in replacement

### P2.4 — Replacement mapping + docs (`docs/tfm-replacement.md`)
Document how wolfTrust drops in where TF-M sits:
- **Boundary** — what wolfTrust provides that TF-M did: the SPM, FF-M IPC
  (`psa_connect`/`psa_call`/`psa_close`), PSA RoT services (Crypto / Initial
  Attestation / ITS), Level-3 partition isolation, and the secure boot chain.
- **Flash-layout mapping** — TF-M BL2 → wolfBoot; TF-M SPE (`secure_fw`) →
  wolfTrust secure image; TF-M NSPE → the NS guest(s). Address-by-address table.
- **API-compat surface** — the NS-side PSA client API wolfTrust exposes vs TF-M's
  `psa/*.h`: what is identical, what differs, what (ideally nothing) a TF-M NS app
  must change.
- **Procedure** — build + flash the wolfTrust chain in place of a TF-M build.

### P2.5 — TF-M baseline, drop-in replacement, and a direct wolfTrust workflow
TF-M is NOT factory-stock on the Nucleo-H563ZI — it is software you build/flash
(ST ships a TF-M reference in the STM32CubeH5 ROT examples; ST's separate "Secure
Manager" parts carry an immutable TF-M-based RoT, which this board is not). Three
deliverables:
- **(a) Direct wolfTrust workflow (primary path).** `make flash-stm32h563` /
  `make run-hardware` that builds + flashes + boots the wolfTrust chain from
  scratch — the everyday "just use ours" path, no TF-M involved. This is what
  ships.
- **(b) Stock TF-M baseline.** Build + flash ST's TF-M reference (STM32CubeH5
  ROT/TFM example) for the H563 and boot it — the apples-to-apples baseline
  (folds in old P8).
- **(c) Drop-in replacement + proof.** Replace the TF-M SPE with wolfTrust while
  keeping the NS side, and run a TF-M NS app (or the Arm `val` NSPE) unmodified
  against wolfTrust's secure side — same `psa_call` ABI, same services answer.
  The literal "full TF-M drop-in port" test.

### P2.6 — Hardware qualification record
- `validation-log.md` hardware section: board, ST-Link, toolchain versions, image
  SHA-256s, option-byte config, captured UART, PASS/FAIL. Explicitly hardware,
  never emulator-implied. Closes the old P9/#38.

### P2.7 — Immutable lock-in demo (wolfTrust as an OEM-owned RoT, verified)
The marquee competitive demo: wolfTrust delivers Secure Manager's immutability
guarantee while the OEM owns the keys. Verified feasible on H5 (see
`docs/competitive-edge-vs-secure-manager.md`). On the Nucleo-H563ZI:
- **⚠️ Provision a Debug-Authentication password/OBK FIRST** — mandatory. Entering
  `Closed` without it permanently bricks the part (documented ST H563 case). The
  harness must set the DA regression credential before every lock, no exceptions.
- Seal wolfTrust as the RoT: `SECWM` + `WRP` over the secure region, `HDP` over
  the RoT keys, `BOOT_UBE`/`SECBOOT_LOCK` freezing the boot entry, then move
  `PRODUCT_STATE` to `Closed`/`TZ-Closed` (the H5 RDP *replacement* — not "RDP 1").
- Prove sealed: secure region unreadable/undebuggable, boots only wolfTrust.
- **Regress** to `Open` via DA (mass-erase) to prove reversibility on the dev
  board; `Locked` (permanent, no regression) is production-only — never on dev.
- Script the option-byte/product-state sequence (CubeProgrammer `-ob` +
  STM32TrustedPackageCreator). wolfBoot automates only the TZ-partitioning bytes
  (`set-stm32-tz-option-bytes.sh`), not the HDP/product-state seal — that step is
  ours.
- Evidence: lock proven (failed secure read + wolfTrust-only boot) then regression
  proven, in the hardware ledger. Needs a direct RM0481 pass on the exact
  option-byte encodings before any external-facing claim.

---

## Open questions to pin before P2.1 execution
1. Board = **Nucleo-H563ZI**? (confirms VCP + UART pins/baud.)
2. OK to **install STM32CubeProgrammer** on the box? (needed for TZEN/SECWM; pyocd
   alone is awkward for option bytes.)
3. The "TF-M that's in place" — is there a specific TF-M build to replace, or do
   we stand up a TF-M reference first and then replace it? (defines P2.5's start.)
4. Current board state — **RDP level, is TZEN already on?** (determines the safe
   option-byte sequence and whether a mass-erase/regression is needed first.)

## Risks / safety
- Option-byte/TZEN mistakes can lock the part: **SECWM before TZEN**, RDP stays 0,
  keep the unlock recipe, never RDP 2.
- Hardware iteration is slower than the emulator (flash + reset + capture per
  cycle) — keep the P2.3 smoke harness fast.
- The emulator carried two local m33mu fixes (SPSEL, ITSTATE) that are
  emulator-only and irrelevant on silicon — real hardware is the ultimate check
  that the wolfTrust behavior, not an emulator quirk, is correct.

## Definition of done
Arc A: wolfTrust boots and passes the smoke checklist on real H5 via a repeatable
`make test-hardware`, recorded in the hardware ledger. Arc B: `docs/tfm-
replacement.md` published, and a TF-M NS app runs unmodified against wolfTrust on
hardware. Old P9/#38 closed; hardware qualification recorded separately from
emulator evidence.
