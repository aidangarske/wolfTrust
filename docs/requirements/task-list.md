# wolfTrust implementation task list

This is the working task list for the clean-room TF-M/TFA replacement. A phase
is complete only when its implementation, negative tests, and the M33MU
lifecycle gate pass on the same commit. Hardware results are recorded
separately and are never implied by emulator results.

## Phase 1 — dependencies and clean-room baseline

- [x] Pin wolfCOSE to upstream commit `588232e6f2213133b48976f5cf3153b21fc7199c`.
- [x] Keep wolfCOSE behind the wolfTrust attestation adapter.
- [x] Cover tagged/untagged COSE Sign1 sizing and external signing on host.
- [x] Re-run the complete M33MU lifecycle with the upstream wolfCOSE pin.
- [x] Record the exact passing commit in `validation-log.md`.

## Phase 2 — portable isolation contracts

- [x] Define architecture-neutral domains, resources, lifecycle, restart, and
  isolation profiles.
- [x] Validate malformed domains, overlaps, ownership, entry points, stacks,
  interrupts, and policy cycles.
- [x] Keep Armv8-M context and CMSE mechanisms out of the generic validator.
- [x] Provide aggregate host, compiler, sanitizer, and Valgrind entry points.
- [x] Split the remaining public partition API from the Armv8-M context type.

## Phase 3 — manifest, SPM, runtime binding, and IPC

Done (host-verified):
- [x] Generate a typed manifest and validate it before SPM ready state.
- [x] Fail closed on missing or invalid generated policy.
- [x] Keep the root `make test` aggregate and the focused suite Makefiles.
- [x] Run the wolfBoot to wolfTrust to Zephyr/FreeRTOS M33MU foundation gate.
- [x] Implement the FF-M client and Secure Partition API surfaces
  (`src/ffm.c`, `src/ffm_api.c`; `include/psa/{client,service,error}.h`).
- [x] Make immutable port capabilities authoritative during manifest validation
  (`src/domain.c` `wt_domain_validate_set`; `tests/host/spm/production_main.c`).
- [x] Generate PSA identity headers (PID, SID, service version, partition
  signals) as standard `psa_manifest/{pid.h,sid.h,<partition>.h}` so an
  unmodified TF-M NS app compiles (`tools/manifest/generate.py`
  `generate_pid_header`/`generate_sid_header`/`generate_partition_header`;
  `tests/host/manifest/test_generator.py::test_standard_psa_manifest_headers`).

Remaining, ordered (each closes with host + M33MU evidence on one commit):

2. [x] Fill the FF-M doorbell signal gap: real `psa_notify`/`psa_clear`
   (`wt_ffm_notify`/`wt_ffm_clear` in `src/ffm.c`, bound in `src/ffm_api.c`;
   WT-FFM-0027, `tests/host/ffm/main.c`).
2a. [ ] Implement `psa_eoi`. Blocked: no engine function asserts an interrupt
   signal into `asserted_signals` yet (unlike the doorbell bit, IRQ signals
   are validated in the manifest but never raised at runtime) — needs real
   interrupt-controller/ISR integration, tied to the M33MU NVIC question in
   item 10, not a host-only `ffm.c` change.
2b. [ ] Honor the `psa_wait` timeout (`src/ffm_api.c` ignores it; `PSA_POLL`
   is already the de facto behavior since wait never blocks). `PSA_BLOCK`
   needs the cooperative scheduler to retry/yield across partitions — that is
   platform glue in item 3's production boot path, not `ffm.c` alone.
3a. [x] Initialize the FF-M IPC runtime at boot against the real generated
   manifest: `wt_monitor_init` now calls `wt_ffm_boot_init` (`src/ffm_boot.c`,
   `include/wolftrust/ffm_boot.h`) right after `wt_partitions_bind_manifest`
   succeeds, which runs `wt_ffm_init` + `wt_ffm_api_bind` and panics on
   failure like every other boot-time validation step. Proven on host
   (`tests/host/spm/production_main.c` confirms `SERVICE_ATTEST`/
   `SERVICE_CRYPTO` register by SID against the real
   `port/stm32h563/manifest.json`) and on the Cortex-M cross-build
   (`sec_ffm_boot.o` links into `wolftrust.elf`). `check_read`/`check_write`
   are fail-closed placeholders (deny by default) and `dispatch` returns
   `WT_FFM_ERROR_STATE` — safe today because nothing calls `psa_call` yet.
3b. [x] Real Armv8-M CMSE memory validation for `check_read`/`check_write` in
   `src/ffm_boot.c` (`WT-FFM-0012`: the SPM validates every external memory
   reference before an API transfer): pairs the raw CMSE range check
   (`wt_cmse_check_ns_ro`/`_rw`) with the per-guest declared-window check
   (`wt_cmse_check_in_guest_ns_addr`/`_ram`, `src/arch/armv8m/cmse.c`) for
   defense-in-depth, reusing existing infrastructure rather than new logic.
   Secure-Partition callers (`caller > 0`) stay fail-closed — no per-SP
   memory envelope exists until item 5. CMSE/MPU details stay out of
   `src/ffm.c` per the architecture-neutral boundary rule. Verified only by
   the Cortex-M cross-build compiling and linking — no test exists yet.
3b-test. [x] Test evidence for 3b's `check_read`/`check_write`: closed by the
   3c-ns M33MU run below. The guest's FF-M `psa_call` crosses the CMSE
   veneer with real NS input/output pointers, so `check_read`/`check_write`
   execute live in `wt_ffm_prepare_vectors` on-target and the correct
   SHA-256 comes back — the ARM-only CMSE path is exercised, not just
   compiled.
3c. [x] Secure-side half of the crypto migration through real
   `psa_connect`/`psa_call` dispatch. The SHA-256 service handler moved to
   its own architecture-neutral file, `src/services/crypto_service.c`
   (`wt_crypto_service_dispatch`: wait/get/read/wolfCrypt `wc_Sha256*`/
   write/reply) — no Armv8-M/CMSE dependency, so it is directly
   host-testable, unlike everything else in `src/ffm_boot.c`. `dispatch`
   in `src/ffm_boot.c` now routes `PARTITION_CRYPTO_ID` to it instead of
   the `WT_FFM_ERROR_STATE` placeholder. **Real test evidence**:
   `tests/host/crypto_service/` drives an actual `wt_ffm_connect` +
   `wt_ffm_call` round trip (registering `wt_crypto_service_dispatch`
   directly as the port's `dispatch` callback, since `wt_ffm_call()`
   invokes it synchronously) and asserts the returned digest against a
   real SHA-256 KAT — proving the dispatch path is correct, not just that
   it compiles. Wired into `make test` (`unit/crypto_service`). Added the
   NS-to-Secure carrier: `WolfTrust_FFM_Connect`/`_Call`/`_Close`,
   `cmse_nonsecure_entry` veneers in `src/ffm_boot.c` following the exact
   pattern proven in `src/services/vnet/vnet_service.c`
   (`wt_platform_active_guest_id()` + paired `wt_cmse_check_ns_*`/
   `wt_cmse_check_in_guest_ns_*`). `WolfTrust_FFM_Call` bundles the vector
   pair into one `wt_ffm_veneer_iovec_t` struct pointer, not 4 scalars —
   `cmse_nonsecure_entry` functions can't take stack-passed args (~4
   register-arg limit); the struct is CMSE-checked then read once into a
   local copy to avoid a NS-side TOCTOU on its fields. Verified on the
   Cortex-M cross-build (compiles, links via `--cmse-implib`); `make test`
   green including the new crypto_service suite. The veneers themselves
   (CMSE checks, guest-id mapping) are still only compile-verified — that
   gap is 3b-test, unchanged, closes with 3c-ns. No purpose-built
   NS-to-Secure transport for wolfTrust's own `psa_connect`/`psa_call`
   exists (`WT_NSC_VENEER` is defined but unused) —
   these veneers ARE that transport, reusing the existing Zephyr `tee`
   driver (`tests/firmware/zephyr-stm32h5/module/wolftrust-tee/`, a generic
   vendor-neutral Zephyr subsystem, not Arm/TF-M-specific) as the carrier.
3c-ns. [x] NS-side half done and proven on M33MU. Added FFM function IDs
   (`WOLFTRUST_FN_FFM_CONNECT/CALL/CLOSE`) to `wolftrust_tee_driver.c`
   dispatching into the veneers, and `exercise_ffm_crypto()` in
   `apps/guest0_psa/src/main.c` driving a real `psa_connect`/`psa_call`
   for `SERVICE_CRYPTO` over the TEE transport. The emulator boot prints
   `wolfTrust FF-M SERVICE_CRYPTO dispatch verified` with the correct
   SHA-256 digest and reaches `[EXPECT BKPT] Success`, exit 0. Asserted in
   the workflow (`.github/workflows/stm32h563-build.yml`) and in the local
   gate (`wolftrust-m33mu` skill / `run_m33mu.sh`). Because GitHub Actions
   is capped this month, the confirming run was the local M33MU gate on
   `wolf-prec5560` in the CI container — emulator evidence, recorded as
   such. This gives 3a/3b/3c-secure their first real on-target caller.
3c-followup. [ ] Remove the TEE-driver dependency once purpose-built FF-M
   NSC veneers exist (`WT_NSC_VENEER`-based, directly exposing
   `psa_connect`/`psa_call`/`psa_close` without going through the generic
   `tee_invoke_func` indirection). TEE is a legitimate reusable carrier for
   now, not the long-term production transport.
4. [ ] Register the PSA services (crypto, attestation) behind SIDs and route NS
   calls through `psa_connect`/`psa_call` → SPM dispatcher instead of the
   current direct wolfHSM/wolfCOSE calls.
5. [ ] Separate Non-secure applications from actual Secure Partitions and run
   each Secure Partition in a distinct Secure Level 3 protection domain
   (WT-FFM-0011). Decomposed:
5a. [x] Architecture-neutral Secure-Partition protection-domain resolver and
   containment predicate: `wt_ffm_resolve_secure_domain` /
   `wt_secure_domain_contains` (`src/ffm_domain.c`,
   `include/wolftrust/ffm_domain.h`). The resolver derives a partition's
   private MPU region set from the validated manifest and fails closed for
   SPM, Non-secure, unknown, oversized, or null inputs (leaving an empty
   domain, so no over-broad set is ever programmed). The predicate proves a
   span lies wholly inside a granted region and excludes any other domain's
   private memory. **Real test evidence**: `tests/host/ffm_domain/` drives 20
   assertions printing `WT-FFM-0011` — a partition owns its RW RAM and RX
   flash (RX not writable), each partition excludes the other's private RAM,
   spans past a region end / zero length / address overflow are rejected, and
   every fail-closed path returns the right error with an empty domain. Green
   under gcc, clang, and ASan/UBSan; wired into `make test`
   (`unit/ffm_domain`). Linked into the secure build
   (`mk/secure-armv8m-stm32h563.mk`) and compiles for Cortex-M33 with the
   production `-ffreestanding` flags (228 B text, no data/bss). This is the
   policy half only: the object is not yet called from the boot path because
   enforcement (5b) needs the region-table swap. Per the completion rule a
   positive marker is not isolation evidence — 5a proves the derivation, 5c
   proves the fault.
5b. [x] Enforcement: run each Secure Partition as its own secure execution
   context and switch the secure MPU to the partition's resolved domain around
   dispatch, restoring the SPM whitelist on return. Design: reuse the existing
   secure coroutine layer (`src/sched/coroutine.c` — per-context stacks,
   cooperative switch from the monitor, and a fault handler that already marks
   a context FAULTED on MemManage/MPU_S/PSPLIM_S). A partition runs as a
   coroutine; on switch-in the port narrows the secure MPU to
   `[secure code RX] + [the partition's resolved private regions]`, on
   switch-out it restores the whitelist. Because the Level 3 profile copies
   IOVEC transfers (WT-FFM-0041), the SPM — not the partition — touches client
   memory, so a partition's table needs only code plus its own regions;
   PRIVDEFENA is already off, so an unmapped access faults even at privileged
   level. Constraint: the M33 secure MPU has 8 regions and `wt_mpu_s_init`
   already uses all 8, so the table is swapped on entry, not appended.
   As built: the crypto SP compute runs on its carved stack via a dedicated
   secure-MSP trampoline (`wt_crypto_sp_call`) rather than a coroutine — the
   coroutine layer's 24 KiB stack gate and nested-context state made it the
   heavier option for a synchronous, non-yielding compute; the trampoline
   narrows/restores the MPU in a C body on the SP stack. Sub-steps (phased so
   A–C are host/compile-only and only D/E spend an M33MU gate — two runs
   total):
   - [x] Architecture-neutral table composition
     (`wt_ffm_compose_secure_partition_table`, `src/ffm_domain.c`): shared
     regions + the resolved private set, fail-closed past 8. Host-tested in
     `tests/host/ffm_domain/` — the composed table maps secure code and the
     partition's own RAM and excludes another partition's RAM and the MPU
     control block (`0xE000ED94`). gcc/clang/ASan/UBSan green; Cortex-M33
     compile clean.
   - [x] Phase A — carve secure per-partition RAM. `memory_map.h` reserves the
     top 16 KiB of the secure RAM window (`0x3009C000..0x300A0000`, the end of
     physical SRAM) as two 8 KiB secure stacks (`WT_SP_CRYPTO_STACK_BASE`,
     `WT_SP_ATTEST_STACK_BASE`); `secure.ld` shrinks the main RAM to 464 KiB,
     adds the `SPSTACKS` region + `.sp_stacks` section, and asserts the region
     sits directly above the RAM window. `tests/host/sp_layout/` guards the
     carve with compile-time `_Static_assert`s (alignment, in-window,
     contiguous, non-overlapping) plus a runtime print; green under
     gcc/clang/ASan/UBSan, wired into `make test` (`unit/sp_layout`). Secure
     link with the shrunk RAM is verified on-target: an M33MU regression run of
     the A+B+C tree links (wolfTrust image 81208 B, no RAM overflow), validates
     and binds the 5-domain manifest at boot, and boots the full lifecycle to
     `[EXPECT BKPT] Success`, exit 0. (That run also surfaced a pre-existing,
     unrelated bug: the wolfBoot `d85fa9d` boot handoff is rejected by
     `wt_boot_handoff_consume` so attestation reports `lifecycle=0x0000` — see
     the "boot handoff rejected" task; the local gate warns rather than fails on
     it so Phase D/E isolation stays verifiable.)
   - [x] Phase B — separate NS-application domains from Secure-Partition
     domains. `manifest.json` now carries five domains: SPM (0), the two guests
     relabelled `NONSECURE_APPLICATION`/NON-SECURE (1,2), and new
     `SECURE_PARTITION` domains for attestation (3) and crypto (4) whose memory
     is genuine secure code + the Phase-A secure stacks. `PARTITION_ATTEST`/
     `PARTITION_CRYPTO` re-point to domains 3/4; port `max_domains` 3→5; the
     guest-binding assertion in `partitions.c` now expects a Non-secure
     application domain. Proven host-side: the generator validates the manifest,
     `unit/spm` binds it (guests → NS-app domains) and — with `src/ffm_domain.c`
     linked in — asserts on the real generated manifest that the crypto Secure
     Partition resolves to its own secure stack (`WT_SP_CRYPTO_STACK_BASE`) and
     no longer reaches Non-secure guest RAM (`0x20000000`). Full `make test`
     green.
   - [x] Phase C — port secure-MPU swap primitive
     (`wt_platform_program_secure_partition_domain` /
     `wt_platform_restore_spm_domain`, `platform_stm32h563.c`; declared in
     `platform.h`). Programs regions 0..count-1 from the composed table with
     any-privilege AP (so an unprivileged partition thread reaches its own
     regions) and disables the rest; restore re-runs `wt_mpu_s_init`. Not
     called yet. The new logic compiles clean in isolation on the host
     (`-Wall -Wextra -Werror -pedantic`) and for Cortex-M33 freestanding
     (108 B text); the full-file secure link is verified at the Phase D
     container build.
   - [x] Phase D — run the crypto SP compute on its own secure stack with the
     MPU narrowed to the composed domain. Two parts: **D1** reshaped
     `crypto_service.c` around copied IOVEC (`5b9b8bd`) — the SPM drains the
     input vector into a bounded buffer and calls a pure `wt_crypto_sp_hash`
     that makes no `psa_*` calls, host-proven in `tests/host/crypto_service/`
     (isolated-compute KAT + over-cap rejection). **D2** added the port runner
     (`wt_platform_run_crypto_sp_isolated`, `69dc8eb`): a naked trampoline
     switches MSP_S to `WT_SP_CRYPTO_STACK_BASE`, narrows the secure MPU to
     `[secure code RX] + [crypto SP stack RW]`, runs the SHA, restores the SPM
     domain; installed at boot via `wt_crypto_service_set_compute`. The compute
     is forced pure-software (`INVALID_DEVID`) so it never touches the shared
     crypto-callback registry. **M33MU positive (run #1)**: `SERVICE_CRYPTO`
     returns the correct SHA-256 under the narrowed domain, attestation
     `verify=0 lifecycle=0x1000`, `[EXPECT BKPT] Success`, no fault markers.
5c. [x] Phase E — M33MU negative proof (`f15fe11`): a test-gated probe
   (`WT_FFM_NEGATIVE_PROBE`) executed inside the crypto Secure Partition reads
   SPM-private RAM (`WT_RAM_S_BASE`, 0x30028000) and faults the initiating
   partition — `[MEMFAULT] pc=0x0c060f34 addr=0x30028000 sp=0x3009dff0` (SP
   stack), boot halts, no data exposed. Closes WT-FFM-0011's failure clause and
   `framework.md` acceptance-gate negative #1. **M33MU negative (run #2)** via
   `run_m33mu_negative.sh`. Follow-up (own item): graceful fault recovery so the
   probe run continues instead of halting, and CI wiring of the negative job.
6. [x] Make generated resources, entry points, lifecycle, services, and policy
   authoritative in the production runtime (not only at validation). Resources,
   entry points, IRQ mask, and NS MSP were already bound from the manifest in
   `wt_partitions_bind_manifest`; services and version policy are already
   manifest-driven in `src/ffm.c`. Closed the two remaining "validated but not
   applied" gaps: **restart policy** — bind now assigns
   `config->restart_policy` from the domain instead of equality-bricking on
   mismatch (`22e5461`); **initial lifecycle** — `wt_partition_reset_runtime`
   sets `runtime->state` from the manifest's `initial_lifecycle`, and NS guest
   domains declare `READY` (`95c61a1`). Host: `test_production_manifest` proves
   both values flow from the manifest (distinct restart_limit and a STOPPED
   guest bind through). M33MU positive green (guests still boot READY). Residual
   non-manifest authority is tracked separately: service **dispatch** routing
   (task #3) and the crypto-SP **MPU region** from the resolved domain (task
   #26). Follow-up: collapse the unused `src/lifecycle.c` restart engine that
   duplicates `wt_restart_guest` (hygiene, no authority impact).
7. [x] Route Initial Attestation and the RTOS framework probes through FF-M IPC.
   - [x] Server side (`29ab959`): added an architecture-neutral
     `wt_attestation_service_dispatch` (`src/services/attestation_service.c`,
     mirroring `crypto_service.c`) that carries a challenge in / token out over
     a real `psa_connect`/`psa_call`, calling the existing
     `wt_initial_attest_get_token` backend; wired `PARTITION_ATTEST_ID` into
     `wt_ffm_boot_dispatch` (gated on `WT_ATTEST_COSE`). Host-proven in
     `tests/host/attestation_service/` (real FF-M round trip, stubbed backend so
     the suite isolates IPC routing — the token generator itself is M33MU-proven
     already). Secure image compiles and links with the new service on target.
   - [x] Client side (`0de6c52`): the guest's `psa_initial_attest_get_token`
     shim (`wolftrust_attestation_client.c`) now connects `SERVICE_ATTEST`
     (sid 4096) and calls it over `WolfTrust_FFM_Connect`/`Call`/`Close` instead
     of the direct `WolfTrust_Attest_GetToken` veneer; the exact token length
     comes from the (deterministic) size query. M33MU positive: `attestation
     verify=0 challenge=ok identity=ok lifecycle=0x1000 measurement=ok
     cose=ES256` through the FF-M path. The now-unused `WolfTrust_Attest_GetToken`
     veneer is retired with the rest under task 16.
   - Decision (RTOS framework probe): the FreeRTOS guest's wolfPKCS11 ->
     wolfHSM path is the wolfHSM CMSE Submit/Poll transport, i.e. the HSM
     service itself, not a PSA FF-M RoT service. It stays a distinct transport
     by design; the FF-M IPC routing goal is met for the PSA RoT services
     (crypto + attestation), which both RTOS guests reach. Exposing wolfHSM
     operations as FF-M services, if ever wanted, is a separate item (relates to
     task 16). Item 7's Initial-Attestation scope is complete.
8. [x] Add the missing wolfTrust FF-M security tests: partition restart and
   cross-domain isolation (handle integrity, bounded pools, scrubbing, and
   pointer revalidation already covered in `tests/host/ffm/main.c`).
   - [x] Slice 1 — restart-on-fault target scenario: `WT_GUEST_FAULT_PROBE`
     (guest0_psa) reads Secure RAM on boot; the SecureFault escalates to the
     monitor. Proves the `guest0_psa alive` banner reappears `restart_limit+1`
     = 4 times then the guest is FAULTED — manifest `restart_limit` honored on
     target, and the monitor gracefully restarts a Non-secure guest fault while
     the other guest keeps running.
   - [x] Slice 2 — detect-or-skip harness: `tests/target/run_m33mu_scenario.sh`
     (DRY runner for positive/restart/crossdomain) + a standalone `make
     test-target` (separate from host-only `make test`, like
     `make test-conformance`). Auto-detects M33MU (or `WT_TARGET_SCENARIOS=1`);
     explicit `SKIP` otherwise, never a silent pass. M33MU: `PASS: target/all`.
   - [x] Slice 3 — folded the item-5 cross-domain negative into the runner and
     wired CI job `wolfboot-wolftrust-m33mu-scenarios` (matrix restart,
     crossdomain) driving the same runner so CI and the local harness assert
     identical markers. Closes the item-5 follow-up of wiring the negative job
     into CI (task #26 half); SP graceful fault recovery stays task #26.
   - Evidence for all three slices in `validation-log.md`.
9. [x] Add M33MU FF-M assertions: a positive `psa_connect`/`psa_call` round trip
   plus negatives (forged handle, oversized vector, cross-domain access) on the
   emulator path (`.github/workflows/stm32h563-build.yml`). Positive round trip
   (SERVICE_CRYPTO dispatch) and cross-domain (item-8 crossdomain scenario) were
   already covered; added `exercise_ffm_negatives` (guest0_psa) proving the SPM
   rejects a forged handle (`st=-129`) and an oversized input vector (`st=-135`,
   > `WT_FFM_TRANSFER_BYTES`) without a fault, guest still reaching
   `[EXPECT BKPT] Success`. Markers asserted in the positive runner, the box
   gate, and the CI `wolfboot-wolftrust-m33mu` job. Evidence in
   `validation-log.md`.
### Design: one macro-gated target-scenario harness (folds in 7, 8, 9, 11, and the item-5/6 target proofs)

Rather than wire each target-only scenario into the M33MU gate piecemeal, add
them together, at the end, as a single **detect-or-skip** harness driven from
`make test` (and the same harness in CI):

- **Detection macro.** A build/env gate (e.g. `WT_TARGET_SCENARIOS`, set when an
  M33MU binary — or real HW — is detected) selects the target-scenario suite.
  When present, `make test` builds and boots the gated scenario firmware and
  asserts each scenario. When absent, it **skips with an explicit message**
  ("M33MU/HW not detected — target scenarios skipped"), never a silent pass —
  same rule as the item-10 `make test-conformance` auto-detect.
- **Every scenario stays host-provable where the logic is portable** (the
  compute, the policy, the state machine live in host unit tests); the harness
  proves only the genuinely target-bound behavior (MPU faults, restart on real
  faults, context switch), each behind a test-only gate so production never
  faults.
- **Scenarios collected into the one sweep** (each a `grep`-asserted marker in
  both the harness and the workflow yml):
  - restart policy honored on a real fault: a guest faults, restarts up to the
    manifest `restart_limit`, then goes `FAULTED` at the limit (item 6 restart
    half / task 7). *This is the currently-missing target coverage — restart is
    never exercised in the happy-path lifecycle run.*
  - cross-domain probe faults the initiating partition (item 5 Phase E — already
    have `run_m33mu_negative.sh`; fold its marker in).
  - graceful SP fault recovery so a negative run continues (task 26).
  - service dispatch by SID including `SERVICE_ATTEST` (task 3).
  - forged/reused handle, oversized vector, interrupted transfer expose no stale
    data (framework.md acceptance-gate negatives 2–3 / item 9).
- **Rationale.** `make test` becomes the single entry point: it runs everything
  host-side always, and the full target scenario set whenever an emulator/board
  is available, skipping cleanly otherwise. CI runs the identical harness on the
  box. Real H5 hardware stays a separate, never-emulator-implied record.

10. [ ] Expand `tests/host/psa_ff_upstream/` past the host-viable subset
    (now `i001,i003-i008,i010,i011,i012,i024,i025,i026,i067[SKIP],i071,i088,
    i090` — Slice 1 added version-policy i010/i011/i026; i090 added the
    negative-type PROGRAMMER_ERROR check; i003 added the invec/outvec data
    plane, i027 the connection drop, i063 the signal-mask refusal, and i002 the
    full connection lifecycle via the per-test dispatch) to the full Arm
    FF-M suite under M33MU (NS app + 3 SPs, including the tests that need real
    reboot continuity and multi-partition isolation) and add the TF-M baseline
    comparison. `make test-conformance` must auto-detect an available M33MU
    binary/emulator: when present, run the full FF-M suite on it; when absent,
    run only the host-viable subset and print an explicit warning that
    hardware/emulator was not detected and coverage fell back to non-HW tests.
    Confirm first whether m33mu models a real NVIC (IRQ-class tests need this;
    TrustZone isolation and flash-persisted reboot cycles are already proven by
    existing CI). Real H5 hardware is not required for this gate — it stays a
    separate, never-implied-by-emulator hardware evidence record per the skill.
10a. [x] Host version-policy tests `i010,i011,i026` wired. FF-M resolves an
    unspecified manifest service to version 1 + `STRICT`, so they model as a
    `STRICT` service at version 1 — not the permissive `WT_SERVICE_VERSION_
    UNSPECIFIED` enum (that means "accept any version", a different concept).
    `i026` also required a conformance fix: `psa_call` with
    `in_len + out_len > PSA_MAX_IOVEC` now returns `PSA_ERROR_PROGRAMMER_ERROR`,
    not `PSA_ERROR_INVALID_ARGUMENT` (`ffm.c`; WT-FFM-0032 updated to match).
    `i002,i003,i048-i053,i058,i063,i090` remain blocked on 10b (server
    dispatch), not on a version policy.
10b. [x] Give `test_dispatch()` in `tests/host/psa_ff_upstream/main.c` real
    per-service logic instead of a generic wait/get/reply(SUCCESS). A
    `g_active_test` selector routes to the right per-test server (the upstream
    tests reuse SIDs with contradictory server behavior). DONE: the router +
    `ipc_connect`/`ipc_close` vtable entries + `i003` (invec/outvec data plane)
    + `i027` (connection drop — new `SERVER_CONNECTION_DROP` service; also fixed
    `wt_ffm_close` to allow closing a dropped `WT_IPC_CONNECTION_ERROR`
    connection, host test WT-FFM-0022) + `i063` (signal-mask refusal,
    client-visible half) + `i002` (all 9 connection-lifecycle checks:
    busy/reject, accept/close, version policy, status-code echo, allowed types,
    identity, connect-limit via the 16-slot pool returning BUSY, block/poll
    refusal). Server-internal halves of `i063` and `i002` block/poll (real
    masked `psa_wait`) are deferred to M33MU (task #14). NOT host-viable:
    `i048`-`i053` (need real MPU isolation → Slice 3/M33MU) and `i058` (doorbell
    client compiled out under `-DNONSECURE_TEST_BUILD`).
11. [ ] Pass the host and M33MU FF-M positive and negative suites on one commit.

The earlier Phase 3 validation proves the generated-policy bootstrap and the
H5 Non-secure guest-monitor lifecycle. It does not prove FF-M IPC or Level 3
Secure Partition isolation. Phase 3 remains open until the acceptance gate in
`framework.md` passes.

## Item 10 — full TF-M-parity conformance port (program P1–P9)

Goal: run the UNMODIFIED upstream Arm PSA-FF conformance suite (NS client app +
Arm's own Secure test partitions + driver partition) through wolfTrust's real
production SPM on the STM32H563/M33MU, matching what TF-M provides — plus our own
equivalent tests, plus a TF-M baseline comparison. The host-only conformance
subset (item 10 Slices 1-2, done) validated our IPC/policy logic in C; this
program makes it a real on-target conformance result and a demonstrable TF-M
drop-in replacement. Decided over the "hybrid one-SP" shortcut because that
proves neither a conformance claim nor the replacement story (marks its own
homework). Each phase is independently host + M33MU testable; anything not
honestly provable yet is split into its own tracked item, never faked.

Root blocker found by scouting (2026-08-12): wolfTrust Secure Partitions are NOT
schedulable entities today — an SP is an inline C call on the NS caller's stack
(`src/ffm_boot.c:78-90` hardcoded `if pid==CRYPTO/ATTEST`), `psa_wait` never
blocks (`src/ffm.c:635-648`, `src/ffm_api.c:104-113` ignores timeout), and the
monitor scheduler only sees NS guests. So P1 is the keystone.

- P1. [ ] **Generic SP scheduling/execution context (KEYSTONE, large).** Make a
  Secure Partition a schedulable context (private stack + saved regs, enter/
  suspend/resume) instead of an inline function call, and a table-driven
  `dispatch(partition_id)` from the manifest-bound partition table instead of the
  per-PID `if` chain. Re-host crypto + attest through the generic path to prove
  no regression. Touches `src/ffm_boot.c`, `src/ffm.c`, `src/monitor.c`,
  `port/stm32h563/partitions.c` (add an SP scheduling-slot table), likely
  `src/sched/coroutine.c`. Host + M33MU. Split into P1a/P1t/P1r:
  - P1a. [x] **Manifest-bound dispatch registry (host, DONE).** Added a
    per-partition `dispatch`/`dispatch_context` to `wt_ffm_partition_runtime_t`
    with `wt_ffm_register_partition`; `wt_ffm_dispatch_message` now routes each
    message to the owning partition's registered service loop (fail-closed port
    op only as fallback), replacing the per-PID `if` chain in `ffm_boot.c`.
    Crypto + attest register through it in `wt_ffm_boot_init`. Evidence: host
    test `WT-FFM-0014 partition dispatch routing` (registered loop intercepts,
    port op bypassed) + crypto/attest KAT round trips still green in `make test`.
  - P1t. [~] **Schedulable execution context.** Make an SP a private-stack +
    saved-regs enter/suspend/resume context on the coroutine scheduler so it is a
    real schedulable entity, not an inline call. Scout facts: `SVC_Handler` +
    `PendSV_Handler` (`src/arch/armv8m/coroutine_armv8m.c`) already switch
    cooperatively (bootstrap on MSP_S, tasklets on PSP_S) but the scheduler is
    compiled yet dormant (no production caller), tasklets run **privileged** with
    **no per-tasklet MPU**, and SVC only distinguishes `#0x7F` (NS-guest return).
    The one-shot `wt_crypto_sp_call` is a same-mode MSP swap and cannot call back
    into the SPM. Correction to the earlier note: the scheduler is **not**
    host-buildable (no host arch backend; a portable one is hostile to the
    `-pedantic -Werror` + ASan + Valgrind host harness), so the switch itself is
    M33MU-proven and only the neutral gate logic is host-proven. Split P1t-1/P1t-2:
    - P1t-1. [x] **SPM-call gate + arg validation + block classification (host,
      DONE 2026-08-12).** `wt_spm_gate` (`src/spm_gate.c`,
      `include/wolftrust/spm_gate.h`) is the single choke point every SP-side
      `psa_*` funnels through (wait/get/set_rhandle/read/skip/write/reply/notify/
      clear → the `wt_ffm_*` runtime); it bounds every SP-supplied pointer against
      the caller's resolved domain via `wt_secure_domain_contains` before the SPM
      touches it, and `wt_spm_call_would_block` flags an empty `psa_wait` as
      "suspend this SP". This is what the target SVC handler calls after
      unmarshalling registers. Evidence: `tests/host/spm_gate` (62 checks,
      gcc+clang+ASan/UBSan) — gate routes the real connect+call dispatch path,
      classifies an empty wait as blocking, and rejects out-of-domain pointers.
      Not yet in the secure build (lands with P1t-2 under one M33MU gate).
    - P1t-2a. [x] **Gate live in the production image (M33MU 2026-08-12).** The
      production crypto dispatch (`src/services/crypto_service.c`) routes
      wait/get/read/write/reply through `wt_spm_gate`; `src/spm_gate.c` is in
      the secure build. Still privileged/synchronous. Host: crypto_service KAT
      green under cc/gcc/clang + ASan/UBSan; `make test` all green. M33MU
      positive gate PASS (KAT + dispatch markers, `[EXPECT BKPT] Success`, no
      faults) — the KAT transits the gate in the production image.
    - P1t-2b. [x] **Coroutine-backed SP via SVC gate (M33MU +/- 2026-08-12).**
      The crypto SP runs as a scheduled coroutine, unprivileged on its own PSP
      stack + manifest MPU domain (nPRIV set on PendSV switch-in, MPU narrowed
      via `wt_platform_program_sp_thread_domain` with PRIVDEFENA so privileged
      handlers keep SPM access), trapping to the privileged SPM through
      `svc #1` -> `wt_spm_svc_entry` -> `wt_spm_gate` for every `psa_*`, and
      suspending on `psa_wait` via `wt_co_block`. New `src/arch/armv8m/spm_svc.c`;
      coroutine gains `domain`/`unprivileged` + `wt_co_set_domain` /
      `wt_co_create_blocked_ex`; `src/spm_gate.c` in the secure build.
      Transport + compute reach the loop through a stack-built dispatch context
      so the unprivileged SP never reads the SPM-RAM globals (fixed a first-run
      cross-domain fault at 0x300282b0). M33MU positive (KAT via
      veneer->svc->gate->coroutine, `[EXPECT BKPT] Success`) + negative
      (`[MEMFAULT] addr=0x30028000`, SP on PSP domain stack, Thread mode) both
      PASS on one tree. **P1 KEYSTONE done**: an SP is now a real schedulable,
      unprivileged, MPU-isolated entity, not an inline call.
  - P1r. [x] **Production registration M33MU regression (DONE, M33MU 2026-08-12).** Confirm P1a's
    `ffm_boot` registration path dispatches crypto + attest unchanged on M33MU
    (host cannot compile the target `ffm_boot`; Mac `arm-none-eabi` lacks libc
    headers). Bundle with the next box gate run.
- P1b. [x] **Wire the generic domain resolver into the live path (DONE, M33MU +/- 2026-08-12).** (was: medium, cheap
  early win).** `wt_ffm_resolve_secure_domain` / `wt_ffm_compose_secure_partition_table`
  (`src/ffm_domain.c`) are already generic + manifest-driven but only exercised
  in host tests; the one live target caller (`wt_crypto_sp_body`) hand-builds its
  2-region table. Connect the resolver so any SP gets its manifest-declared MPU
  domain. Can land before/parallel to P1. CODE LANDED (evidence pending M33MU):
  `wt_platform_run_crypto_sp_isolated` now resolves the crypto domain via
  `wt_ffm_resolve_secure_domain(PARTITION_CRYPTO_ID)`, takes the SP stack/work
  area from the manifest's writable resource (fail-closed if absent or too
  small), and appends non-EXEC domain resources to the MPU table. EXEC windows
  stay the shared whole-image RX (manifest 4K code window lies inside it;
  Armv8-M MPU regions must not overlap — task #26 tracks narrowing). Tick only
  after the M33MU positive + negative-probe runs pass on this code.
- P2a. [x] **Manifest-ingestion generator, identity headers (host, DONE).**
  `tools/manifest/ingest_psa_arch.py` converts Arm's 3 upstream
  `*_partition_psa.json` into `psa_manifest/{pid.h,sid.h,<partition>.h}` by
  reusing `generate.py`'s validated header emitters (FF-M unspecified-version
  default → version 1; per-partition signal assignment incl. the driver UART
  IRQ signal). Gated in `make test-conformance` via `test-manifest-ingest`
  (`tests/host/manifest_ingest/run.py` asserts SIDs 0xFA01/0xFB01-07/0xFC01-04,
  versions, and driver signals against the real fetched manifests).
- P2b. [x] **Ingester emits the full conformance system manifest (host, DONE).**
  `ingest_psa_arch.py --base manifest.json --emit-manifest` merges Arm's
  SERVER/DRIVER/CLIENT onto the production manifest with deterministic secure-RAM
  layout (SP stacks 0x3009A000/C000/E000, code windows 0x0C012000+), resolving
  cross-partition dependencies by name→SID and bumping caps/limits. The committed
  `port/stm32h563/manifest-conformance.json` is now proven reproducible from the
  unmodified upstream manifests (test asserts generated == committed and that it
  validates through `generate.py`) — no more hand-transcription.
- P2c. [x] **Capacity + build selection (host, DONE).** 5-slot secure stack carve
  (`memory_map.h`, `secure.ld`), platform caps 8 domains / 3 mem-resources
  (checked as `<=`, so production stays valid), `WT_CONFORMANCE=1` swaps the
  conformance manifest into the secure build. Remaining P2 (trampoline generalized
  to N schedulable SPs) is folded into P1t — a one-shot generalization is a
  dead-end because real Arm SPs call back into the SPM mid-execution.
- P2. [~] **Table-driven SP load/entry + capacity (large).** `entry_point` is
  validated but never branched to for SPs; generalize the crypto trampoline
  (`platform_stm32h563.c:449-539`) into an N-partition manifest-driven mechanism;
  bump per-SP stack carve (`WT_SP_SECURE_STACK_COUNT`), `max_memory_resources_per_domain`
  (2→3+ for code+stack+MMIO), and check `WT_FFM_MAX_PARTITIONS`/`WT_MAX_MPU_REGIONS`
  against the Arm suite's partition count. Enables registering a foreign SP by
  manifest data alone.
  A full ff/ipc run needs THREE real Arm SPs (fixed SIDs): SERVER_PARTITION
  (0xFB01-07), DRIVER_PARTITION (0xFC01-04 UART/watchdog/NVMEM/TEST + 1 IRQ),
  CLIENT_PARTITION (0xFA01). The DRIVER partition is BASELINE PLUMBING for EVERY
  test (all `val_print`→DRIVER_UART, all boot-flag/NVM→DRIVER_NVMEM), not just the
  isolation tests. wolfTrust also needs a manifest-ingestion generator: turn Arm's
  3 `platform/manifests/*_psa.json` into wolfTrust partitions AND emit the
  `psa_manifest/sid.h`/`pid.h` the upstream `val`/PAL `#include`s (today the host
  harness fakes this by hand in `main.c:132-196`). The target port itself is small
  (4 files: `pal_config.h`, `nspe/pal_driver_ipc_intf.c`, `spe/pal_driver_intf.c`,
  `target.cmake`, modeled on `tgt_ff_tfm_an521/`); STM32 UART/watchdog drivers
  already exist upstream (`platform/drivers/{uart,watchdog}/stm/`), but NVM must be
  real flash (upstream `pal_nvmem.c` is SRAM-only). Test-bucket order (of 90):
- P3. [x] **Bucket (a): NS client + val + SERVER SP + DRIVER SP (print/NVM only)
  — the true minimum-viable real SPM run.** Needs P1/P2 + the 3-SP hosting +
  target PAL + manifest ingestion + real UART/NVM drivers. First real on-target
  conformance (version-policy, lifecycle, data-plane, signal/status). Then bucket
  (a′): add CLIENT_PARTITION SP. NOTE (2026-08-12 scout): upstream checkout is
  **90 IPC tests** (`test_i001..i090`), not 36 — 36 was a stale count; the
  runnable non-IRQ/non-isolation subset is the P3 target, the rest fall in P4/P5.
  PSA_BLOCK now works (P1t-2b coroutine suspend/resume). Full scout map +
  first-slice brief in `/tmp/wolftrust-p3-plan-2026-08-12.md`. Sliced P3a/b/c:
  - P3a. [ ] **First real Arm SP on target (keystone-equivalent proof).**
    Ordered, each M33MU-gated (do not stack; each must pass before the next):
    - P3a-1. [ ] **Generalize `wt_spm_sched_start` to a table of N SPs** driven
      from the bound manifest, replacing the single hardcoded crypto SP
      (`src/arch/armv8m/spm_svc.c`). Per-SP coroutine + domain + dispatch keyed
      by the current coroutine (the `g_spm_sp_*` singletons become arrays; the
      SVC entry resolves the caller from `wt_co_current()`). Crypto stays green
      as table entry 0. No Arm SP yet — pure foundation. Gate: existing M33MU
      positive (crypto KAT) + negative regression on the generalized path.
    - P3a-2. [ ] **NS->S `psa_*` client veneers** (`psa_framework_version`,
      `psa_version`, `psa_connect/call/close`) aliasing the existing
      `WolfTrust_FFM_*` + `wt_ffm_framework_version/service_version`, exported to
      the NS guest through the `.gnu.sgstubs` veneer table. Gate: NS guest calls
      `psa_framework_version()` across the veneer and gets the right value.
    - P3a-3. [x] **Build integration** — compile Arm `val/` NSPE +
      `ff/partition/{server,client}_partition.c` into the images: server/client
      partitions into the secure image as scheduled SPs, val NSPE + `psa/client.h`
      shim into the Zephyr NS guest. Dominant unknown; Mac cannot target-compile,
      so it is M33MU-only. Gate: links + boots clean. (`claude-fable-5`.)
    - P3a-4a. [x] **Stand up val NSPE in the guest, run i001**: compile Arm's
      unmodified val NSPE lib + i001 body + a minimal NS PAL (`pal_print`->
      console; uart/wd/terminate stubs) + NSPE `psa_connect/call/close` shim into
      the Zephyr guest; generate the NSPE `test_entry` `.inc` lists; call
      `val_entry()`. i001 is version-only (no `psa_call`) so the existing 1+1
      veneer suffices. Gate: `val_entry` runs a real Arm test against the SPM and
      i001 PASSes on M33MU.
    - P3a-4b. [x] **Widen the call veneer to N-vec, run i003**: extend the
      `WolfTrust_FFM_Call` ABI from 1 invec+1 outvec to arrays (up to
      `PSA_MAX_IOVEC=4`) with out-len writeback, and lift the SP-side transport
      cap `WT_SPM_SP_IOVEC` 2->4 (i003's client checks also run inside the
      client SP with 3 in + 4 out). `wt_ffm_call` already takes arrays. Run
      i003 (multi-vector `psa_call`/`psa_read`/`psa_skip`/`psa_write`)
      end-to-end: the client test's `psa_call` reaches the real `server_main`
      through the SPM. i002 is a `panic_test` (needs P5), so i003 is the first
      non-panic connect/call test. Gate: confboot `TOTAL PASSED : 2` on M33MU.
  - P3b. [x] **PAL driver plane + DRIVER partition.** As built: NS val's
    `pal_nvm_read/write` route over IPC to `DRIVER_NVMEM_SID` (0xFC03) with
    the suite's `nvmem_param_t` protocol, so NS and SPE val share the one
    driver-served store (`conformance_pal.c`; needed 4b's N-vec veneer).
    Split out: SPE `pal_print` -> real UART needs the driver domain's
    manifest MMIO grant, which is P4's ingestion mechanism -> moved to P4;
    WDG stays a no-op (M33MU models no watchdog; timers are P6). Gate:
    confboot `TOTAL PASSED : 2` with the shared store live.
  - P3c. [x] **Full val dispatcher + test list; iterate the non-IRQ /
    non-isolation subset to green** (`val_dispatcher`, `.acs_test_info` publish,
    `execute_non_secure_tests` + `switch_to_secure_client`). Exclude IRQ
    (`psa_eoi`) and MMIO-isolation tests (P4). M33MU-gated per green increment.
    - P3c-1. [x] **First subset green (i001, i003, i071, i088).** Full test
      list wired; `gen_tests_list` range `1 90` with a build-time `skip`
      schedule (mk `testsuite_sched.db`, DB untouched) dropping the tests that
      need a runtime capability we lack; `memmove` via `libc_stubs.c`; pid.h
      bare-name aliases (`CLIENT_PARTITION`); `PSA_LIFECYCLE_*` masks. Gate:
      confboot `TOTAL PASSED : 4`. Landed with P3c-2 as part of the six-test
      green (`96fee67`); the epilogue USGFLT did not reproduce on the fixed
      tree.
    - P3c-2. [x] **Doorbell + psa_wait signal-mask scheduler completeness —
      land i058, i063** (pulls P6 #35 scope forward for these two; i021/i067
      stay deferred for IRQ/heap). The psa_wait signal-mask *primitive* is
      already correct (`wt_ffm_wait` returns NOT_READY on `(asserted&mask)==0`;
      `wt_spm_slot_ready` wakes only on a masked signal) — the gap is
      cross-partition doorbell-driven origination plus the three-party
      interleave, decomposed into ordered phases:
      - Phase A. [x] **Lock in psa_wait signal-mask semantics (host)**
        (`8f01063`). `tests/host/ffm` `test_wait_signal_mask` +
        `tests/host/spm_gate` regressions: an out-of-mask asserted signal keeps
        the wait blocking; `psa_wait` returns only `asserted & mask`. Guards the
        already-correct primitive. `make test` green.
      - Phase B. [x] **Doorbell state machine (host)** (`9c8781c`). Gate test
        `test_gate_doorbell_state_machine` proves the i058 Check-1 rule end to
        end (notify->DOORBELL, stays asserted across a second wait, `psa_clear`
        drops it). Also closed the real POLL/BLOCK gap: threaded the `psa_wait`
        `timeout` through `wt_spm_call_t` so `PSA_POLL` returns instead of
        blocking (i058's final poll); `wt_spm_call_would_block` now honors it.
        (Task #14.) `make test` green (spm_gate 90 checks).
      - Phase C. [x] **Doorbell-driven origination in the scheduler (host)**
        (`52d3f67`). `test_doorbell_origination` models i063's portable core: a
        doorbell-woken client originates an outbound connect through the
        SP-as-client gate; it asserts a server signal the server masks out, so
        it stays starved across the server's masked waits and is delivered only
        when the server waits on it, completing with `CONNECTION_REFUSED`.
        **Finding: the existing gate + runtime already carry this — no
        `wt_spm_sched_dispatch`/`wt_spm_slot_ready` change was needed.** So the
        target faults below are coroutine-choreography/epilogue bugs, not a
        missing capability. `make test` green (spm_gate 126 checks).
      - Phase D. [x] **Fix i058/i063 on target** (`3bac964` + `96fee67`, M33MU
        2026-08-14). Root cause was NOT a stale-handle race: the SP-side SVC
        transport re-issued any NOT_READY gate call, so a `PSA_POLL` wait miss
        (i058's post-`psa_clear` doorbell poll) spun the client coroutine
        forever — silent hang, no fault. Found via WT_CONFORMANCE-gated hang
        tripwires (WAIT-run spin guard + SysTick idle probe + diag-trap
        register dumps, kept in-tree). Transport now re-issues only calls that
        suspended (`wt_spm_call_would_block`). Same-class sibling: the crypto
        SP's WAIT lacked `timeout` after the POLL/BLOCK split (`1d37648`,
        host-guarded). i058 and i063 both `Result=Passed` under confboot.
      - Phase E. [x] **NS epilogue USGFLT** — did not reproduce on the fixed
        tree: confboot exits clean through `[EXPECT BKPT] Success`, exit 0.
        The 2026-08-13 fault was collateral of the pre-fix stall class.
      - Phase F. [x] **Full subset green + close P3 (target)** (2026-08-14).
        Six-test schedule confboot `TOTAL TESTS : 6` / `TOTAL PASSED : 6` /
        `TOTAL FAILED : 0`, `[EXPECT BKPT] Success`; `PASS: target/positive`
        and `PASS: target/crossdomain` rerun on the same tree; host
        `make test` green. Validation-log entry recorded.
- P4/P5. [ ] **Buckets (b)+(c): MMIO/IRQ isolation + reboot/NVM continuity.**
  Scout finding (2026-08-14): of P4's 7 tests, **6 are `panic_test`s**
  (`i047,i055,i057,i064,i065,i066` — deliberate programmer errors that pass only
  if the SPM panics the offending SP and the framework then RESUMES); only
  `i021` is a live UART-IRQ test. Nearly all of P5's 41 tests are the same
  panic-reboot shape plus survive-reset NVM. So P4 and P5 share ONE keystone —
  **panic → controlled reset → survive-reset NVM → val resumes from the boot
  flag** — with a real emulator-feasibility gate. Decomposed below; the Keystone
  (K) is done first, then P4/P5 test buckets hang off it. One M33MU-gated slice
  at a time; host evidence before every target run.

  - K. **Reboot-continuity keystone (panic→reset→resume).** The val panic_test
    protocol: set a boot flag in NVM → induce the panic → on the NEXT boot val
    reads the flag (`NVM_BOOT`/`NVM_PREVIOUS_TEST_ID`) and marks the panicked
    check passed, continuing. So each panic test resets the whole image; a run
    reboots once per panic test and resumes further each time. Today
    `wt_platform_panic` spins (`bkpt; for(;;)`) and NVM is RAM-backed — neither
    survives, so this is the gating subsystem.
    - K1. [x] **Reset feasibility probe — GO (emulator-source verified,
      2026-08-14).** Answered by authoritative inspection of the pinned M33MU
      (`danielinux/m33mu@c84792f7`) rather than a throwaway probe firmware:
      (a) default CPU is `stm32h563` (`src/cpu_db.c:89`, our runner passes no
      `--cpu`); (b) an `AIRCR` write with VECTKEY `0x05FA` + SYSRESETREQ bit 2
      requests a warm reset (`src/scs.c:582`) that re-runs firmware from the
      reset vector without reloading images (`src/main.c:6064`,
      `[RESET] ... reinitialising core`); (c) flash + option bytes survive it —
      the emulator's own `tests/firmware/test-stm32h563-dualbank/main.c:240`
      programs SWAP, does `AIRCR=0x05FA0004`, and reads it back intact on the
      second boot, over the same FLASH MMIO (`0x40022000`) wolfTrust's
      `port/stm32h563/hsm_flash.c` already drives; (d) bonus — `.noinit` RAM
      also survives (the dualbank `reset_marker`), a cheap boot-count detector
      for K3/K4. **GO: the panic-reboot bucket is feasible, not
      hardware-gated.** `wt_platform_system_reset` is built in K3 (where it runs
      through the production SPM path); the first target run of reset lands in
      K3/K4, not a disposable probe. Ledger: validation-log.md.
    - K2. [x] **Flash-backed survive-reset NVM — host-proven + conformance
      cross-build clean (2026-08-14).** The DRIVER partition's NVMEM PAL
      (`pal_nvmem_*`) now keeps a RAM shadow loaded from a reserved secure-flash
      sector (`WT_CONF_NVM_FLASH_BASE_S`, one sector below the wolfHSM NVM) at
      first use and writes through on every write. The unprivileged SP reaches
      the flash controller through a `WT_CONFORMANCE`-only NVM-sync op
      intercepted in the arch SVC layer (`wt_spm_svc_entry`) before the neutral
      FF-M gate; the flash erase/program stays in the tested `hsm_flash.c`
      (`wt_conf_nvm_flash_sync`). Host suite `tests/host/flash_nvm` drives the
      real PAL against a faithful flash model — write-through, whole-sector RMW
      preserves other fields, and reload-after-simulated-reset all green. The
      cross-reset **M33MU** proof (write NVM → real SYSRESETREQ → read back)
      lands in K4, where reset runs through the production panic path. Reserved
      sector, gate op, and hsm_flash primitive all compile+link clean in the
      conformance image (no warnings).
    - K3. [x] **Controlled panic-reset in the SPM — code + host-proven,
      cross-build clean (2026-08-14).** `wt_platform_system_reset`
      (AIRCR.SYSRESETREQ, `platform_stm32h563.c`) is the reset primitive. The
      neutral gate now sets a `must_panic` flag on the FF-M must-panic
      PROGRAMMER ERRORs — `psa_get`/`psa_read`/`psa_write` with an out-of-domain
      buffer (i047/i055/i057) — instead of only returning the error; the arch
      SVC layer (`wt_spm_svc_entry`), ONLY under `WT_CONFORMANCE`, resets on that
      flag. Production ignores it (fail-closed quarantine stays task #26). No
      NVM write in the fault path: val itself writes `BOOT_EXPECTED_NS` to flash
      (K2) before the offending call, so the reset alone is the recovery signal.
      Host: `spm_gate` suite asserts `must_panic` is set on the three bad-buffer
      ops and clear on valid / zero-length transfers (133 checks). The target
      panic→reboot proof is K4's single M33MU run (i047).
    - K4. [ ] **End-to-end resume proof (M33MU).** Un-skip ONE panic test
      (`i047`) only: run → SP panics → reset → reboot → val reads the boot flag →
      marks i047 passed → continues to the remaining schedule → clean
      `[EXPECT BKPT] Success`. Proves the whole loop before scaling. Gate:
      confboot with i047 included reaches its `Result=Passed` across a reboot.
  - P4.1. [ ] **Six panic isolation tests (needs K).** Un-skip
    `i047,i055,i057,i064,i065,i066`, wire into the schedule (gen_tests_list
    panic mode), M33MU green across their reboots. Confirm each induces the SP
    panic our SPM already raises (bad msg pointer, oversized vector, etc.).
  - P4.2. [ ] **i021 UART-IRQ + `psa_eoi` (own feasibility gate; supersedes
    task #13).** Probe whether M33MU delivers a USART peripheral NVIC line
    (SysTick works, but a peripheral IRQ is unproven). If yes: route real UART TX
    IRQ → driver SP's IRQ signal → `psa_wait`/`psa_eoi`. If no: hardware-gate it
    like K1, document, keep `i021` skipped. Independent of the reboot keystone.
  - P5.1. [ ] **Flash-NVM continuity, non-panic tests (needs K2).** The P5 tests
    that only need survive-reset NVM, not a panic (`i002` PSA_POLL/state,
    `i004-i012`, `i024-i027` where non-panic). Host + M33MU per increment.
  - P5.2. [ ] **Panic-reboot P5 tests (needs K).** The PROGRAMMER_ERROR + panic
    subset of `i048-i054,i068-i090`. Scale the K4 loop to the full panic set;
    watch the reboot count and the per-test boot-flag resume.
  - P5.3. [ ] **Watchdog-reset tests (own feasibility gate).** Any test that
    exercises the watchdog specifically needs a WDG model that resets on timeout
    (`platform_stm32h563.c` WDG is a no-op today; task-list P3b note). Probe
    emulator WDG support; hardware-gate if absent.
- P6. [ ] **Interrupt + scheduler completeness (large; needs P1).** Real
  partition IRQ delivery: FLIH asserts a `psa_signal_t` into a partition's
  `asserted_signals`, `psa_eoi` unmasks instead of panicking (`ffm_api.c:173-177`),
  and `psa_wait` honors PSA_BLOCK/timeout. m33mu HAS a real NVIC+SysTick (verified
  2026-08-11). Unlocks i058 doorbell, i063 mask, i002 block/poll server halves.
  Supersedes tasks #13/#14.
- P7. [ ] **Full suite green on M33MU + `make test-conformance` auto-detect**
  (present→full target suite, absent→host subset + explicit non-HW warning).
- P8. [ ] **TF-M baseline comparison** — same suite on TF-M vs wolfTrust, parity.
- P9. [ ] **Physical STM32H563/H5 bring-up + qualification** — the only milestone
  that makes "tested on the H5" literally true; all prior evidence is M33MU
  emulator. Separate track once the emulator suite is green.

## Phase 4 — Crypto, protected storage, and ITS

- [ ] Implement PSA Crypto service ownership through wolfHSM.
- [ ] Implement protected storage and ITS semantics, including WRITE_ONCE.
- [ ] Preserve backend failures and never treat storage errors as blank storage.
- [ ] Add per-client key namespace and restart/persistence tests.

## Phase 5 — Initial Attestation

- [ ] Consume authenticated wolfBoot/DICE measurement handoff.
- [ ] Encode PSA Initial Attestation EAT claims with wolfCOSE.
- [ ] Sign COSE_Sign1 through a wolfHSM-protected IAK.
- [ ] Verify challenge, identity, lifecycle, measurements, signer IDs, and
  buffer/error behavior end to end.

## Phase 6 and later — boot, portability, and TFA expansion

- [ ] Complete authenticated update and rollback policy.
- [ ] Add a second Cortex-M port using the same core/service contracts.
- [ ] Define the Cortex-A secure-runtime adapter and TFA replacement boundary.
- [ ] Add architecture-specific isolation and IPC implementations without
  changing the generic manifest, service, or attestation APIs.
