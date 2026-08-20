# Firmware Framework requirements

These requirements define the observable wolfTrust Secure Partition Manager
behavior. They are derived only from the published sources pinned in
`sources.md` and original wolfTrust security requirements. They do not
describe or depend on a TF-M implementation.

## Manifest and build requirements

| ID | Behavior | Failure | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- |
| WT-FFM-0001 | The manifest declares FF-M 1.0 or 1.1 and only selects features supported by that version and build profile. | An unknown version or unsupported feature fails generation. | SRC-FFM 4.1.1; SRC-FFM-EXT 2, 3.5.1 | Manifest version and feature tests | |
| WT-FFM-0002 | Every partition manifest supplies a unique symbolic name, persistent positive partition ID, RoT class, priority, stack size, and the entry information required by its model. | A missing, malformed, duplicate, reserved, or incompatible value fails generation. | SRC-FFM 3.2.1, 4.1; SRC-FFM-EXT 3.5.1 | Required-field, identity, and model tests | |
| WT-FFM-0003 | Every declared service has a unique SID and name, a nonzero version, a version policy, and an explicit Non-secure access policy. | A collision, invalid value, or contradictory policy fails generation. | SRC-FFM 3.3.1, 4.1.1 | Service identity and policy tests | |
| WT-FFM-0004 | Generated public headers bind partition names to IDs and service names to SIDs and versions. Generated partition headers bind declared services and interrupts to unique signals. | A symbol collision, reserved signal, or allocation beyond 28 general signals fails generation. | SRC-FFM 3.2.3, 4.1 | Generated-header golden tests | |
| WT-FFM-0005 | The generated runtime description is the sole production source for partition resources, services, dependencies, and policy. Its digest and generator version are recorded in generated output. | Missing or stale generated data fails the build, and runtime validation failure prevents scheduling. | SRC-FFM 3.2.2; WT-SYS-0011 | Stale-output, digest, and fail-closed boot tests | |
| WT-FFM-0006 | A partition may call only services listed as dependencies, cannot call a service it implements, and participates in no circular dependency. | An unauthorized, self, or cyclic dependency fails generation. | SRC-FFM 3.2.2 | Dependency graph tests | |
| WT-FFM-0007 | Stacks, heaps, code, data, MMIO, and interrupts are explicitly declared and fit the selected platform resources without prohibited overlap or sharing. | An unfulfillable resource request fails before signing. | SRC-FFM 3.2.2, 4.1; WT-PORT-0004 | Layout boundary and overlap tests | |
| WT-FFM-0008 | FF-M 1.1 manifests explicitly select IPC or SFN. FF-M 1.0 manifests select IPC implicitly. | A model unavailable in the selected build fails generation. | SRC-FFM-EXT 2.2, 3.5.1 | Model-selection tests | |

## Isolation and lifecycle requirements

| ID | Behavior | Failure | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- |
| WT-FFM-0010 | Code is executable and not writable, constant data is neither executable nor writable, and private data is not executable. | A policy that grants a prohibited access fails validation. A runtime violation faults the caller. | SRC-FFM 3.1.1, 3.1.2 | Memory-permission generation and fault tests | |
| WT-FFM-0011 | The Level 3 profile gives the SPM and every Secure partition a distinct protection domain. Private data, including stacks and heaps, is inaccessible to other partitions and Non-secure domains. | Cross-domain access faults the initiating context without exposing target data. | SRC-FFM 3.1.3 to 3.1.6; WT-SYS-0003, WT-SYS-0004 | Level 3 negative-isolation suite | |
| WT-FFM-0012 | The SPM accesses another domain only while implementing an API transfer and validates every external memory reference before use. | Invalid, overflowing, stale, or unauthorized references cause the specified programmer-error path without partial output. | SRC-FFM 3.1.5, 3.3.5; WT-SYS-0007 | Memory-reference abuse tests | |
| WT-FFM-0013 | Each IPC partition has one managed execution thread, begins at its generated entry point, and waits for signals after initialization. | Returning from the entry point or executing an invalid lifecycle transition is a partition fault. | SRC-FFM 3.2.3 | Entry, wait-loop, and return-fault tests | |
| WT-FFM-0014 | The SPM tracks blocked, ready, and running states, preserves preempted context, eventually delivers queued messages and asserted signals, and runs a ready Secure partition when the Non-secure environment is idle. | Invalid scheduling state fails closed. Resource exhaustion delays delivery but cannot discard an accepted request. | SRC-FFM 3.2.3, 3.2.4 | Scheduler state and eventual-delivery tests | |
| WT-FFM-0015 | Service, interrupt, and doorbell signals remain asserted until handled by the matching operation. Wait masks expose only requested asserted signals. | An invalid signal or handler operation triggers the programmer-error path. | SRC-FFM 3.2.3, 4.5 | Signal mask, persistence, and misuse tests | |
| WT-FFM-0016 | Partition identity and caller identity are derived from the active SPM execution context and remain stable across firmware updates. Secure IDs are positive and Non-secure client IDs are negative. | Supplied, forged, stale, or wrong-state identity is rejected. | SRC-FFM 3.2.1, 3.3.3; WT-SYS-0005 | Identity forgery and persistence tests | |
| WT-FFM-0017 | A partition panic or isolation fault terminates that partition and follows its declared restart policy. Unrelated restartable domains remain intact. | A mandatory service fault or exhausted restart policy triggers fail-closed platform recovery. | SRC-FFM 3.5; WT-SYS-0008 | Panic, containment, restart, and escalation tests | |

## Client and connection requirements

| ID | Behavior | Failure | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- |
| WT-FFM-0020 | The client API provides the published framework version, service version lookup, connect, call, and close behavior with source-compatible public types and constants. | Unsupported or unauthorized service lookup reveals no service version. | SRC-FFM 4.4; SRC-FFM-EXT 2.1 | Public-header and client API tests | |
| WT-FFM-0021 | Connect enforces SID existence, dependency policy, Non-secure access policy, requested version policy, and bounded connection capacity before allocating a caller-owned handle. | A policy error follows the caller-specific programmer-error behavior. Temporary capacity exhaustion returns a retryable busy result. | SRC-FFM 3.3.1, 4.4.3 | Connect policy and exhaustion tests | |
| WT-FFM-0022 | A connection follows the published pending-connect, connecting, idle, pending-request, active, disconnecting, error, and terminal transitions. | An invalid transition triggers programmer-error handling and cannot resurrect or reuse the connection. | SRC-FFM Appendix A | Connection state-model tests | |
| WT-FFM-0023 | Handles are nonzero SPM-managed references bound to the creating caller and object type. A consumed, closed, forged, or wrong-type handle is invalid. | Invalid handle use follows programmer-error behavior. Null close has no effect. | SRC-FFM 3.3.4, 4.4 | Handle ownership, type, lifetime, and forgery tests | |
| WT-FFM-0024 | A call accepts a nonnegative request type and at most four total input and output vectors. Zero-length vectors ignore their base addresses. | Invalid counts, arithmetic overflow, invalid references, or a concurrent request on one connection triggers programmer-error handling without partial output. | SRC-FFM 3.3.2, 3.3.5, 4.4.3 | Vector boundary, overflow, alias, and concurrency tests | |
| WT-FFM-0025 | A normal close sends one disconnection message, waits for its reply, releases all connection state, and invalidates the handle. | Repeated or invalid close follows the specified null-handle or programmer-error behavior. | SRC-FFM 3.3.3, 4.4.3, Appendix A | Close ordering and lifetime tests | |
| WT-FFM-0026 | Abnormal termination prevents later calls, delivers a cleanup disconnection, and requires close before final handle release. | No request after termination reaches the service and no connection state leaks into reuse. | SRC-FFM 3.3.3, Appendix A | Abnormal connection and cleanup tests | |
| WT-FFM-0027 | Notify raises the doorbell signal on a named partition. Clear consumes only the caller's own asserted doorbell signal. | Notifying an unknown partition or clearing an unasserted doorbell triggers programmer-error handling. | SRC-FFM 3.2.3, 4.5 | Doorbell notify and clear tests | |

## Service-side IPC requirements

| ID | Behavior | Failure | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- |
| WT-FFM-0030 | Wait returns the requested asserted signals. Get retrieves one queued message for exactly one asserted service signal and supplies type, caller identity, sizes, and connection state from SPM-owned metadata. | Invalid wait or get arguments follow programmer-error handling and reveal no other queue. | SRC-FFM 3.2.3, 3.3.3, 4.5 | Wait/get routing and isolation tests | |
| WT-FFM-0031 | Connect, request, and disconnect messages expose only the fields valid for their message type. | An operation that is invalid for the message type follows programmer-error handling. | SRC-FFM 3.3.3, 4.5 | Message-type matrix tests | |
| WT-FFM-0032 | Read and skip advance only within the selected input vector. Write advances only within the selected output vector and cannot exceed client capacity. | Invalid index, length, ownership, or message type triggers programmer-error handling without out-of-bounds access. | SRC-FFM 3.3.2, 4.5 | Read, skip, write, and short-buffer tests | |
| WT-FFM-0033 | Reply completes exactly one active message, publishes only successfully written output lengths, wakes the blocked client, and invalidates the message handle. | Duplicate, forged, stale, or wrong-owner replies trigger programmer-error handling. | SRC-FFM 3.3.3, 3.3.4, 4.5 | Reply atomicity and handle tests | |
| WT-FFM-0034 | A connection-based service may bind an opaque reverse handle while processing connect or request. The SPM returns it only on later messages for that connection. | Cross-connection, stale, or disconnect-time updates cannot change another connection. | SRC-FFM 3.3.3, 4.5 | Reverse-handle isolation tests | |
| WT-FFM-0035 | IPC queues, connections, messages, and transfer storage have static configured bounds in the Level 3 profile and are scrubbed before reuse. | Exhaustion returns the specified busy or memory result and never aliases an active object. | WT-SYS-0009, WT-SYS-0010 | Exhaustion, reuse, and zeroization tests | |

## FF-M 1.1 extension requirements

| ID | Behavior | Failure | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- |
| WT-FFM-0040 | A 1.1 build reports framework version 1.1, its actual isolation level, and whether memory-mapped IOVEC is available. | Feature discovery never advertises an unenforced capability. | SRC-FFM-EXT 2.1 | Feature-discovery tests | |
| WT-FFM-0041 | The initial Level 3 profile reports memory-mapped IOVEC unavailable and uses copied service transfers. | A manifest or service requiring unavailable direct mapping fails the build. | SRC-FFM-EXT 1.3.3, 2.1.2, 5 | Feature rejection and mapping-isolation tests | |
| WT-FFM-0042 | A 1.1 service explicitly declares connection-based or stateless operation. A stateless service has a unique generated handle, receives request messages only, and has no reverse handle. | Connect, close, or reverse-handle use on a stateless service triggers programmer-error handling. | SRC-FFM-EXT 4.2, 4.5 | Stateless routing, version, and misuse tests | |
| WT-FFM-0043 | The generator reserves IPC and SFN as distinct partition models. IPC is required for the first production gate; SFN is not advertised until its execution and API tests pass. | Selecting an unavailable SFN model fails generation. | SRC-FFM-EXT 2.2, 3 | Model capability and rejection tests | |

## Phase 3 acceptance gate

Phase 3 is complete only when generated manifests are the sole input to the
production SPM, all requirements through `WT-FFM-0035` have passing host and
Cortex-M33 tests, the selected requirements in `WT-FFM-0040` through
`WT-FFM-0043` match advertised features, and all of the following negative
tests pass:

1. Cross-partition read, write, execute, stack, heap, MMIO, and interrupt
   probes fault the initiating partition.
2. Caller IDs, connection handles, message handles, and reverse handles cannot
   be forged, shared, confused, or reused after release.
3. Invalid vectors, overlapping arithmetic, queue exhaustion, and interrupted
   transfers expose no partial output or stale data.
4. Invalid manifests, dependency cycles, resource overlaps, stale generated
   output, and unsupported capabilities fail before signing.
5. Partition panic and restart preserve unrelated domains, scrub reused
   resources, and escalate mandatory-service failures.

The existing H563 monitor is not a substitute for this gate. No Level 3 or
FF-M compatibility claim is made until the generated production path and its
negative tests pass.

## Phase 4 crypto and storage requirements

Crypto, Internal Trusted Storage (ITS), and Protected Storage (PS) run as
independently isolated Secure Partitions and reach the wolfHSM key/NVM vault
only through the SPM gate. Private keys never leave the vault — the stronger
posture than a Crypto partition holding key material in its own memory.

| ID | Behavior | Failure | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- |
| WT-FFM-0044 | Every ITS and PS object is owned by the SPM-authenticated caller partition. A caller reaches only its own `(owner, uid)` namespace on set, get, get_info, and remove. | A cross-owner access reveals nothing and returns does-not-exist, never another owner's data or metadata. | SRC-PSA-STORAGE, SRC-FFM 3.3.4 | Per-client storage isolation tests | |
| WT-FFM-0045 | An object created with `PSA_STORAGE_FLAG_WRITE_ONCE` cannot be modified or removed, and the flag survives reset. Enforced in SECURED and locked debug lifecycles, relaxed in provisioning. | A set or remove on a write-once uid returns not-permitted, before and after a `SYSRESETREQ` reboot. | SRC-PSA-STORAGE | Write-once persistence tests | |
| WT-FFM-0046 | Persistent and private keys created through the Crypto service live only in the wolfHSM vault. The Crypto partition operates on handles and never holds raw key material; a key owned by one partition is unusable by another. | A read or export of a non-exportable key, or cross-owner key use, returns not-permitted or does-not-exist with no key bytes disclosed. | SRC-PSA-CRYPTO | Key ownership and non-exportability tests | |
| WT-FFM-0047 | The Crypto, ITS, and PS partitions reach the wolfHSM vault only through the SPM gate, tagged with the caller identity the SPM stamps. No partition maps vault state into its own domain. | A partition that touches vault memory directly, or supplies a forged owner identity, faults or is overridden by the SPM-stamped identity. | SRC-FFM 3.2, WT-SYS-0009 | Gated-routing and domain-isolation tests | |
| WT-FFM-0048 | PS objects are AES-GCM encrypted and authenticated under a device-unique wolfHSM key with a fresh nonce per write, and are rollback-protected by a monotonic counter. | A tampered or replayed PS object returns invalid-signature or data-corrupt and yields no plaintext. | SRC-PSA-STORAGE | PS confidentiality and rollback tests | |

## Phase 4 acceptance gate

Phase 4 is complete only when Crypto, ITS, and PS run as isolated partitions
routed through the gated wolfHSM vault, `WT-FFM-0044` through `WT-FFM-0048` have
passing host and Cortex-M33 tests, the unmodified Arm `dev_apis` Crypto and
Storage suites pass on the production path, and the negative matrix proves a
compromised Crypto partition cannot read a key it owns, one client cannot reach
another's stored objects, write-once and rollback survive reset, and every
enabled capability has a matching negative test.
