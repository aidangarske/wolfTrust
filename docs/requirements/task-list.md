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

2. [ ] Fill the FF-M API gaps: real `psa_notify`, `psa_clear`, `psa_eoi`, and
   honor the `psa_wait` timeout (`src/ffm_api.c:109,159-174` currently panic /
   ignore). Requires adding doorbell + interrupt signal state to the runtime
   engine (`src/ffm.c`), not just the API shim, plus host tests.
3. [ ] Wire the FF-M IPC runtime into the production boot path: `src/main.c`
   must `wt_ffm_init` + `wt_ffm_api_bind`, and the SPM must dispatch `psa_call`
   to registered services. (Runtime is compiled but never invoked today.)
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
    Two sub-gaps found while wiring the host-viable subset, needed before more
    tests can be added: (a) add an `UNSPECIFIED` service version policy to
    `WT_SERVICE_VERSION_*`/`ffm.c` — blocks `i002,i003,i010,i011,i026,i048-
    i053,i058,i063,i090`; (b) give `test_dispatch()` real per-service logic
    instead of a generic wait/get/reply(SUCCESS) — blocks `i027` (connection
    drop) and any future test needing service-specific server behavior.
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
