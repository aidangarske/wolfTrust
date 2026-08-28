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


- [x] **Phase 5 — Initial Attestation — COMPLETE (2026-08-25).** All slices
  below have real evidence: production tagged profile-2 token; ARM `test_a001`
  green on host, M33MU (shim + reference QCBOR), and H563 silicon (both
  backends); deterministic claim-set golden vector; handoff/challenge/tamper
  negatives; replay + lifecycle binding; IAK key-isolation at the wolfHSM
  enforcement layers (beat-TF-M — the signing key never leaves the vault); and
  the on-target `attestneg` scenario in CI. All host suites also green under
  gcc/clang and ASan/UBSan. Slices:
    - [x] **P5-S2 (buckets 2+3 core): host COSE_Sign1 real ES256 sign→verify.**
      New `tests/host/attestation/` drives the production `wt_attest_cose_*`
      seam with a real P-256 signer, verifies via wolfCOSE against the IAK
      public key, and rejects tampered signature / tampered body / truncated
      token / wrong key / undersized buffer / bad flags / null signer (15/15
      host checks). Added to `UNIT_SUITES` → runs in `make test` and as its own
      `Unit tests / attestation` CI check.
    - [x] **P5-S1 (bucket 1): deterministic EAT claims — DONE.** New
      `tests/host/attestation_golden/` pins the production claim set against an
      embedded 216-byte golden vector (fixed RFC 6979 P-256 IAK so the UEID is
      deterministic, fixed handoff/challenge): tokens byte-identical up to the
      ECDSA signature, claim bytes match the golden, 12/12 (gcc/clang/ASan).
      Profile decision pinned by test: profile-2 claim set, no boot-seed (268).
      Regenerate after an intended claim change with
      `make run EXTRA_CFLAGS=-DWT_GOLDEN_GEN`. In `UNIT_SUITES`/CI.
    - [x] **P5-S3 (bucket 3): negative evidence — DONE.** New
      `tests/host/attestation_negatives/` (19/19, gcc/clang/ASan): NOT_READY
      before any handoff; NULL/unknown-hash/truncated-measurement handoffs
      rejected without arming the state; challenge 0/31/33/65 + NULL rejected
      at runtime; a good token then fails against a different measurement,
      a different lifecycle, and with one in-token measurement byte flipped
      (ES256 catches it). In `UNIT_SUITES`/CI. On-target attestneg = #102.
    - [x] **P5-S4 (bucket 4, the beat-TF-M proof): IAK key-isolation — DONE.**
      New `tests/host/attestation_iak/` (14/14, gcc/clang/ASan) provisions the
      IAK exactly as `wt_hsm_attest_generate_key` (same server config, keygen
      message, lockdown flags, commit id) on ramsim, then proves at the real
      wolfHSM layers: sign works + only the 65-byte public point exports (I1);
      raw `WH_KEY_EXPORT` refused (I2); Checked NVM read refused (I3);
      destroy/re-provision refused, original still signs (I4); a guest client
      identity can neither sign with nor even see the IAK — namespace
      isolation — while the attest identity still signs (I5). TF-M keeps this
      key in partition RAM; wolfTrust never lets it leave the vault.
    - [x] **P5-S5 (bucket 5): replay + lifecycle — DONE.** New
      `tests/host/attestation_replay/` (13/13, gcc/clang/ASan): different
      challenges → different tokens, each verifying only under its own
      challenge (replay of an old token under a fresh nonce is rejected both
      directions); a real lifecycle transition 0x1000→0x3000 via a new
      handoff — the secured token is rejected as development and vice versa.
      Boot-seed decision pinned in P5-S1 (no claim 268). In `UNIT_SUITES`/CI.
    - [x] **P5-CONF (ARM drop-in proof): unlock `dev_apis/initial_attestation`
      (`test_a001`)** from the pinned psa-arch-tests (rev `e17d294`) — DONE:
      host + M33MU both ways + H5 silicon both ways, all green.
      - [x] **CBOR backend done + interop proven.** `test_a001`'s val needs QCBOR;
        wolfTrust uses wolfCOSE. Built a wolfCOSE-backed `qcbor.h`/`qcbor_shim.c`
        (`tests/conformance/qcbor-shim/`) so ARM's `val_attestation.c` stays
        unmodified. Proven on host (`tests/host/qcbor_shim/`, 10/10) parsing a
        real wolfCOSE `COSE_Sign1` (array-of-4 + tag 18 + claims map) and byte-
        exact Sig_structure encode; also builds the SAME test against the
        reference QCBOR lib (fetched test-only, `tests/upstream/fetch_qcbor.sh` +
        `qcbor.rev`, gitignored) — a wolfCOSE token verifies under both. Both run
        in CI: `Unit tests / qcbor_shim` (in UNIT_SUITES) + `Unit tests / CBOR
        interop (wolfCOSE and QCBOR)`. Green host + gcc + ASan/UBSan.
      - [x] **Target integration DONE.** `attestation` suite block in
        `guest0_psa/CMakeLists.txt` compiles val_attestation + `test_a001` + the
        two unmodified upstream attestation PALs against wolfPSA; new
        `port/stm32h563/conformance/pal_attestation_config.h` (COSE constants,
        `CRYPTO_VERSION_BETA3`; NO `PLATFORM_OVERRIDE_ATTEST_PK` — the IAK is
        device-generated, so `conformance_pal.c` bridges
        `tfm_initial_attest_get_public_key` to the runtime IAK); testlist gen in
        `mk/secure-armv8m-stm32h563.mk`; NS client fix: zero/NULL token buffer →
        `INVALID_ARGUMENT` (check 8). Scenarios `devattest`/`devattestqcbor` in
        the runner + M33MU CI matrix + `ci:` labels.
      - [x] **Production token change DONE (`26bd175`).** `get_token`/`get_token_size`
        now emit a **tagged** COSE_Sign1 (flags `0u`; val's `IsTagged(18)` gate) and
        the SW component carries a **signer_id** (label 5, `SHA-256("wolfBoot")`),
        bumping the map `3u→4u` so profile-2 `mandatory_sw_components==2`. wolfCOSE
        already tag-capable both ways (encode gated by `WOLFCOSE_SIGN1_UNTAGGED`;
        verify auto-detects tag 18) — no wolfCOSE change. Evidence: new host suite
        `tests/host/attestation_token/` drives the real production encoder + the
        production guest verifier (26/26, gcc/clang/ASan) + M33MU `positive`
        (`token_len=291`, `COSE_Sign1 verified`, `verify=0 … cose=ES256`).
      - [x] `test_a001` on M33MU **both ways**: `PASS: target/devattest` (shim,
        16/16 checks, TOTAL 1/0) + `PASS: target/devattestqcbor` (reference
        QCBOR, TOTAL 1/0), one tree, profile 2 — see validation-log.
      - [x] `test_a001` on H5 **silicon** both ways: `PASS: hardware/h5/devattest`
        (shim, rerun after a one-time first-boot 0/0 transient on the image
        switch) + `PASS: hardware/devattestqcbor` — both `Result=Passed`,
        TOTAL 1/0, profile 2, no fault markers. P5-CONF COMPLETE.
    - [x] **P5-CI — DONE.** `attestneg` M33MU scenario (production image +
      `WT_ATTEST_NEG_PROBE` guest probe over real FF-M IPC): oversized
      challenge and zero token buffer rejected `st=-135`, tampered token and
      lifecycle mismatch refused by the guest verify, positive lifecycle still
      green, clean exit — `PASS: target/attestneg`. In the M33MU CI matrix +
      `ci:attestneg` label. H5 variant rides #96 (positive HW image fix).
      **PHASE 5 COMPLETE.**


- [x] **Phase 6 — authenticated boot, runtime verification, and update**
  (`phases.md:120-126`). **COMPLETE (2026-08-26): the full emulated
  boot-and-update gate passes (S6 `bootupdate` green).** Scoped to the normative
  source: authenticated/measured guest launch, firmware rollback + recovery,
  runtime verification, and a PSA Firmware Update service. **Second Cortex-M port + Cortex-A/TFA boundary moved
  to Phase 8** (decided 2026-08-25). Stop = the full emulated boot-and-update
  gate passes. Guest auth = **hash-pin + monotonic version** (re-hash each guest
  at dispatch vs a manifest-pinned digest + min-version; fail closed). Plan:
  `~/.claude/plans/zany-wandering-stallman.md`. Slices:
    - [x] **S0 (baseline): Phase-6 reqs + collapse dead `src/lifecycle.c` into
      `src/monitor.c` (#28) — DONE.** Added `WT-SYS-0013` (runtime
      re-measurement) and a framework Phase 6 block (`WT-FFM-0049..0052`,
      `WT-FWU-0001..0003`) + acceptance gate. Single-sourced the live restart
      engine: extracted `wt_restart_policy_evaluate` (`src/restart_policy.c`,
      byte-identical to the old inline `wt_restart_guest` math), deleted dead
      `src/lifecycle.{c,h}` from the secure build, repointed
      `tests/host/lifecycle/` at the real predicate. Host `make test` green
      (`unit/lifecycle` 25, gcc/clang + ASan/UBSan); M33MU `PASS: target/restart`
      (guest restarted 3x then FAULTED, banners 4/4) — extracted engine proven
      on-target.
    - [x] **S1 (keystone, Fable): authenticated guest launch (WT-SYS-0002 /
      WT-FFM-0049) — DONE on M33MU.** Manifest carries the policy
      (`launch_required`+`launch_min_version` per domain, schema + conformance
      regen + fixture); the pinned digests ride in a `.wt_guest_meas` slot the
      image-assembly patcher (`tools/measure/patch_guest_digests.py`) stamps
      into `wolftrust.bin` BEFORE wolfBoot signs, so the pins share the image
      root of trust (patch-then-sign; forced by build order — guests link
      against the secure implib). `wt_monitor_init` + every relaunch re-hash
      the guest image (`wt_guest_verify_image`, SHA-256 + constant-time pin +
      version floor) and fail closed to FAULTED/quarantine. Verified digests
      become lean per-guest SW components (measurement+signer_id) in the
      attestation token; the expected-measurement assertion moved into the
      harness (breaks the guest↔image circular build dependency;
      `wt_attestation_verify_ex` report-only mode). Host `guest_verify` 30/30
      + 34-suite `unit/all` green (golden vector byte-identical); M33MU
      `PASS: target/positive` (token measurement == harness-computed wolfBoot
      measurement), `PASS: target/authneg` (corrupt guest0 refused, guest1
      survives), `PASS: target/devattest` (a001 green on the 3-component
      token). Defect found by the gate: the const slot accessor const-folded
      the unpatched marker — fixed with a volatile load. `authneg` in the CI
      matrix + `ci:authneg`.
    - [ ] **S1-HW: repeat authenticated-launch evidence on H563 silicon**
      (devattest + authneg via the updated `run_h5_hardware.sh` patch-then-sign
      flow) — hardware-pending, board session (rides with #91/#96).
    - [x] **S2 (Fable): firmware anti-rollback (WT-FFM-0050) — DONE on M33MU.**
      `boot_handoff.image_version` finally consumed: `wt_hsm_rollback_enforce`
      runs on the boot stack after `wt_hsm_init` (NVM live) and before the
      first dispatch, checking the image version + every S1 pinned guest
      version against monotonic floors in a new vault NVM object
      (`WT_HSM_ROLLBACK_TABLE_ID` 0x0122, WT-FFM-0048 idiom). Below-floor
      image ⇒ all guests quarantined (new `wt_monitor_quarantine_guest`);
      below-floor guest ⇒ that guest only; floors advance on an accepted boot
      (NVM write only when changed). Lifecycle-gated like #95: assembly/
      provisioning bypass, SECURED/unknown enforce. Neutral predicate
      `src/rollback.c` + host `rollback` suite 51/51 (35-suite unit/all);
      M33MU `PASS: target/positive` (floor advance, no regression) +
      `PASS: target/rollbackneg` (probe arms floor above the running version,
      SYSRESETREQ, second boot refused fail-closed 0x7D — floor proven
      persistent across reset; probe forces SECURED like vaultrecoversec).
      `rollbackneg` in the CI matrix + `ci:rollbackneg`; the inline per-guest
      CI job reordered to the S1 patch-then-sign flow. Silicon rides #113.
    - [x] **S3 (Fable): graceful SP fault recovery + CI (#26) — DONE on M33MU.**
      A faulted Secure Partition is now recovered without resetting the world
      (WT-SYS-0008 / WT-FFM-0017): the fault handler only marks the coroutine
      dead and pends recovery; the SPM dispatch path then runs the neutral
      engine (`src/sp_recovery.c`: locks released via `wt_hsm_release_locks`,
      pinned clients failed with a defined error via
      `wt_ffm_fail_partition_messages` — which also drains the dead
      partition's queues and deasserts its signals — stack scrubbed, coroutine
      restarted in place via `wt_co_reinit` under the manifest
      `restart_policy` budget; NEVER/PLATFORM or exhausted budget escalate).
      Recovery deliberately runs on the bootstrap thread, never in handler
      mode. Host: new `sp_recovery` suite (300 checks: budget decision,
      ordered orchestration incl. failed-restart downgrade, 50x in-place
      reinit with slot/domain identity preserved) + `ffm` fault-unblock case —
      36-suite `unit/all` green, gcc/clang + ASan/UBSan. M33MU:
      `PASS: target/spfaultneg` — the crypto SP faults once
      (`WT_SP_FAULT_PROBE` out-of-domain read), the pinned client unblocks
      with -145, the RESTARTED SP serves the later key-ops, ITS/PS/HSM/
      attestation all green through a clean BKPT exit, no platform reset —
      plus `PASS: target/positive`, `target/crossdomain`, `target/confboot`
      (85/0/4) regressions. `spfaultneg` in the CI matrix + `ci:spfaultneg`.
      Three defects found by the gate: (1) `wt_secure_fault_dispatch` blamed
      the scheduled NS guest for Secure-Thread faults — `SecureFault_Handler`
      now routes secure-frame Thread faults to the tasklet recovery entry
      (also covers the M33MU delivering secure MPU faults through the
      SecureFault vector — emulator defect #3, stale `securefault_pending`,
      tracked with the #63 patch family); (2) force-completed messages left
      queued kept the service signal asserted (restart spin); (3) the fault
      path left `g_wt_co_pendsv_target` stale. Collapses #26. Silicon rides
      #113.
    - [x] **S3-R: fix pre-existing `target/restart` regression (task #114) —
      virtual-SysTick injection inside the dispatch window.**
      `wt_virtual_systick_restore_arriving` armed the guest SysTick and
      pended owed ticks (`PENDSTSET`) from `wt_platform_prepare_guest_return`
      — after the `VTOR_NS` write but before the NS bank restore. With
      PRIS=0 the injected tick preempts the secure dispatcher right there,
      vectoring through the ARRIVING guest's table while stacking on the
      DEPARTING guest's live MSP_NS; the arriving guest's MPU denies the
      cross-window push (derived NS HardFault, rotation dead at 2/4
      banners). Proof from the failure log itself: the faulting SysTick
      context's `EXC_RETURN=0xFFFFFFD0` (ES=0, Mode=Handler, S=1) shows an
      NS tick that preempted secure HANDLER code. S1's in-handler relaunch
      hash guarantees owed ticks at every relaunch dispatch — hence the
      S0-green/S1-red bisect. Fix: the arm/inject step is deferred to
      `wt_virtual_systick_arm_arriving()`, called from the tail of
      `wt_exception_return_ns_msp` and `wt_jump_to_ns` once
      MSP/PSP/CONTROL_NS are restored. Evidence: `PASS: target/restart`
      (4/4 banners, 3 restarts then FAULTED) + regressions
      positive/spfaultneg/confboot all green on the same tree. The
      same-window peripheral-IRQ unmask hole is tracked as #116.
    - [x] **S4 (Fable, deepest): PSA Firmware Update service (WT-FWU-0001..0003).**
      Neutral state machine `src/services/fwu_service.c` + client API
      `include/psa/update.h` (`psa_fwu_query/start/write/finish/install/abort`),
      driven through a backend seam so the host test uses a RAM mock and the
      target uses real flash. New privileged `SERVICE_FWU` SP (domain 8, SID
      4101) mirroring the vault: stages a candidate into the real wolfBoot
      update partition (`0x0C100000`) via `hsm_flash.c` with lazy per-sector
      erase + read-back verify, and arms an update request. Stack band carved
      (`memory_map.h`/`secure.ld`/cap 8→9); FWU is production-only, excluded
      from the conformance manifest by the ingester so the 85/4 layout is
      unchanged. Evidence: host `fwu_service` 33 checks (gcc/clang/ASan, every
      negative — bad-state/oversize/misaligned/rolled-back/storage-failure/
      abort-restores) + M33MU `PASS: target/fwustage` (NS guest drives
      start/write/finish/install over IPC, candidate lands in update-partition
      flash and verifies, write-before-start refused) + `positive` + `confboot`
      (85/4) regressions green on one tree. `fwustage` in the CI matrix +
      `ci:fwustage`. The wolfBoot trailer-exact arm + reboot→swap→gated launch
      ride S6.
    - [x] **S5 (Fable): runtime verification (WT-FFM-0052 / WT-SYS-0013).**
      On-demand post-boot re-measurement: neutral `wt_runtime_verify_decide` +
      `wt_runtime_verify_should_quarantine` (guest_verify.c) reuse the S1
      SHA-256 pin check; `wt_runtime_verify_guest` (monitor.c) does the
      window+record lookup and, on any mismatch, drives the domain through the
      fail-closed `wt_monitor_quarantine_guest` path — a tamper after launch is
      caught instead of trusting the boot-time measurement. Guest-domain only
      (SPs have no pinned-digest store — split if SP coverage is wanted).
      Evidence: host `runtime_verify` 7 checks (gcc/clang/ASan: untampered OK,
      tampered/rolled-back/shrunken/no-record fail closed, no-launch-policy
      passes) + M33MU `PASS: target/remeasureneg` (a secure probe re-measures
      guest0 clean, then tampers its flash window in place — secure MPU dropped
      for the single privileged-RO program — and the on-demand re-measure
      catches it and quarantines, `[BKPT] imm=0x6c`, no fault) + `positive` +
      `confboot` (85/4) green on one tree. CI matrix `remeasureneg` +
      `ci:remeasureneg`.
    - [x] **S6 (Fable + silicon): full boot-and-update gate.** The FWU backend
      arm now writes wolfBoot's real WRITEONCE update trigger
      (`wt_fwu_wolfboot_arm_trailer`, host-tested byte-exact). `bootupdate`
      scenario: one image signed v1@1 / v2@2 (version in the hashed header ->
      distinct measurement), v2 pre-staged in the UPDATE partition, a
      version-gated arm probe (v1 only) arms + reboots, wolfBoot swaps v2 in and
      boots it. Evidence: host `fwu_service` +7 encoder checks (gcc/clang/ASan) +
      M33MU `PASS: target/bootupdate` (no fault, token reports v2's measurement
      not v1's, clean exit) + `positive` + `fwustage` + `confboot` (85/4) green on
      one tree. CI matrix `bootupdate` + `ci:bootupdate`. H563 silicon:
      `PASS: hardware/h5/bootupdate` — SWD read-back of the boot-partition header
      == v2, != v1 after the armed reset (wolfBoot physically swapped on real
      silicon). Closes Phase 6 (`phases.md:126`).


- [ ] **Phase 7 — OS integrations** (`phases.md:128-134`): OS-neutral NS client
  ABI + thin Zephyr/FreeRTOS integrations; same PSA/isolation tests from both.
  Stop = both OS gates pass. **Security decision LOCKED (2026-08-26, most
  secure): every non-secure client call goes NS -> FF-M SPM -> SERVICE_*
  partition; the raw HSM-CMSE bypass is retired from production so the SPM is
  the single mediated gatekeeper; FreeRTOS reaches full PSA parity.**
    - [x] **S0**: Phase 7 reqs seated — `WT-SYS-0014` (one OS-neutral NS client
      ABI, SPM the single mediated path) in system.md; `WT-FFM-0053` (OS-neutral
      client core, met by S1/S2, `cb87ae5`), `WT-FFM-0054` (single mediated path,
      open S3/S6), `WT-FFM-0055` (both-OS parity, open S4/S5) + the Phase 7
      acceptance gate in framework.md. Docs-only, no runtime gate; sliced S1–S6
      plan below.
    - [x] **S1**: Extracted the OS-neutral FF-M client core (`psa_connect/call/
      close/framework_version/version`) into portable `src/client/psa_ffm_client.c`
      (calls `WolfTrust_FFM_*` veneers, zero OS headers) + `tests/host/psa_ffm_client`
      (8 checks, gcc/clang/ASan); core/port split guard clean. `WT-FFM-0053`.
      Evidence: `cb87ae5` (pushed both remotes, CI green).
    - [x] **S2**: guest0 FF-M now routes through the neutral core, not the Zephyr
      TEE subsystem. main.c `wt_tee_invoke` shim -> psa_connect/call/close (all 35
      call sites); CMakeLists links psa_ffm_client.c; the TEE driver dropped its
      duplicate psa_framework_version/version + dead FF-M cases (keeps HSM
      poll/cancel); conformance_pal.c dropped its duplicate psa_* so the val NSPE
      uses the neutral core too. **Closes #16.** Evidence: M33MU
      `PASS: target/positive` (guest0 FF-M) + `PASS: target/confboot` (85/4/0, val
      NSPE psa_call) on the box.
    - [x] **S3 (Fable, deepest)**: FreeRTOS guest1 now reaches secure crypto only
      through the FF-M SPM. New `WT_CRYPTO_OP_RANDOM` on SERVICE_CRYPTO forwards to
      a vault-domain RNG (`WT_VAULT_OP_RANDOM` + keyvault `random`); guest1 drops
      wolfPKCS11/wolfHSM-client/raw glue for a wolfPSA front-end + the neutral
      client (`wt_ffm_crypto_random`), DRBG seed crossing via the SPM. Coverage:
      SHA-256 already covered, RNG was the one gap (added); no ITS/PS needed
      (volatile keys). Budget: transport swap, no window grow. **WT-FFM-0054.**
      Evidence: host `crypto_service`/`psa_ffm_client` (+10 checks, incl. a
      boundary crossing-counter and vault-route ablation) + M33MU `PASS:
      target/positive` (5 guest1 FF-M markers) / `spfaultneg` / `authneg`;
      disasm proof that guest1 branches ONLY the WolfTrust_FFM_* veneers, never
      the raw HSM veneers; build-time `nm` guard fails if any `wh_Client_*`
      returns.
    - [x] **S4**: Both-OS PSA gate — new `bothpsa` M33MU scenario asserts the
      SAME three operations from guest0 (Zephyr) AND guest1 (FreeRTOS) in one
      boot: mediated SERVICE_CRYPTO SHA-256 KAT, `psa_generate_random`, and
      `psa_hash_compute` KAT. Wired into the M33MU matrix + `pr-m33mu-select`
      (`ci:bothpsa`). Evidence: `PASS: target/bothpsa` (8/8 checks) on the box.
      **WT-FFM-0055.**
    - [x] **S5**: Both-OS isolation gate — guest1 `run_ffm_negatives` (forged
      handle, oversized input vector, unknown-SID connect) mirrors guest0's proven
      rejections through the neutral client; new `bothiso` M33MU scenario asserts
      the SPM rejects forged-handle + oversized-vector from BOTH OSes, refuses the
      bad SID, and neither guest faults (guest1 keeps serving mediated
      SERVICE_CRYPTO). CI matrix + `pr-m33mu-select` (`ci:bothiso`). Ran on
      `claude-opus-4-8` — replicating proven patterns, not deep work. **WT-FFM-0055
      (isolation half).** Evidence: `PASS: target/bothiso` (8/8 checks) on the box.
    - [ ] **S6**: Retire the raw HSM-CMSE bypass entirely — NO gate, NO mixed
      transport (decision LOCKED 2026-08-27, plan `/tmp/wolftrust-s6-plan-2026-08-27.md`):
      the wolfHSM server becomes the ONE crypto backend for every algorithm,
      reached only through a new `SERVICE_HSM` relay partition (the wolfHSM
      client's pluggable transport swaps from direct CMSE to `psa_call`). The
      second keystore (`wt_hsm_keyvault`) and SERVICE_CRYPTO's ad-hoc handlers
      retire with it. Closes WT-FFM-0054; flips the Phase 7 header. H5 rides
      Phase 8. Sub-slices:
        - [x] **S6a**: host proof of the relay transport — real wolfHSM client
          over `psa_call` to a real wolfHSM server through the in-process FF-M
          runtime. New neutral `src/services/hsm_relay_service.c` (opaque-packet
          dispatch, pluggable submit seam, fail-closed default, 512 B bound) +
          `src/client/hsm_psa_transport.c` (the whTransportClientCb whose Send is
          one synchronous mediated psa_call — blocking wrappers complete with NO
          NOTREADY spin, retiring the multi-chunk hang class). Evidence:
          `tests/host/wolfhsm_relay` 19/19 (CommInit, blocking RNG, 1000 B
          multi-chunk RNG, ECC keygen+sign+verify through the relay, fail-closed
          without the hook, client- and relay-side bounds) under gcc/clang +
          ASan/UBSan; split guard clean.
        - [x] **S6b**: manifest swap — PARTITION_CRYPTO/SERVICE_CRYPTO(4097) →
          PARTITION_HSM/SERVICE_HSM(4102) reusing domain 4, deps dropped (the
          relay talks to the monitor, not the vault); both manifests; generator
          verified (`PARTITION_HSM_ID 4`, `SERVICE_HSM_SID 4102`). Boot core now
          registers `wt_hsm_relay_dispatch` (fail-closed until the platform
          submit lands); secure mk + host suite Makefiles compile the relay;
          spm/ffm_veneer/psa_ffm_client fixtures updated. Host `unit/all` +
          conformance host-subset green. Target scenarios intentionally red
          until the S6c/S6d arc completes.
        - [x] **S6c**: secure relay — `wt_hsm_relay_submit` in `wt_hsm.c` maps
          the SPM-stamped caller to its guest server and pumps
          `wh_Server_HandleRequestMessage` inline over a per-guest secure
          capture buffer in monitor RAM (no tasklet wake, no NS-RAM CSR);
          every guest server rebinds from `wt_cmse_transport_cb` to the
          capture transport (`wt_hsm_guest_init_relay`); PARTITION_HSM runs
          as a scheduled PRIVILEGED coroutine (`wt_spm_hsm_start`, vault
          model) whose loop is `wt_hsm_relay_dispatch` over the SVC
          transport. Fault re-home: a relay fault lands in the existing
          graceful SP recovery (pinned client fails with -145); the probe is
          now an undefined instruction (privileged code cannot
          MemManage-fault on an out-of-domain read), and the old CMSE
          fault-notify goes unused on the relay path. Boot attest tasklets
          kept for IAK provisioning. Evidence: host `unit/all` green, split
          guard clean; box cross-build links for production,
          `WT_SP_FAULT_PROBE=1`, and `WT_CONFORMANCE=1` (scenario layout).
          Target proof (devcrypto through the relay) lands at S6d.
        - [x] **S6d**: NS transport swap in both wolfhsm_client_glue copies —
          one synchronous `psa_call(SERVICE_HSM 4102)` per packet over
          `wt_hsm_psa_transport_cb`; the CSR window and the raw veneer calls
          are gone from both glues. The wolfhsm-client Zephyr module now owns
          `psa_ffm_client.c` + `hsm_psa_transport.c` (guest0 links too;
          guest0_psa dropped its duplicate), and the baremetal harness
          compiles both under a new src/client rule. Evidence (M33MU box):
          `devcrypto` PASS first try — 77 scheduled / 64 passed / 13 skipped
          / 0 failed / 0 SIM ERROR through the relay; `confboot` ACS clean —
          85 passed / 0 failed / 4 skipped / 0 SIM ERROR. confboot's
          post-suite check list still fails on the guests' SERVICE_CRYPTO
          (4097) demo probes (handle=-130) — the known mid-arc redness S6f
          retires; a post-suite scheduler diag-trap rides that same failed
          probe epilogue and should disappear with it (verify at S6f).
        - [x] **S6e**: retire the second keystore. `wt_hsm_keyvault.c` deleted;
          the vault no longer registers a key backend, so its key ops stay
          fail-closed (keys live only in the wolfHSM server keystore now). The
          vault RANDOM face split onto its own `wt_vault_service_set_rng` seam
          (RNG relocated to `wt_hsm_vault_random` in wt_hsm.c) — that op plus
          crypto_service + ffm_crypto_client are guest-RNG-coupled and retire
          with the guest repoint at S6f. Host suites: `keyvault` deleted;
          `negatives` reduced to the vault storage-face negatives (WT-FFM-0044
          owner isolation + WT-FFM-0048 sealing + flag forgery); new
          `tests/host/keystore_isolation` re-asserts WT-FFM-0046 on the server
          keystore — two servers on shared NVM at distinct stamped client_ids
          prove cross-client key isolation (request path + direct NVM
          namespace) and NONEXPORTABLE, modelled on the on-target IAK
          provisioning. Evidence: host `unit/all` green (incl. keystore_isolation
          under gcc/clang/ASan), split guard clean, box cross-build links for
          production + `WT_SP_FAULT_PROBE=1` + `WT_CONFORMANCE=1`. crypto_service.c
          kept (dead SHA/RANDOM face) until S6f.
        - [x] **S6f**: both guests on the single mediated path. guest0's
          `exercise_ffm_crypto/keys/key_negatives` (dead SERVICE_CRYPTO 4097
          op-protocol) rewritten onto the mediated wolfPSA path —
          `psa_hash_compute` for the SHA KAT, volatile P-256
          `psa_generate_key`/`psa_sign_hash`/`psa_verify_hash` for key-ops
          (tampered-digest refusal + cross-key verify refusal); `exercise_ffm_negatives`
          repointed to SERVICE_HSM 4102; every marker string preserved.
          guest1 (FreeRTOS) links the wolfHSM client legitimately and now calls
          `psa_crypto_init` in `guest_crypto_init` before the first mediated
          `psa_hash_compute` (a first-boot `-137` BAD_STATE ordering bug found and
          fixed on the box). nm guard asserts guest1 links `wt_hsm_psa_transport_cb`
          (raw `WolfTrust_HSM_*` still bundled in the shared CMSE implib until S6g
          deletes them). Evidence (M33MU box, one tree): `positive` + `bothpsa` +
          `bothiso` all PASS — both guests emit `ffm sha256 ok` through SERVICE_HSM;
          guest0 SERVICE_CRYPTO dispatch / key-ops / key-negatives / forged-handle /
          oversized-vector all green; both-OS isolation negatives (forged, oversized,
          unknown-SID) green from Zephyr and FreeRTOS — plus `confboot` PASS.
          `crypto_service.c` + `ffm_crypto_client.c` stay (unscheduled dead code);
          removing them needs descheduling SERVICE_CRYPTO from the secure image and
          folds into S6g.
        - [x] **S6g**: bypass deleted from the secure image. The three
          `WolfTrust_HSM_Submit/Poll/Cancel` CMSE veneers + prechecks are gone
          from the platform; `cmse_transport.c/h`, `crypto_service.c/h`, and
          `ffm_crypto_client.c/h` deleted; the dead crypto-SP isolated-compute
          block (work struct, MSP-switch trampoline, `run_crypto_sp_isolated`)
          and the unscheduled `wt_spm_sp_entry`/`wt_spm_sched_start` loop
          removed; monitor's veneer-only HSM wake hooks removed. The port
          contract can no longer express the bypass: `WT_PORT_CAPABILITY_HSM_TRANSPORT`,
          the `hsm_transport` window type/field, its validation, and the
          `memory_map.h` NS-RAM window macros are all deleted (spm host test
          updated). Two latent scenario breaks found+fixed: crossdomain's
          `WT_FFM_NEGATIVE_PROBE` lived only in the descheduled crypto-SP path
          (dead since the manifest swap) — re-homed into the live unprivileged
          ITS partition loop; spfaultneg's assertions still expected the
          pre-relay MEMFAULT signature + the retired guest connect-failure
          marker — updated to the relay's UNDEFINSTR UsageFault + restarted-
          relay service markers (client-unblock stays host-proven in
          sp_recovery). The repaired scenario then caught a REAL resilience
          defect: the relay's fault window can overlap a guest's boot, and the
          NS wolfHSM client glue latched one failed init as terminal — guest0's
          connect raced the recovery window and every later mediated op failed
          (A/B: same build, probe disabled, fully green). Fixed client-side
          (FF-M lets partitions restart; clients must reconnect): the glue
          heals on demand (`wolfhsm_guest_ensure_ready` + healing cryptocb
          wrapper, boot init failure downgraded to a warning, baremetal RNG
          stub retries too); defense kept secure-side:
          `wt_hsm_relay_reinit_servers` rebuilds each per-guest server on relay
          recovery (fail closed). NS side: the wolftrust-tee
          module's init/ping/
          invoke liveness probes ride `WolfTrust_FFM_FrameworkVersion` (markers
          preserved). Guards: nm absence of `WolfTrust_HSM_*` enforced on the
          secure ELF (mk link rule), guest0 (build_guest.sh), and guest1
          (build_freertos_guest.sh, which also keeps the positive
          `wt_hsm_psa_transport_cb` assert). Host suites: `crypto_service`
          suite deleted; `psa_ffm_client` + `ffm_veneer` re-fixtured onto the
          production `wt_hsm_relay_dispatch` with a SHA-256 submit hook (same
          KAT digest). WT-FFM-0054 marked met (`17e7187` + this slice).
          Evidence: host `unit/all` green, split guard hard leaks 0, box
          M33MU matrix green (see validation-log).


- [ ] **Phase 8 — hardware and port qualification** (`phases.md:136-143`):
  **second Cortex-M port** (proves the MP4 port kit) + Cortex-A/TFA replacement
  boundary; H5/C5 hardware qualification. (Moved here from the old Phase 6 line.)


- [ ] **Phase 9+ — parity, security review, release qualification.**

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
