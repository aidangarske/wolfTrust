# Phased implementation and test gates

Each phase is completed, tested, and reviewed before implementation starts on
the next phase. A failing gate keeps the current phase open. Later work may be
planned, but it must not be used to hide or defer a failure in an earlier
phase.

## Phase 1: dependency and clean-room baseline

Add the approved wolfSSL project dependencies and prove their required public
interfaces from wolfTrust-owned tests.

The wolfCOSE development dependency is pinned to commit
`c96270786043244f8a001595c5d59e79772eec72`, the two-commit head of wolfCOSE
PR #65. This commit includes the external signing seam and provides tagged and
untagged `COSE_Sign1` output plus exact size prediction. It remains an exact
development commit pin until the complete wolfTrust attestation and wolfBoot
DICE paths have been qualified. No future release tag is assumed.

The Phase 1 wolfCOSE gate must prove:

1. Production wolfTrust code consumes wolfCOSE through an API that does not
   expose wolfCOSE types to the Initial Attestation service.
2. The production adapter accepts encoded EAT payloads and routes ES256
   signing through an opaque callback suitable for wolfHSM.
3. Exact tagged and untagged `COSE_Sign1` sizes match produced output.
4. The external signing callback is invoked with an ES256 digest and can sign
   without exposing a private key to wolfTrust or wolfCOSE.
5. The pinned wolfSSL revision, latest stable wolfSSL, and wolfSSL master all
   build and pass the wolfTrust-owned integration test.
6. The Cortex-M33 Secure runtime links the constrained wolfCOSE profile.
7. The dependency commit is recorded by the wolfTrust gitlink.

Stop after this gate passes.

## Phase 2: portable isolation contracts

Implement architecture-neutral domain, partition, memory-region, interrupt,
security-state, privilege-state, restart, and lifecycle contracts. Validate
them with host unit tests, compiler coverage, sanitizers, Valgrind, and the
existing Cortex-M33 cross-build.

Stop after the portable contract gate passes.

## Phase 3: manifest, Secure Partition Manager, and IPC

Implement a generated manifest intermediate representation, isolated Secure
partition lifecycle, scheduling, PSA IPC connection state, message state, and
strict caller validation. Add negative tests for illegal memory access,
spoofed identity, invalid handles, queue exhaustion, and partition faults.

Stop after the isolation and IPC gate passes.

## Phase 4: Crypto and trusted storage

Run Crypto, Protected Storage, and Internal Trusted Storage as independently
isolated Secure partitions. Route private-key and persistent-key operations to
wolfHSM through wolfPSA, and enforce per-client identity and write-once
semantics across backend failures and restarts.

Stop after the service correctness and isolation gate passes.

## Phase 5: Initial Attestation

Run Initial Attestation as an isolated Secure partition. Consume authenticated
DICE evidence and measurements handed off by wolfBoot, obtain the Initial
Attestation Key operation from wolfHSM, encode the required EAT claims with
wolfCOSE CBOR, and emit a tagged `COSE_Sign1` token through the wolfCOSE
external signing seam.

Stop after deterministic claim tests, token decode and verify tests, negative
evidence tests, key-isolation tests, and replay and lifecycle tests pass.

## Phase 6: authenticated boot, runtime verification, and update

Complete the wolfBoot to wolfTrust authenticated launch, DICE handoff,
measured guest launch, rollback and recovery behavior, runtime verification,
and firmware update service.

Stop after the full emulated boot and update gate passes.

## Phase 7: operating-system integrations

Expose an operating-system-neutral non-secure client ABI and add thin Zephyr
and FreeRTOS integrations. Run the same PSA client behavior and isolation
tests from both operating systems.

Stop after both operating-system gates pass.

## Phase 8: hardware and port qualification

Qualify the complete chain under STM32H563 M33MU and on the available H5 and
C5 hardware. Keep architecture mechanisms, SoC policy, board description, and
application configuration separate so a new port supplies only the layers the
new target changes.

Stop after emulation and hardware evidence agree.

## Phase 9: parity, security review, and release qualification

Close the PSA and TF-M compatibility register, execute conformance and abuse
tests, complete security review, document migration from TF-M, and qualify the
dependency commits that form the release candidate.
