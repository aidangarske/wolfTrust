# Session handoff

<!-- pre-compact-handoff -->

## Objective and success criteria
MP5 and **MP6 are COMPLETE**. The H5 port is done through MP6 (silicon drop-in
proof + all docs consolidated and RM0481-verified). Next = **Phase 4** (Crypto +
trusted storage: Crypto/PS/ITS as isolated Secure Partitions via wolfPSA→wolfHSM,
per-client identity + WRITE_ONCE). Phase 4 is a large multi-slice implementation
milestone — scope/slice it before coding; the isolation + wolfHSM-routing slices
are Fable-tier.

## User decisions and constraints
- Single-line commits; author `Aidan Garske <aidan@wolfssl.com>`; no AI
  attribution. NEVER push without explicit approval (each push = fresh approval).
- Board: never permanent Locked (0x5C); SECWM1_END must stay 0x4F.
- Deep silicon/crypto work on `claude-fable-5`; routine on `claude-opus-4-8` high.
- Zero-allocation design (no partition heaps) — the 4 conformance skips are
  correct, not a gap.

## Repository state
- cwd `/Users/aidangarske/wolfTrust`, branch `wolftfm-l3`.
- **HEAD `ece551d`** ("Add ITS Secure Partition forwarding PSA storage to the
  gated vault") — **Phase 4 S2 DONE**. Branch **3 ahead of origin, UNPUSHED**
  (`97de1bb` S0 + `52b7239` S1 + `ece551d` S2); origin/wolftfm-l3 = `d51977f`.
- S2 one-commit evidence: host `unit/all` (23 suites incl. full-chain
  tests/host/storage_service, 16 asserts); M33MU positive **12/12 incl.
  "wolfTrust ITS set/get verified"** (NS→ITS SP→vault→flash NVM, 3 domains);
  confboot **89/85/0/4/0** (10-domain manifest). S2 shape: SERVICE_ITS 4099
  NS-facing unprivileged SP, storage-less, forwards to the vault SP-to-SP
  (deps [4098]); delegated sub_owner namespaces end clients (frontends only
  partition their OWN namespace); single-invec [hdr][data] client wire (TEE
  1+1 transport); psa/storage_common.h + psa/internal_trusted_storage.h.
  Infra fixes: wt_ffm_dispatch_pending host wake stand-in in
  wt_spm_transport_direct; WT_CO_MAX 8→12; psa_manifest includes
  unconditional in spm_svc.c; ITSSTACK 8K @0x3008F000 (RAM 428→412K).
  NEXT = **S3 PS SP** (AES-GCM wrap under a device-unique wolfHSM key +
  default-on NV-counter rollback; reuses S2 machinery) → S4 → S5 → S6 per
  the task-list Phase 4 checklist.
- Working tree: only `SESSION_WRAPUP.md` modified (this handoff).
- Phase 4 plan APPROVED at `~/.claude/plans/zany-wandering-stallman.md`
  (Q1=gated backing, Q2=grow vault; keys never leave the wolfHSM vault —
  stronger than TF-M). Slice tasks #84–#90 (#84 S0 done, #85 S1 done).
- S1 one-commit evidence (all on the final tree): host `PASS: unit/all` incl.
  new tests/host/vault_service (19 asserts, real wolfHSM NVM over ramsim);
  M33MU `PASS: target/positive` (11/11); M33MU `PASS: target/confboot`
  **89/85/0/4/0** with the vault + 9-domain manifest in the image. Logs on box:
  /home/aidangarske/m33mu-s1-{pos,conf2,pos2}.log.
- S1 shape: SERVICE_VAULT sid 4098, PARTITION_VAULT (prod domain 5 /
  conformance domain 8), nonsecure_clients=false + dependencies[] gate;
  privileged scheduled coroutine (wt_spm_vault_start / sched_add_common priv=1,
  wide table for SVC bounds-check, MPU never narrowed — NVM mutex needs a
  coroutine context); backend wt_hsm_vault.c over wh_Nvm_*Checked, ids
  0x0100..0x011F, label = magic+owner+uid+flags; WRITE_ONCE →
  NONMODIFIABLE|NONDESTROYABLE. VAULTSTACK band 0x30091000 (RAM 428K→420K).
  Defect found by fail-closed gate: port capability max_domains 8→9
  (partitions.c) — conformance manifest correctly panicked until declared.
- aidans-skills: `656f5c2` + `61c4987` still UNPUSHED (H5 skill + gotcha).

## Completed work — MP6 (this session)
- **RM0481 cross-check** → new `docs/rm0481-encoding-crosscheck.md`: every
  register/option-byte value the guide + provisioning tooling assert is verified
  against RM0481 + the Arm Cortex-M33 arch — product-state ladder
  (0xED/0x17/0xC6/0x72/0x5C), TZEN/BOOT_UBE=0xB4, SECWM (0x4F/0x7F), AIRCR
  SYSRESETREQS bit 3. External confirmations: ST app-note (0x17/0x72), SEGGER
  (TZEN 0xB4), Arm M33 UG (all AIRCR bits). "As-observed, not verified" caveat
  retired.
- **Coherence pass** across guide + port-contract + adding-a-port +
  competitive-edge + port-plan (+ architecture.md). Fixed: dead `platform_stub.c`
  refs (file was deleted in `335cdf9`) in 2 docs; wolfBoot `set-stm32-tz-option
  -bytes.sh` misrepresented as the working tool (it's superseded by
  `provisioning_ctrl.sh` — wrong SECWM, no BOOT_UBE); RDP wrongly listed for H5
  (H5 replaced RDP with PRODUCT_STATE); cross-vendor table mismatches
  (i.MX RT, SAM L11); stale MP1-frozen "Where we are" in port-plan; stale
  "outstanding core→arch leaks" list in port-contract (guard now clean, hard=0);
  dead "P2 plan" ref; superseded confboot-flake paragraph in validation-log.
- MP6 flipped to `[x]` in task-list; MP6 evidence entry added to validation-log.
- Task #71 marked completed.

## Verification evidence
- Core/port guard `tools/check-core-port-split.sh`: **hard leaks = 0, soft
  hits = 0** (confirms port-contract's rewritten "enforced" status).
- All doc link targets verified to exist on disk; no dead refs remain
  (grep-confirmed).
- (Prior) MP5: confboot 20/20 clean on H563, each 89/85/0/4/0; M33MU positive
  gate green.

## Next tasks (ordered) — Phase 4 (plan approved; tasks #84–#90)
- **#84 S0 DONE** (`97de1bb`) · **#85 S1 DONE** (`52b7239`) — see Repository
  state above for the S1 shape + evidence.
- **#86 S2 NEXT — ITS Secure Partition (Fable-tier).** New SERVICE_ITS
  unprivileged SP + neutral `src/services/storage_service.c` + client header
  `include/psa/internal_trusted_storage.h`. psa_its_set/get/get_info/remove
  mapped onto the vault protocol (wt_vault_req_t SET/GET/GET_INFO/REMOVE over
  psa_connect(SERVICE_VAULT_SID)/psa_call from the SP — SP-to-SP through the
  SVC gate). Add SERVICE_VAULT_SID to the ITS partition's dependencies[] in
  BOTH manifests. This is the first in-image vault client → the on-target
  SP→vault round-trip evidence S1 deferred. Also wire the ITS pal on the
  conformance side later (S6). **Do on `claude-fable-5`.**
- #87 S3 PS SP (AES-GCM+rollback) · #88 S4 crypto key-ops via wolfPSA→wolfHSM
  (deepest) · #89 S5 security negatives (beat-TF-M proof; WRITE_ONCE across
  reset on silicon) · #90 S6 unlock dev_apis Crypto+Storage conformance.
Each slice: host test → Cortex-M cross-build → M33MU gate → single-line commit →
validation-log. Every behavior needs its own test, not a compile check.

## Blockers and uncertainties
- Push approval outstanding: wolfTrust `3a42dbd`+`d51977f`; skills `656f5c2`+
  `61c4987`. Aidan must say "pr it" / approve push.
- Phase 4 backend decisions (above) need Aidan's direction before slice 1.

## Relevant files and reports
- New: `docs/rm0481-encoding-crosscheck.md`.
- Guide: `docs/stm32h5-secure-manager-guide.md`. Contract: `docs/port-contract.md`,
  `docs/adding-a-port.md`. Landscape: `docs/competitive-edge-vs-secure-manager.md`.
- Live tracker: `docs/requirements/task-list.md` (Phase 4 = next `[ ]`).
- Evidence ledger: `docs/requirements/validation-log.md` (MP6 entry at tail).
- Phase defs: `docs/requirements/phases.md` (Phase 4).
- Box `wolf-prec5560` (100.87.53.96): M33MU + confboot build/runner.

## Resume instruction
MP6 done and committed (`d51977f`, unpushed). Start Phase 4 by presenting the
slice plan above and getting Aidan's direction on the backend/identity decisions
and first slice — do not begin implementation blind. Do NOT push without explicit
approval.
