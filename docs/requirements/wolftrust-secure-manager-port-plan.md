# wolfTrust Secure Manager replacement — the H5 port mega plan

Authoritative program plan to carry the wolfTrust STM32H5 port to completion as a
portable, OEM-owned equivalent of ST's Secure Manager. Supersedes the emulator-era
P8/P9; folds in `hardware-tfm-replacement-plan.md` (bring-up detail) and
`../competitive-edge-vs-secure-manager.md` (verified positioning).

## Vision

wolfTrust delivers the same thing ST's Secure Manager delivers — a PSA-Certified
secure runtime (Crypto / Attestation / ITS via FF-M IPC) sealed as an immutable
Root of Trust — but **portable across every Armv8-M part and with the OEM owning
the keys and the RoT**, not ST. The H5 is the reference port; the structure must
generalize to any other ST part (not port-specific) and to other vendors.

## Architecture — the "Secure Manager role" = generic core + thin port

Clean separation, enforced:

- **Core (part- and vendor-agnostic).** The SPM, FF-M IPC, PSA RoT services,
  Level-3 partition isolation model, generated-manifest authority, and the
  immutable-RoT **lock model + provisioning workflow abstraction**. This *is* the
  Secure-Manager equivalent and must contain **zero silicon specifics**.
- **Port (per silicon).** The lock primitives (H5: HDP/SECWM/WRP/BOOT_UBE/
  product-state; NXP: CMPA/CFPA; Nordic: APPROTECT/KMU; Renesas: DLM/SFP;
  Microchip: CEHL), flash/RAM map, UART + clock tree, crypto backend, and the
  wolfBoot chain. A new part or vendor = a new port implementing the same
  contract; the core is unchanged.
- **Provisioning/lock contract.** A defined interface each port fills — *seal the
  RoT immutably* → *prove sealed* → *regress (reversible, dev) / lock (permanent,
  prod)* — so the lock **workflow is uniform** across vendors while each maps to
  its native primitive.

Net: "generally support Secure Manager for any ST part that isn't port-specific"
= the core is generic; ST-part support is a port; other vendors are ports. Same
shape every time.

## Where we are (2026-08-18)

- **Core proven on M33MU emulator:** full Arm FF-M IPC suite 85/4, isolation,
  panic-reset, all host suites green (validation `49f7dad`). Emulator evidence.
- **H5 hardware, first boot:** the signed chain flashes and **wolfTrust boots on
  the real Nucleo-H563ZI** — pyocd shows the core `Running [Nonsecure]`, i.e.
  secure boot + wolfTrust + the TrustZone handoff to the guest all executed. The
  console is **baud-mismatched** (garbled UART: the real clock tree ≠ the
  emulator's fixed clock) — the first MP1 fix.

## Program — H5 reference port to completion

### MP1 — H5 bring-up: clean console + positive smoke green
- Fix the console: reconcile USART3 (the Nucleo ST-Link VCP) baud against the real
  H5 clock tree so 115200 is clean. Resolve any further hardware-only faults
  (clock/PLL, VOS/wait-states, I/D cache, GTZC-vs-SECWM, RNG) until the positive
  lifecycle prints clean.
- Exit: `run_h5_hardware.sh flash` asserts the full positive checklist on the
  board (TEE init → SERVICE_CRYPTO → SHA-256 KAT → attestation verified → guest
  done, no fault marker).

### MP2 — Full functional equivalence on silicon
- Bring the full service surface green on hardware: PSA Crypto, Initial
  Attestation, ITS, FF-M IPC round trips, and L3 isolation — the M33MU suite's
  assertions, now on real silicon.
- Repeatable `make test-hardware` smoke + a **hardware ledger** in
  `validation-log.md` (separate from emulator evidence).

### MP3 — Immutable-RoT lock model + reversible lock test + workflow
- Implement the H5 provisioning/lock workflow: **⚠️ provision the Debug-Auth
  regression credential FIRST** (skipping it makes `Closed` a permanent brick),
  seal via HDP/SECWM/WRP/BOOT_UBE + `PRODUCT_STATE` → `Closed`/`TZ-Closed`, prove
  sealed (secure region unreadable, boots only wolfTrust), then regress to `Open`
  (mass-erase). **Reversible only — never permanent `Locked` on the dev board.**
- Ship it as scripts + docs an OEM can reproduce. wolfBoot automates the
  TZ-partitioning option bytes; the HDP/product-state seal is our added step.
- Exit: lock proven + regression proven on the Nucleo, recorded. Permanent
  `Locked` explicitly deferred (production-only, likely never on this board).

### MP4 — Generalize: enforce the core/port split + the port contract
- Refactor so the core has zero silicon specifics and the H5 port implements a
  documented **port contract** (lock primitives, flash map, uart/clock, crypto,
  boot chain, provisioning/lock hooks).
- Document how each other ST part (U5/L5/H7) and each other vendor (NXP/Nordic/
  Renesas/Microchip) instantiates the contract against its native lock primitive
  (from the competitive doc's cross-vendor table). Stretch: stand up a second port
  (U5 or nRF5340) to prove the abstraction.

### MP5 — TF-M / Secure Manager drop-in proof (on silicon)
- Drop-in proof: the unmodified Arm `val` NSPE FF-M IPC conformance suite (pinned
  SHA, 85 passed / 4 skipped) runs **unmodified** against wolfTrust's secure side
  on the real H563 board — same PSA ABI, zero test edits. Delivered as a new
  `confboot` scenario in `run_h5_hardware.sh`; the panic tests reboot the chain
  with real SYSRESETREQ and val resumes off its flash boot flag (K2/K3).
- ST Secure Manager side-by-side baseline **descoped** (2026-08-19, owner
  decision): the H573I-DK comparison is dropped (the H563 cannot run Secure
  Manager and the board is not owned). The unmodified-Arm-conformance-on-silicon
  result is the drop-in evidence.

### MP6 — Docs, testing, completion
- Docs: the H5 port guide, the Secure-Manager-replacement guide, the lock
  workflow, the port contract, plus the competitive doc. A direct **RM0481** pass
  on exact option-byte encodings before any external-facing security claim.
- Testing: `run_h5_hardware.sh` smoke + the lock test + regression; wire a
  hardware smoke into CI where the lab allows.
- **Completion criteria (H5 port DONE):** wolfTrust boots and passes the full
  service suite on the Nucleo-H563ZI via `make test-hardware`; the immutable lock
  is demonstrated reversibly with a documented workflow; a TF-M NS app runs
  unmodified; the core/port split + port contract are documented; everything is
  in the hardware ledger.

## Guardrails
- **Reversible lock only on dev** (`Closed` → regress); **never permanent
  `Locked`** on the dev board — keeps the hardware reusable for testing.
- **DA regression credential provisioned before every lock** (brick-safety;
  documented ST H563 brick case).
- **Hardware evidence is a separate ledger**, never emulator-implied.
- **RM0481 verification** of bit-level option-byte encodings before external
  claims.

## Superseded / folded in
- Emulator-era **P8** (TF-M baseline) → MP5; **P9** (physical bring-up) → MP1–MP2.
- `hardware-tfm-replacement-plan.md` → MP1–MP3 bring-up + lock detail.
- `../competitive-edge-vs-secure-manager.md` → positioning reference (verified).
