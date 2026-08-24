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
  - [x] **P4-S3 — PS Secure Partition**: `SERVICE_PS` (sid 4100, NS-facing
    unprivileged SP, prod domain 7 / conformance domain 10, 8 KiB stack @
    0x3008D000) — the S2 storage loop parameterized (ctx gains
    `client_flags_mask`/`vault_flags`/`caps`) so every PS request is
    forwarded with `WT_VAULT_FLAG_SEALED`. Sealing runs entirely INSIDE the
    privileged vault domain (`wt_hsm_seal.c`): AES-256-GCM under a
    device-unique key generated on first boot (NVM id 0x0120,
    NONEXPORTABLE + immutable, never enters any SP), nonce = the persisted
    monotonic rollback counter (table @ 0x0121, counter persisted BEFORE
    ciphertext so power loss can never repeat a nonce), AAD = the object
    label — so a replayed/rolled-back or cross-object ciphertext fails tag
    authentication (WT-FFM-0048). `create`/`set_extended` gated on
    `psa_ps_get_support()` = 0 (honest NOT_SUPPORTED, no silent success);
    NO_* hints accepted and recorded but never honored downward.
    `include/psa/protected_storage.h` added. Capacity gates caught + fixed:
    `WT_FFM_MAX_SERVICES` 16→20 (conformance image now carries 17
    services), port `max_domains` 10→11; shared-UART `TOTAL` asserts made
    interleave-tolerant (`expect_flat`). Evidence on one tree: host
    `unit/all` incl. `tests/host/ps_service` (23 asserts: sealed round
    trip, plaintext absent from flash at rest, rollback replay →
    INVALID_SIGNATURE, WRITE_ONCE, support gating, key+counters survive
    reboot); M33MU positive 13/13 incl. **"wolfTrust PS sealed set/get
    verified"**; confboot **89/85/0/4/0** with the 11-domain manifest.

  - [x] **P4-S4 — crypto key-ops in the gated vault** (deepest, Fable):
    key generate/import/export_public/sign/verify/{en,de}crypt as vault
    wire ops 5-11 — ECC P-256 + AES-256-GCM compute runs INSIDE the
    privileged vault (`wt_hsm_keyvault.c`; P-256 objects store
    [d][X9.63 pub], public derived once at creation; raw r||s signatures),
    keys stored SENSITIVE+NONEXPORTABLE in the S1 NVM window with usage
    policy in the label, per-owner via delegated sub_owner. Three
    independent layers between a compromised SP and raw key bytes: no
    private-export wire op exists, the storage face refuses key-flagged
    objects, and NONEXPORTABLE blocks every *Checked NVM read.
    SERVICE_CRYPTO forwards ops 1-8 SP-to-SP (PARTITION_CRYPTO gains
    `dependencies: [4098]` in both manifests — no new
    domain/partition/service). wolfPSA becomes the NS-side psa_* shim at S6
    (decided: private-key compute cannot leave the vault). Gate caught +
    fixed: ECC verify's arbitrary-point multiply
    (`sp_256_ecc_mulmod_fast_8`) overflowed the vault's 8 KiB coroutine
    stack — a REAL ARMv8-M `PSPLIM` STKOF hardware catch (CFSR
    0x00100000); VAULTSTACK grown to 16 KiB @ 0x3008F000 (ITS → 0x3008D000,
    PS → 0x3008B000, RAM 404→396 KiB). Evidence on one tree: host
    `unit/all` incl. `tests/host/keyvault` (28 asserts: generate/import/
    export_public/sign/verify + tamper refusals, usage policy,
    NONEXPORTABLE at both layers, cross-client invisibility, AES-GCM
    round trip + tamper refusal, destroy, key + public stable across
    reboot); M33MU positive 14/14 incl. **"wolfTrust key-ops sign/verify
    verified"**; confboot **89/85/0/4/0**. On-H5 hardware run pending the
    board (both runners carry the new assertions).

  - [x] **P4-S5 — security negatives (the beat-TF-M proof, Fable)**: a
    consolidated adversarial suite `tests/host/negatives` (24 asserts)
    enumerating the threat model wolfTrust's gated vault defeats and TF-M's
    Crypto-partition-RAM key storage does not, over the real
    wt_hsm_vault/keyvault/seal backends: a key owned by SP-A is unusable by
    SP-B (cross-owner, WT-FFM-0046); a compromised owner cannot read the raw
    bytes of its OWN key (NONEXPORTABLE blocks the Checked read, the storage
    face refuses the key object, and no private-export op exists); a forged
    sub_owner never crosses the SPM-stamped owner boundary (WT-FFM-0044); a
    wrong-key AES-GCM decrypt fails authentication (no cross-key oracle);
    storage/key type confusion refused both directions; a rolled-back /
    cross-owner sealed ciphertext fails (WT-FFM-0048); the internal
    KEY/SEALED flags cannot be forged from a storage client. On target: a
    new `exercise_ffm_key_negatives` guest probe proves the wrong-key
    decrypt refusal (**"wolfTrust key negatives verified"**, also the first
    on-target exercise of the key encrypt/decrypt path); the tampered-verify
    refusal already rode the positive scenario. The negative M33MU job
    (`crossdomain`, SP-domain MEMFAULT) is already in the CI matrix. Evidence
    on one tree: host `unit/all` (26 suites); M33MU positive 15/15;
    confboot **89/85/0/4/0**. Splits out the silicon-only WRITE_ONCE-across-
    SYSRESETREQ proof as a hardware-pending item (needs the board; do not
    fake board evidence). #26's fault-recovery half remains separate.

  - [x] **P4-S6 — unlock dev_apis conformance**: real bodies for
    `pal_its/ps/crypto_function` (conformance_pal.c stubs) translating
    VAL codes into `psa_connect(SID)/psa_call`; add `dev_apis/storage`
    (s001–s017) + `dev_apis/crypto` (c001–c080) to the WT_RUN_CONFORMANCE
    build. Run under M33MU, then on H5; record pass counts + any correct
    zero-alloc skips.
    - [x] **S6a Storage**: NS ITS/PS shim (`psa_storage_ns.c`) +
      `pal_its/ps_function` bodies; `WT_CONF_SUITE=storage` build plumbing;
      `devstorage` scenario + CI. M33MU **11 passed / 6 skipped / 0 failed**
      (skips = optional PS create/set_extended, get_support()=0). The suite
      found and fixed two vault defects: full-pool adds now capacity-gated
      (`wt_hsm_vault_reserve`, wolfHSM NOTBLANK poisoning) with counter-table
      headroom so sealed REMOVE always fits; storage uid 0 rejected.
    - [x] **S6b Crypto**: compile upstream `pal_crypto_intf.c` directly
      against wolfPSA (the guest psa_* provider) — no hand port;
      `WT_CONF_SUITE=crypto` build (78 scheduled test_c*; c064/c065 hash
      suspend/resume are db-excluded upstream); `pal_crypto_config.h` matched
      to the guest wolfCrypt set (ECC P-256, AES CBC/CTR/GCM/CCM, SHA-256,
      HMAC/HKDF/PBKDF2/TLS-1.2-PRF); `devcrypto` scenario + CI. Carried the
      wolfPSA `ec_key_pair` OOB-read patch. Grew guest0 to a 256K flash window
      (crypto image ~200K > 128K); guest1 moved to 0x080E0000. Fixed 7 missing
      PSA error codes in `psa/error.h` + wolfPSA's unguarded `wc_PRF_TLS`
      (needs `WOLFSSL_HAVE_PRF` + `kdf.c`). Host bench (wolfPSA's own harness,
      our config) **65 passed / 13 skipped / 0 failed** (skips = 2 RSA-only
      asymmetric + 11 optional PAKE). M33MU `devcrypto` **64 passed /
      13 skipped / 0 failed** (77 scheduled; c047 schedule-skipped, below);
      c020 fixed (below).
    - [x] **c020 TLS12_PRF was a real wolfPSA bug**: `wolfpsa_kdf_tls12_prf`
      and `_psk_to_ms` passed a `WC_HASH_TYPE_*` value to `wc_PRF_TLS`, which
      wants a `wc_MACAlgorithm` id — the enums alias (`WC_HASH_TYPE_SHA256`
      = 6 = `sha512_mac`), so `wc_PRF` picked the wrong/unbuilt hash and
      returned `HASH_TYPE_E` → PSA `GENERIC_ERROR`. Fixed with a
      `wolfpsa_prf_mac_from_alg` helper (maps only SHA-256/384/512; others
      → NOT_SUPPORTED). PR'd upstream (`aidangarske:wolfPSA:tls12-prf-mac-alg`,
      skoll-clean) and carried in wolfTrust as
      `tests/target/wolfpsa-tls12-prf-mac-alg.patch`, applied after
      `git submodule update` in the M33MU/H5 runners (guarded reverse-check,
      mirrors `m33mu-tb-sec-chain.patch`). Drop with the pin bump (below).
    - [x] **c047 is schedule-skipped, not a bug or a failure**: HMAC-key +
      CMAC-alg negative case; CMAC is compiled out, so wolfPSA returns the
      spec-permitted NOT_SUPPORTED instead of the test's assumed
      INVALID_ARGUMENT. The crypto sched db marks `test_c047, skip` (the same
      mechanism the upstream db uses for c064/c065), dropping it from the
      schedule so the run is 77 scheduled / 0 failed. The ARM test source is
      unmodified (preserves the drop-in claim); the skip is one `sed` line in
      `mk/secure-armv8m-stm32h563.mk`, not a test edit.
    - [ ] **Bump the wolfPSA submodule pin after the TLS12_PRF PR merges**
      (blocks nothing; cleanup). Once `tls12-prf-mac-alg` lands upstream,
      advance `lib/wolfPSA` past `dd557dc` to the merged commit, delete
      `tests/target/wolfpsa-tls12-prf-mac-alg.patch`, and drop the two
      `git -C lib/wolfPSA apply` blocks in `run_m33mu_scenario.sh` and
      `run_h5_hardware.sh`. Same carry-then-point-back pattern as #63.
    - [x] **Max-size HSM response overflowed the CMSE slot** (found by
      c017, task #92): the transport slot data area was sized to hold
      `WOLFHSM_CFG_COMM_DATA_LEN` but must hold the whole wolfHSM comm
      packet — the 8-byte `whCommHeader` rides in front of the payload in
      the same slot. A max-size RNG chunk (cap = COMM_DATA_LEN − 16) yields a
      response whose payload exactly fills COMM_DATA_LEN, so
      `wh_CommServer_SendResponse` hands the transport `8 + COMM_DATA_LEN`,
      `wt_cmse_transport_send` rejects it `WH_ERROR_BADARGS`, and the
      tasklet's silent error branch parks with no response → the client
      spins `WH_ERROR_NOTREADY` forever. Not a re-dispatch race: any
      max-size chunk wedges (c017's 512/1000 both open with one). Fix:
      COMM_DATA_LEN 376→368 so `sizeof(whCommHeader) + COMM_DATA_LEN` = 376
      = one slot's data area, and correct the `_Static_assert` in
      `cmse_transport.c` to include the comm header (setting 376 now fails
      the build instead of hanging). New RNG single-shot cap = 352 B; 512/
      1000 chunk cleanly. Buffer stays 768 B (already ends at the sram1
      boundary). Follow-up: the tasklet's silent error swallow + the client
      loop's missing timeout are a robustness gap — candidate wolfHSM
      upstream report (with S6a's NOTBLANK).
    - [ ] **Magic psa_store return codes** (logged 2026-08-21): wolfPSA's
      `psa_store` contract uses bare ints (0 ok, -4 not-found, other = fail)
      and `psa_key_storage.c` hardcodes `== -4`; wolfTrust's
      `psa_store_stub.c` now returns -4 for the volatile-only "not found".
      Replace the literal with a named constant (e.g. `WOLFPSA_STORE_NOTFOUND`)
      in a shared header — candidate for an upstream wolfPSA cleanup.
    - [x] **On-H5 runs of both suites — DONE (2026-08-24, real silicon)**:
      `devcrypto`/`devstorage` scenarios wired into `run_h5_hardware.sh`
      (scenario-conditional 256K guest layout, guest1 at 0x080E0000; other
      scenarios keep the proven 128K layout). Results match the emulator
      exactly: crypto **64/13/0 (77 scheduled)**, storage **11/6/0 (17)**
      twice, 0 SIM ERROR. Two silicon-only findings, both root-caused with
      the debugger and fixed in the runner:
      1. A vault pool written by an older firmware generation made wolfHSM
         NVM init fail (`WH_ERROR_ACCESS`) and the secure image BKPT-trap
         into a mute HardFault pre-UART (emulator never sees it — blank
         flash every run). Runner now guarantees a blank pool; the real
         defect (init must reformat/quarantine, never dead-trap) is its
         own tracked item below.
      2. Erase-while-running + double-reset ordering: erasing the vault on
         a live target let the old firmware's cached wolfHSM state rewrite
         the pool pre-reset (s001 stale-UID failures), and the post-flash
         cleanup reset landed mid vault-format, tearing a flash write that
         s003's remove-all later tripped (SIM-ERROR reboot). Fix: after
         flashing, `reset halt` → erase vault+boot-flag while halted →
         boot exactly once. Also added a build/flash scenario stamp so
         mismatched images fail fast.
    - [x] **Vault NVM init recovery (silicon robustness) — DONE (#95)**: the
      boot-time IAK provisioning now recovers from a foreign/corrupt pool
      instead of the BKPT dead-trap. On a pool whose IAK slot is held by a
      NONMODIFIABLE object (or is otherwise unreadable), the recovery is
      **lifecycle-gated**: only the unlocked development states
      (ASSEMBLY_AND_TEST / PSA_ROT_PROVISIONING) may reformat + re-provision
      (rebinding the attest server to the fresh store); a SECURED or unknown
      lifecycle **never auto-wipes** — it fails closed (attestation degraded,
      `g_wt_attest_degraded`, no trap), so WRITE_ONCE storage and the sealed
      key survive. Geometry stays in the port (`wt_hsm_flash_format`). A
      deterministic `WT_VAULT_FOREIGN_PROBE` build forces the foreign-pool
      ACCESS; two scenarios (`vaultrecover` / `vaultrecoversec`) prove both
      halves on **H5 silicon** (self-heal: crypto 64/0, `g_vault_reformatted=1`;
      fail-closed: `degraded=1`, `reformatted=0`) **and M33MU** (crypto 64/13/0
      after self-heal; graceful no-brick fail-closed).


- [~] **Phase 5 — Initial Attestation** — positive path **implemented and
  hardware-verified**: `psa_initial_attestation` st=0, DICE/measured-boot
  handoff, IAK via wolfHSM, `COSE_Sign1`/ES256 verify (challenge/identity/
  lifecycle/measurement all ok in the positive scenario), but on M33MU/H5 only —
  host attestation was stubbed. Remaining = the five stop-gate buckets
  (`phases.md:116-118`) as real evidence. Beat-TF-M angle = IAK key-isolation
  (the signing key never leaves the wolfHSM vault). Slices:
    - [x] **P5-S2 (buckets 2+3 core): host COSE_Sign1 real ES256 sign→verify.**
      New `tests/host/attestation/` drives the production `wt_attest_cose_*`
      seam with a real P-256 signer, verifies via wolfCOSE against the IAK
      public key, and rejects tampered signature / tampered body / truncated
      token / wrong key / undersized buffer / bad flags / null signer (15/15
      host checks). Added to `UNIT_SUITES` → runs in `make test` and as its own
      `Unit tests / attestation` CI check.
    - [ ] **P5-S1 (bucket 1): deterministic EAT claims** — byte-stable golden
      vector for the encoded claim set; decide profile / boot-seed claims.
    - [ ] **P5-S3 (bucket 3): negative evidence** — tampered measurement,
      garbage DICE handoff (`wt_initial_attest_init` refusal), oversized
      challenge (>64) runtime rejection.
    - [ ] **P5-S4 (bucket 4, Fable, the beat-TF-M proof): IAK key-isolation** —
      a foreign owner/partition cannot invoke `wt_hsm_attest_sign` or read
      `WT_HSM_ATTEST_KEY_ID`; IAK export is public-only (NONEXPORTABLE).
    - [ ] **P5-S5 (bucket 5): replay + lifecycle** — different-challenge →
      different-token differential; boot-seed decision; real lifecycle
      transition (not the single fixed `0x1000`).
    - [ ] **P5-CI:** `attestneg` M33MU scenario + `ci:attestneg` label +
      workflow markers, mirroring the `vaultrecover` pattern.


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
