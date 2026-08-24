# Session handoff

<!-- pre-compact-handoff -->

## Objective and success criteria
Phase 4 is **code-complete**. The last piece, #95 (vault NVM init recovery from
a foreign/corrupt pool), is implemented and proven on both M33MU and H5 silicon.
Next planned work (agreed with Aidan): restructure CI into fast-per-PR vs
M33MU-nightly, and stand up a `dev` integration branch so `dev -> main` replaces
one giant `wolftfm-l3 -> master` PR, with nightly CI verifying the M33MU matrix.

## User decisions and constraints
- Single-line commits, author `Aidan Garske <aidan@wolfssl.com>`, no AI
  attribution. NEVER push/create remote branches without fresh explicit approval.
- Board: Nucleo-H563ZI on wolf-prec5560 (100.87.53.96); build in container
  ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15, flash on host; rsync WITHOUT --delete.
- CI plan: heavy M33MU scenarios move to a nightly `schedule:` trigger (minutes
  not a concern nightly); fast lane (host test + cross-build + split guard) per PR.

## Repository state
Branch `wolftfm-l3`. Pushed through the S6 slice `b6ecde1`. The #95 slice is
committed on top this session (see below), UNPUSHED. lib/wolfPSA clean at
`dd557dc` (TLS12_PRF fix carried via tests/target/wolfpsa-tls12-prf-mac-alg.patch).

## Completed this session (#95)
- Root-caused the board brick: foreign IAK -> WH_ERROR_ACCESS -> BKPT -> mute
  HardFault. Fix: lifecycle-gated recovery in wt_hsm_init/attest_init —
  reformat+re-provision only in unlocked lifecycle (rebinding the attest server
  to the fresh store); SECURED/unknown fails closed (g_wt_attest_degraded), never
  auto-wipes. attest_bootstrap + initial_attest_init boot calls degrade instead
  of panic. Geometry stays in the port (wt_hsm_flash_format). Refactored
  wt_hsm_init -> wt_hsm_bind_store (re-callable).
- Deterministic negative: WT_VAULT_FOREIGN_PROBE (+WT_VAULT_PROBE_SECURED) +
  scenarios vaultrecover/vaultrecoversec in both runners.
- Unified the HW runner to the 256K guest layout (guest1 @ 0x080E0000) for all
  scenarios (matches M33MU); added a build/flash scenario stamp guard.

## Verification evidence (all green this tree)
- H5: vaultrecover (self-heal: g_vault_reformatted=1, degraded=0, crypto 64/0);
  vaultrecoversec (fail-closed: degraded=1, reformatted=0, no trap).
- M33MU: vaultrecover (crypto 64/13/0, clean exit); vaultrecoversec (graceful,
  no brick); + regressions positive 15/15, devcrypto 64/13/0, devstorage 11/6/0.
- Host make test PASS earlier this tree; core/port split guard 0 leaks.

## Next tasks
1. Push #95 + all pending wolftfm-l3 commits (needs fresh approval).
2. CI restructure: fast-per-PR vs M33MU-nightly (schedule cron); add
   vaultrecover/vaultrecoversec to the nightly matrix. Then `dev` branch +
   trigger wiring; `dev -> main` PR. (All remote actions need approval.)
3. Fix the positive/attestation-only HW image not booting (guests not runnable;
   128K-layout bit-rot in the positive HW path; separate from #95) — new task.
4. #93 wolfPSA pin bump after upstream PR merges; #91 WRITE_ONCE on silicon.
5. Phase 5 (attestation negative-evidence matrix) is the next program phase.

## Resume instruction
Recheck git status (expect clean tree, #95 committed, ahead of origin). Continue
at Next tasks — likely the CI restructure + dev branch after Aidan approves the
push. Board is flashed with the vaultrecoversec image and idle.
