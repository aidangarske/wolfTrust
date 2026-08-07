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
3b-test. [ ] Add test evidence for 3b's `check_read`/`check_write`. Blocked
   the same way as `psa_eoi` (item 2a): the CMSE calls can't be host-tested
   (ARM-only intrinsics) and nothing calls `check_read`/`check_write` yet
   since `dispatch` is still a placeholder — no M33MU exerciser exists
   either. Closes naturally once item 3c gives `dispatch` a real caller;
   until then this must not be counted as tested.
3c. [ ] Migrate one real service through actual `psa_connect`/`psa_call`
   dispatch — the crypto hash KAT already proven direct-via-wolfHSM on M33MU
   is the natural first target (`SERVICE_CRYPTO`, SID `4097`, already
   declared in `port/stm32h563/manifest.json`). This is where `dispatch`
   stops being a placeholder and item 4 (route NS calls through FF-M instead
   of direct wolfHSM/wolfCOSE calls) actually starts. No purpose-built
   NS-to-Secure transport for wolfTrust's own `psa_connect`/`psa_call`
   exists yet (`WT_NSC_VENEER` is defined but unused). Reusing the existing
   Zephyr `tee` driver (`tests/firmware/zephyr-stm32h5/module/wolftrust-tee/`,
   a generic vendor-neutral Zephyr subsystem, not Arm/TF-M-specific — already
   proven end-to-end on M33MU for wolfHSM crypto/attestation) as the carrier
   for now: a new `tee_invoke_func` function ID dispatches into a new
   `cmse_nonsecure_entry` veneer wrapping `wt_ffm_connect`/`wt_ffm_call`,
   following the exact pattern already proven in
   `src/services/vnet/vnet_service.c` (`veneer_precheck` +
   `wt_platform_active_guest_id()` + paired `wt_cmse_check_ns_*` /
   `wt_cmse_check_in_guest_ns_*` validation).
3c-followup. [ ] Remove the TEE-driver dependency once purpose-built FF-M
   NSC veneers exist (`WT_NSC_VENEER`-based, directly exposing
   `psa_connect`/`psa_call`/`psa_close` without going through the generic
   `tee_invoke_func` indirection). TEE is a legitimate reusable carrier for
   now, not the long-term production transport.
4. [ ] Register the PSA services (crypto, attestation) behind SIDs and route NS
   calls through `psa_connect`/`psa_call` → SPM dispatcher instead of the
   current direct wolfHSM/wolfCOSE calls.
5. [ ] Separate Non-secure applications from actual Secure Partitions and run
   each Secure Partition in a distinct Secure Level 3 protection domain.
6. [ ] Make generated resources, entry points, lifecycle, services, and policy
   authoritative in the production runtime (not only at validation).
7. [ ] Route Initial Attestation and the RTOS framework probes through FF-M IPC.
8. [ ] Add the missing wolfTrust FF-M security tests: partition restart and
   cross-domain isolation (handle integrity, bounded pools, scrubbing, and
   pointer revalidation already covered in `tests/host/ffm/main.c`).
9. [ ] Add M33MU FF-M assertions: a positive `psa_connect`/`psa_call` round trip
   plus negatives (forged handle, oversized vector, cross-domain access) on the
   emulator path (`.github/workflows/stm32h563-build.yml`).
10. [ ] Expand `tests/host/psa_ff_upstream/` past the host-viable subset
    (`i001,i004-i008,i012,i024,i025,i067[SKIP],i071,i088`) to the full Arm
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
10a. [ ] Add an `UNSPECIFIED` service version policy to `WT_SERVICE_VERSION_*`
    / `ffm.c` (any client version accepted, no strict/relaxed check). Blocks
    wiring `i002,i003,i010,i011,i026,i048-i053,i058,i063,i090` in
    `tests/host/psa_ff_upstream/`.
10b. [ ] Give `test_dispatch()` in `tests/host/psa_ff_upstream/main.c` real
    per-service logic instead of a generic wait/get/reply(SUCCESS). Blocks
    wiring `i027` (connection drop) and any future test needing
    service-specific server behavior.
11. [ ] Pass the host and M33MU FF-M positive and negative suites on one commit.

The earlier Phase 3 validation proves the generated-policy bootstrap and the
H5 Non-secure guest-monitor lifecycle. It does not prove FF-M IPC or Level 3
Secure Partition isolation. Phase 3 remains open until the acceptance gate in
`framework.md` passes.

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
