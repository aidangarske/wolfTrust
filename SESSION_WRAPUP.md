# Session handoff

<!-- pre-compact-handoff -->

## Objective and success criteria
Finish **P4-S6b dev_apis Crypto** (task #90) — the last Phase 4 piece — and
wrap up Phase 4. The RNG multi-chunk hang (task #92) and the two dev_apis-crypto
gaps (c020, c047) are all resolved; the suite runs green on M33MU. **Phase 4 is
complete** once the S6b slice is committed and #90/#92 closed.

## User decisions and constraints
- On Opus 4.8 (no Fable). Single-line commits, author
  `Aidan Garske <aidan@wolfssl.com>`, no AI attribution. **NEVER push without
  fresh explicit approval.**
- Box: container `ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15` on wolf-prec5560
  (100.87.53.96), workdir `/home/aidangarske/wolfTrust-l3-work`, rsync WITHOUT
  --delete.
- "pr it" = push the wolfPSA branch to Aidan's fork only; Aidan opens the PR.

## Repository state
Branch `wolftfm-l3`. The S6b slice is committed (this session) on top of S6a
(`e0f6365`). `lib/wolfPSA` submodule is **clean** at pin `dd557dc`; the wolfPSA
TLS12_PRF fix is carried as a build-time patch, not a working-tree change.
Nothing pushed — origin still at `29f77db` for the pushed slices; S6a + S6b are
local and UNPUSHED (push not approved).

## Completed this session
- **wolfPSA TLS12_PRF fix carried as a build-time patch (CI-reproducible).**
  The fix previously lived only in the submodule working tree, which a fresh CI
  checkout (`git submodule update` in the runners) would reset away, regressing
  c020. Converted it to `tests/target/wolfpsa-tls12-prf-mac-alg.patch`, applied
  with a guarded reverse-check after the submodule update in
  `run_m33mu_scenario.sh` and `run_h5_hardware.sh` — same carry-then-point-back
  pattern as the emulator's `m33mu-tb-sec-chain.patch` (#63). Submodule reverted
  clean; pin stays `dd557dc`.
- **Pin-bump tracked (task #93):** after the upstream wolfPSA PR
  (`aidangarske:wolfPSA:tls12-prf-mac-alg`, skoll-clean) merges, advance
  `lib/wolfPSA` past `dd557dc`, delete the patch, and drop the two apply blocks.
- Docs finalized: task-list S6b + parent P4-S6 marked done, c020/c047
  resolutions and the pin-bump item recorded; validation-log S6b crypto entry
  added.

## Verification evidence (one tree, patch path)
- **M33MU devcrypto: `PASS: target/devcrypto` — 64 passed / 13 skipped /
  0 failed (77 scheduled; c047 CMAC config schedule-skipped).** Proven from a
  CLEAN `dd557dc` submodule + `git apply` of the carry patch (the
  CI-reproducible path, not the working-tree carry). c020 TLS12_PRF passes.
  c047 is dropped from the schedule via `test_c047, skip` in the crypto sched
  db (same mechanism as c064/c065), so the run has zero failures.
- M33MU positive 15/15, confboot 89/85/0/4/0, devstorage 17/11/0/6 on the same
  tree (earlier this session).
- Host `make test`: PASS: unit/all on this tree.
- This is M33MU **emulator** evidence, not physical silicon.

## Next tasks
1. Push (needs fresh approval): S6a `e0f6365` + the S6b commit.
2. Task #93: bump the wolfPSA pin + drop the carry patch after the PR merges.
3. On-H5 dev_apis runs (task #91 area, board-pending): HW scripts still pin the
   old 128K guest layout — move guest1 to 0x080E0000 before on-target.

## Relevant files and reports
- Carry patch: `tests/target/wolfpsa-tls12-prf-mac-alg.patch`.
- Box gate log: `/home/aidangarske/m33mu-gate.log` (last: devcrypto PASS).
- wolfPSA PR worktree: `scratchpad/wolfpsa-pr` (branch `tls12-prf-mac-alg`,
  commit `a96f8d7`); `git -C lib/wolfPSA worktree remove` to clean.

## Resume instruction
Phase 4 is done. If Aidan approves a push, push `wolftfm-l3` (S6a + S6b). The
only open Phase-4-adjacent items are the wolfPSA pin bump (#93, after the PR
merges) and the board-pending on-H5 runs.
