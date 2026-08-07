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

- [x] Generate a typed manifest and validate it before SPM ready state.
- [x] Fail closed on missing or invalid generated policy.
- [x] Keep the root `make test` aggregate and the focused suite Makefiles.
- [x] Run the wolfBoot to wolfTrust to Zephyr/FreeRTOS M33MU foundation gate.
- [ ] Generate the required PSA PID, SID, version, and partition signal headers.
- [ ] Separate Non-secure applications from actual Secure Partitions.
- [ ] Make immutable port capabilities authoritative during validation.
- [ ] Make generated resources, entry points, lifecycle, services, and policy
  authoritative in the production runtime.
- [ ] Link the bounded SPM IPC and lifecycle implementation into production.
- [ ] Implement the FF-M client and Secure Partition API surfaces.
- [ ] Run the pinned Arm PSA FF architecture tests from
  `tests/upstream/psa-arch-tests.rev` against wolfTrust and a TF-M baseline.
- [ ] Keep wolfTrust-owned FF-M security tests for handle integrity, bounded
  resources, memory scrubbing, pointer revalidation, restart, and isolation.
- [ ] Run each Secure Partition in a distinct Secure Level 3 protection domain.
- [ ] Route Initial Attestation and the RTOS framework probes through FF-M IPC.
- [ ] Pass the host and M33MU FF-M positive and negative suites on one commit.

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
