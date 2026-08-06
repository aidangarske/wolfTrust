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
- [x] Bind H5 guest identity, security state, privilege state, and restart policy
  to generated domains before the first dispatch.
- [x] Keep the root `make test` aggregate and the focused suite Makefiles.
- [x] Map generated stack descriptors into guest reset context.
- [x] Define the vector/reset-PC contract: the authenticated Thumb reset PC
  must fall inside the manifest executable memory resource; exact instruction
  addresses are intentionally not pinned across rebuilds.
- [x] Map generated memory resources into the H5 memory windows and MPU
  envelope; device and NSC regions remain explicit platform capabilities.
- [x] Map generated interrupt resources into IRQ ownership/quarantine.
- [x] Represent HSM transport and vector-alias requirements as explicit port
  capabilities instead of hidden manifest assumptions.
- [x] Add negative runtime coverage for descriptor mismatch; generic manifest
  tests cover memory overlap and unauthorized interrupt ownership.
- [x] Run the complete wolfBoot → wolfTrust → Zephyr/FreeRTOS M33MU gate.

Phase 3 exit gate: no guest may be scheduled unless the generated manifest is
validated and all runtime-protection fields have been bound or explicitly
rejected as unsupported by the selected port.

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
