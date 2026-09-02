# wolfTrust threat model

This note states what wolfTrust protects, who it defends against, where the
trust boundaries are, and how the assurance activities (the abuse and fuzz
suite, the isolation negatives, the conformance suite) map onto those
boundaries. It is scoped to the first production profile on the Armv8-M
reference platform.

## Assets

- The PSA Root-of-Trust secrets: the Initial Attestation signing key, the
  device-unique storage-sealing key, and the wolfHSM keystore contents.
- Integrity state: the anti-rollback version floors, the replay counters, and
  the sealed Internal Trusted Storage and Protected Storage objects.
- The privileged SPM itself: its code, its stacks, and the platform control it
  holds (flash programming, NVIC, MPU, the secure timer, system reset).
- Availability of the mediated services to well-behaved guests.

## Trust boundaries

1. **Non-secure to Secure (the primary boundary).** Every Non-secure guest
   reaches a secure service only through the single CMSE gateway and the SPM
   dispatch: `psa_connect` / `psa_call` / `psa_close` into a scheduled Secure
   Partition. The raw Non-secure-to-wolfHSM transport is absent in production
   (WT-FFM-0054). This is the boundary the fuzz suite targets.
2. **Secure Partition to Secure Partition.** FF-M Level 3 isolates each
   partition in its own MPU domain; a partition cannot read another's stack,
   keystore band, or the SPM (WT-FFM-0011). Privileged flash, entropy, and
   NVM-lock reach hardware only through SPM gate operations the dispatcher pins
   to the calling partition's identity.
3. **wolfBoot to wolfTrust to guest.** wolfBoot authenticates the wolfTrust
   image; wolfTrust measures and version-checks each guest against a record
   pinned into its own signed image before entry, and can re-measure a running
   guest to quarantine a tampered one.
4. **Non-secure guest to Non-secure guest.** Guest kernels run privileged in
   the Non-secure state (a Zephyr or FreeRTOS kernel needs its NVIC, SysTick,
   and MPU), so the per-guest NS MPU alone cannot contain a hostile kernel —
   privileged NS code can reprogram `MPU_NS`. The enforced boundary is the
   GTZC curtain: on every dispatch the monitor marks the whole shared guest
   RAM extent Secure at the block level (MPCBB) and reopens only the arriving
   guest's declared windows, so peer RAM rejects Non-secure transactions at
   the fabric regardless of NS privilege (`gtzcneg` negative). The NS MPU
   remains as fault-containment defense in depth. Residual: a guest can read
   the peer's flash-resident code image (public, measured content — no
   secrets are stored in guest flash).

## Adversary model

- A **malicious or compromised Non-secure guest** with full control of its own
  Non-secure execution: it may issue any sequence of `psa_*` calls with any
  arguments, forge handles, supply vectors pointing anywhere in its map or into
  Secure or another guest's memory, claim another client identity, and attempt
  to exhaust SPM resources. It may not execute Secure code or bypass the CMSE
  gateway (enforced by the veneer whitelist and the SAU/GTZC configuration).
- A **fault or power-loss adversary** that can interrupt the device at an
  arbitrary instant, including mid-flash-program and mid-NVM-transaction.
- Out of scope for this profile: physical side-channel and fault-injection
  attacks on the SoC, a compromised wolfBoot or hardware root of trust, and
  Denial of Service by a guest against its own availability.

## Attack surface and assurance mapping

| Surface | Adversary action | Assurance |
| --- | --- | --- |
| SPM IPC dispatch | Forged/stale handles, out-of-range SIDs, negative call types, over-count or overlapping iovecs, boundary-straddling buffers | Fuzz target `tests/fuzz/wt_spm_fuzz.c` (libFuzzer + ASan over `wt_ffm_connect`/`call`/`close`/read/skip/write), plus the curated FF-M negatives (`tests/host/negatives`, `exercise_ffm_negatives`) |
| Caller identity | A guest forging a wolfHSM client id to reach the IAK or another guest's keys, or an NVM-group request against a trusted object | Mediated-relay caller-isolation negatives (`hsmattackneg`), WT-FFM-0059 |
| Cross-partition memory | A caller vector into another partition's MMIO or the SPM | Isolation negatives (`crossdomain`, `keystoreneg`), the upstream `i048`-`i053` isolation cases, WT-FFM-0011 |
| Secure Partition misuse | An SP committing an FF-M programmer error through the gate | Must-panic quarantine (`panicneg`), WT-FFM-0063 |
| Firmware update | Malformed, oversized, or rolled-back candidate | FWU refuse-before-arm negatives, anti-rollback (WT-FWU-0003, WT-FFM-0050) |
| Attestation token | Malformed challenge, claim tampering | Golden vector + the upstream attestation conformance suite (`devattest`) |

## Fuzzing

The fuzz suite drives the Non-secure-to-SPM boundary the way a hostile guest
does: `tests/fuzz/wt_spm_fuzz.c` is a libFuzzer harness that reinterprets each
input as a stream of client operations against the production FF-M runtime
(`src/ffm.c`, `src/ipc.c`) under AddressSanitizer, mixing valid and forged
handles, real and fuzzed service ids, arbitrary call types, and adversarial
vector lengths and contents. `.github/workflows/fuzz.yml` runs a 60-second
smoke on every non-draft pull request and merge and a 600-second soak nightly
and on manual dispatch, uploading any crash, OOM, or timeout artifact. Seed
corpus and the token dictionary live beside the harness; the harness is also
replayable under a plain ASan build for local triage.

## Residual risks

- **NVM pool wedge on interrupted `AddObject`.** A power loss between the
  directory write and the object write of a keystore add can leave a partial
  entry that poisons later adds until the pool is reconciled. Tracked in
  `docs/requirements/task-list.md`; the intended mitigation is a vault-init
  reconciliation pass that discards a half-written trailing entry. The fuzz
  suite exercises the SPM boundary but does not yet inject power loss at the
  flash-transaction level; that is a hardware fault-injection activity scoped to
  release qualification.
- **Availability under resource exhaustion.** The IPC pools are statically
  bounded and scrubbed (WT-FFM-0035); a guest can still exhaust its own share
  and receive the specified busy result. The fuzzer exercises exhaustion but
  cross-guest availability guarantees are not a first-profile claim.
