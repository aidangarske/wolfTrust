# Phased implementation and test gates

Each phase is completed, tested, and reviewed before implementation starts on
the next phase. A failing gate keeps the current phase open. Later work may be
planned, but it must not be used to hide or defer a failure in an earlier
phase.

## Mandatory phase revalidation gate

At the end of every phase, rerun the complete validation ladder against the
exact phase commit before starting the next phase:

1. Host unit and integration tests, including the phase's negative tests.
2. The full STM32H5 M33MU wolfBoot to wolfTrust lifecycle, with both the
   Zephyr and FreeRTOS guest matrix entries.
3. A real STM32H563 H5 hardware smoke test covering secure boot, wolfTrust,
   wolfHSM, wolfCOSE/PSA behavior, and guest output relevant to that phase,
   when target hardware is available.

The M33MU result is the active acceptance gate in the current no-hardware
environment and must be captured in a log tied to the exact phase commit. A
hardware result must also be captured and recorded when a target is available;
hardware qualification remains explicitly open until then. A successful host
or M33MU run must not be described as physical-target evidence, and a local
toolchain or board connection failure must be reported separately from a
firmware failure.

## Phase 1: dependency and clean-room baseline

Add the approved wolfSSL project dependencies and prove their required public
interfaces from wolfTrust-owned tests.

The wolfCOSE dependency is pinned to upstream `main` commit
`588232e6f2213133b48976f5cf3153b21fc7199c`. This upstream commit contains the
external signing seam and provides tagged and untagged `COSE_Sign1` output plus
exact size prediction. It remains an exact commit pin until the complete
wolfTrust attestation and wolfBoot DICE paths have been qualified; no release
tag is assumed.

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

The wolfBoot integration has two deliberately separate gates while the
secure-app handoff is being upstreamed. The standalone STM32H5 PSA build tracks
official wolfBoot `master`. The complete wolfBoot-to-wolfTrust lifecycle keeps
the validated wolfTrust-aware wolfBoot commit until its
`stm32h5-tz-wolftrust.config` and secure-app handoff settings exist upstream.
The lifecycle job must not be moved to official `master` merely because the
DICE/COSE changes have merged; doing so would test a different boot layout and
fail before wolfTrust starts.

Stop after this gate passes.

## Phase 2: portable isolation contracts

Implement architecture-neutral domain, partition, memory-region, interrupt,
security-state, privilege-state, restart, and lifecycle contracts. Validate
them with host unit tests, compiler coverage, sanitizers, Valgrind, and the
existing Cortex-M33 cross-build.

This phase defines and host-tests the validation contract. The H563 monitor now
compiles the validator into SPM startup and fails closed on validation errors
before scheduling any partition. The architecture-neutral partition API uses
an opaque runtime type, while the Armv8-M context and complete runtime type are
defined in the Armv8-M architecture layer. The port binds guest identity and
restart policy to generated secure-partition domains.

Stop after the portable contract gate passes.

## Phase 3: manifest, Secure Partition Manager, and IPC

Implement a generated manifest intermediate representation, isolated Secure
partition lifecycle, scheduling, PSA IPC connection state, message state, and
strict caller validation. Add negative tests for illegal memory access,
spoofed identity, invalid handles, queue exhaustion, and partition faults.

Generated manifests are validated before the SPM can enter its ready state, and
malformed or missing policy leaves it failed closed with the validator result
preserved. The H563 port binds generated identity, lifecycle, restart policy,
stack, memory, and interrupt resources before scheduling. Device and NSC
windows remain platform policy. Vector-read aliases and wolfHSM transport
windows are declared as explicit port capabilities and checked before first
dispatch. The transport must be contained in manifest-authorized writable,
non-executable memory. Host negative tests and the complete wolfBoot to
wolfTrust M33MU lifecycle exercise this binding.

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

Qualify the complete chain under STM32H563 M33MU first. Complete H5 and C5
hardware qualification when those targets are available. Keep architecture
mechanisms, SoC policy, board description, and application configuration
separate so a new port supplies only the layers the new target changes.

Stop after emulation and hardware evidence agree.

## Phase 9: parity, security review, and release qualification

Close the PSA and TF-M compatibility register, execute conformance and abuse
tests, complete security review, document migration from TF-M, and qualify the
dependency commits that form the release candidate.
