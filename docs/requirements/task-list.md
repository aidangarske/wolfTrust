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
- [~] **MP5** — TF-M drop-in proof. Unmodified Arm FF-M IPC suite reached
  **85/4 on H563 silicon** via the new `confboot` hardware scenario (proven 3×).
  ST-SM/H573I-DK side-by-side **descoped** (owner decision). **OPEN:** the
  automated confboot gate is not yet deterministic — see Open items #A.
- [ ] **MP6** — docs + completion: consolidate port guide, SM-replacement guide,
  lock workflow, port contract; verify RM0481 encodings before external claims.
  Most material exists (port-contract.md, adding-a-port.md, MP3/MP5 evidence,
  the two wolftrust skills). **Recommended next.**

## Remaining implementation phases (`phases.md`)

- [ ] **Phase 4 — Crypto + trusted storage** (the last big implementation
  chunk). Run Crypto / Protected Storage / ITS as isolated SPs; route key ops to
  wolfHSM via wolfPSA; enforce per-client identity + WRITE_ONCE across restarts.
  Unlocks the psa-arch-tests `dev_apis` suites → widens the drop-in claim from
  FF-M IPC to full PSA.
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

- **#A confboot gate flakiness (MP5).** ~1-in-4 the panic-reboot loop stalls
  (infinite reboot, no report). Root cause: the K3 panic path writes its flash
  boot-flag then fires SYSRESETREQ, but on real silicon the flash write
  intermittently doesn't complete before the reset — flag lost, val re-runs,
  loops. Emulator-invisible. Fix = flush/complete flash + barrier before
  SYSRESETREQ in the panic-reset primitive; re-prove with N back-to-back suite
  runs. Deep silicon-timing work (Fable-tier).
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
