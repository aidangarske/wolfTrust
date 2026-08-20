# wolfTrust task list (live tracker)

Compressed live tracker. Full historical detail (Phase-3 slices P1–P7, K1–K4,
MP1–MP4 sub-tasks, per-test batches) is archived in
[`task-history.md`](task-history.md) — read that only for provenance.

Legend: `[x]` done · `[~]` in progress / partial · `[ ]` not started.
Canonical phase definitions: `phases.md`. Evidence ledger: `validation-log.md`.

## Where we are

The FF-M / Secure-Manager runtime is built, isolated (L3), and proven under the
M33MU emulator (Arm conformance 85/4) **and** on real STM32H563 silicon. The
core/port split is enforced (adding a port = one `port/<soc>/` folder, zero core
edits, CI-guarded). The unmodified Arm PSA/FF-M conformance suite runs against
wolfTrust on the board — the TF-M drop-in proof.

## Milestones — H5 secure-manager port (`wolftrust-secure-manager-port-plan.md`)

- [x] **MP1** — first STM32H563 hardware boot (clean console, positive smoke).
- [x] **MP2** — full functional equivalence on silicon (`make test-hardware`:
  positive / restart / crossdomain).
- [x] **MP3** — reversible immutable-RoT lock proven on silicon at all three
  rungs (Provisioning / TZ-Closed / Closed → DA-cert regression → Open).
- [x] **MP4** — core/port split enforced; core has zero arch code (CI-guarded);
  port contract + adding-a-port docs.
- [x] **MP5** — TF-M drop-in proof. Unmodified Arm FF-M IPC suite reaches
  **85/4 on H563 silicon** via the new `confboot` hardware scenario, now
  **deterministic** (20/20 clean back-to-back). ST-SM/H573I-DK side-by-side
  **descoped** (owner decision). Gate flake (#83) root-caused to a Non-secure
  guest issuing SYSRESETREQ mid-suite; fixed by `AIRCR.SYSRESETREQS` (Secure
  becomes sole reset authority), plus a SysTick/PendSV priority-inversion fix
  and an SPSEL-gated HSM preempt. See Open items #A (closed).
- [x] **MP6** — docs + completion of the H5 port. Consolidated guide written:
  [`docs/stm32h5-secure-manager-guide.md`](../stm32h5-secure-manager-guide.md)
  — ties together the memory map, provisioning perimeter, the real
  Open→Provisioning→TZ-Closed→Closed lock-ladder transitions, the four hardware
  scenarios, the on-silicon 85/4 conformance run, and the silicon gotchas
  (incl. the #83 SYSRESETREQS fix), every state/result quoted from an actual
  board run under `docs/evidence/`. Ties into `port-contract.md`,
  `adding-a-port.md`, `validation-log.md`, and the two skills. The
  register/option-byte encodings are paper-verified against RM0481 and the Arm
  Cortex-M33 architecture in
  [`docs/rm0481-encoding-crosscheck.md`](../rm0481-encoding-crosscheck.md)
  (product-state codes, TZEN/BOOT_UBE, SECWM watermarks, AIRCR SYSRESETREQS),
  and a coherence pass was run across the guide + `port-contract.md` +
  `adding-a-port.md` + `competitive-edge-vs-secure-manager.md` +
  `wolftrust-secure-manager-port-plan.md`.

## Remaining implementation phases (`phases.md`)

- [~] **Phase 4 — Crypto + trusted storage** (the last big implementation
  chunk). Run Crypto / Protected Storage / ITS as isolated SPs; route key ops to
  wolfHSM via wolfPSA; enforce per-client identity + WRITE_ONCE across restarts.
  Unlocks the psa-arch-tests `dev_apis` suites → widens the drop-in claim from
  FF-M IPC to full PSA. Requirements = `WT-FFM-0044`–`0048` (framework.md);
  security thesis = keys never leave the wolfHSM vault (stronger than TF-M,
  which holds key material in the Crypto partition's own RAM). Every slice:
  host test → cross-build → M33MU gate → single-line commit → validation-log.
  - [x] **P4-S0 — requirements + capacity** (`97de1bb`): WT-FFM-0044..0048 +
    Phase 4 acceptance gate in framework.md; wolfHSM NVM directory 8→32
    objects (no flash-layout change; SECWM rules untouched).
  - [x] **P4-S1 — gated wolfHSM vault, the keystone** (`52b7239`):
    `SERVICE_VAULT` (sid 4098) behind the SPM gate — SP-only
    (`nonsecure_clients: false`) + `dependencies[]` authorization; scheduled
    PRIVILEGED coroutine (NVM mutex needs a coroutine context; MPU never
    narrowed, slot table = SVC bounds-check whitelist); backend over
    `wh_Nvm_*Checked` with SPM-stamped `(owner, uid)` namespacing and
    WRITE_ONCE → `NONMODIFIABLE|NONDESTROYABLE`; VAULTSTACK 8 KiB @
    0x30091000; port capability max_domains 8→9 (fail-closed gate caught the
    undeclared 9-domain conformance manifest). Evidence on one tree: host
    `unit/all` incl. `tests/host/vault_service` (real wolfHSM NVM over
    ramsim, 19 asserts); M33MU positive 11/11; confboot **89/85/0/4/0** with
    the vault in-image.
  - [x] **P4-S2 — ITS Secure Partition**: `SERVICE_ITS` (sid 4099, NS-facing)
    as a real unprivileged isolated SP (own 8 KiB stack @ 0x3008F000,
    narrowed MPU), storage-less — every op forwards to SERVICE_VAULT over
    SP-to-SP FF-M IPC through the SVC gate (`dependencies: [4098]` in both
    manifests); end clients namespaced at the vault via the delegated
    sub_owner (a frontend can only partition its OWN namespace). Neutral
    `src/services/storage_service.c` + PSA headers `psa/storage_common.h` +
    `psa/internal_trusted_storage.h`. Gates surfaced + fixed:
    `wt_ffm_dispatch_pending` host wake stand-in in the direct transport;
    `WT_CO_MAX` 8→12; unconditional `psa_manifest` includes in `spm_svc.c`.
    Evidence on one tree: host `unit/all` incl. full-chain
    `tests/host/storage_service` (16 asserts); M33MU positive 12/12 incl.
    **"wolfTrust ITS set/get verified"** (NS → ITS SP → vault → flash NVM,
    three domains); confboot **89/85/0/4/0**.
  - [ ] **P4-S3 — PS Secure Partition**: `SERVICE_PS` +
    `include/psa/protected_storage.h`; `psa_ps_*` incl. `create`/
    `set_extended` gated on `psa_ps_get_support()`; AES-GCM object wrap
    under a device-unique wolfHSM key (fresh nonce per write) + default-on
    NV-counter rollback protection. Reuses S2 machinery.
  - [ ] **P4-S4 — crypto key-ops via wolfPSA→wolfHSM** (deepest, Fable):
    wire wolfPSA into PARTITION_CRYPTO; route `psa_generate_key/import/
    export_public/sign/verify/{en,de}crypt` to the gated backing; keys
    NONEXPORTABLE, per-owner. Host + M33MU + on-H5.
  - [ ] **P4-S5 — security negatives (the beat-TF-M proof, Fable)**:
    cross-owner uid unreadable; WRITE_ONCE survives SYSRESETREQ on silicon;
    key owned by SP-A unusable by SP-B; a compromised Crypto SP cannot read
    the raw bytes of a key it owns; PS tamper/replay rejected. Wire the
    negative M33MU job into CI (folds in #26).
  - [ ] **P4-S6 — unlock dev_apis conformance**: real bodies for
    `pal_its/ps/crypto_function` (conformance_pal.c stubs) translating
    VAL codes into `psa_connect(SID)/psa_call`; add `dev_apis/storage`
    (s001–s017) + `dev_apis/crypto` (c001–c080) to the WT_RUN_CONFORMANCE
    build. Run under M33MU, then on H5; record pass counts + any correct
    zero-alloc skips.
- [~] **Phase 5 — Initial Attestation** — core **implemented and
  hardware-verified**: `psa_initial_attestation` st=0, DICE/measured-boot
  handoff, IAK via wolfHSM, `COSE_Sign1`/ES256 verify (challenge/identity/
  lifecycle/measurement all ok in the positive scenario). Remaining = the full
  negative-evidence / replay / key-isolation / claim-determinism test matrix.
- [ ] **Phase 6 — authenticated boot + update; portability.** Complete update /
  rollback / recovery; add a **second Cortex-M port** (proves the MP4 port kit);
  define the Cortex-A / TFA replacement boundary (cross-architecture lift).
- [ ] **Phase 7+ — OS integrations, HW/port qualification, parity + release.**

## Open items (active)

- **#A confboot gate flakiness (MP5) — CLOSED (#83).** Now deterministic (20/20
  clean, 0 SIM ERROR). Previously ~1-in-4 runs reported a single SIM ERROR.
  Root cause (found via a reset-survival SRAM black box, since
  the reset defeats both the UART log and the debugger): guest0's Zephyr
  (`CONFIG_REBOOT` + `sys_reboot`) intermittently issued a **Non-secure
  SYSRESETREQ** mid-suite, resetting the whole SoC while val had armed
  `BOOT_NOT_EXPECTED` → SIM ERROR. Fix: set `AIRCR.SYSRESETREQS` in secure init
  so a NS SYSRESETREQ can no longer reset the SoC (correct Secure-Manager
  policy). Two latent bugs fixed alongside: SysTick defaulted to priority 0 and
  preempted PendSV mid-coroutine-switch (INVPC faults) — now equal-lowest with
  PendSV; and the HSM tasklet preempt is SPSEL-gated. Proven 20/20 clean.
- **#63 m33mu upstream point-back.** Blocked on two upstream PRs (SPSEL #16 +
  ITSTATE-advance). When both merge: bump `M33MU_REF` in the runners + yml and
  drop `tests/target/m33mu-tb-sec-chain.patch` (carries both fixes locally).
- **#26 / #28 recovery + lifecycle cleanup.** Graceful SP fault recovery + wire
  the negative M33MU job into CI; collapse dead `src/lifecycle.c` into monitor.
- **#16 TEE-driver dependency** removal once purpose-built FF-M veneers suffice.
- **#62 watchdog-reset tests** — deferred (needs a WDG driver; not FF-M
  conformance).

## Test entry points

- Host: `make test` (unit/all).
- Emulator: `make test-target` (positive/restart/crossdomain/confboot) +
  `make test-conformance` (85/4) — skill `wolftrust-m33mu`.
- Hardware: `WT_H5_DOCKER_IMAGE=… make test-hardware` — skill
  `wolftrust-h5-hardware`. Physical-board evidence is a separate ledger.
