# wolfTrust as a Trusted Firmware-M replacement

This document is the outward-facing compatibility record for using wolfTrust in
place of Trusted Firmware-M (TF-M). It states, per API area, the specification
and version wolfTrust implements, the as-built status, and the in-tree evidence;
it records every deliberate deviation from a strict TF-M or PSA implementation
with a justification; and it gives a migration path for an existing
PSA-Certified application. The requirement-level baseline lives in
`docs/requirements/compatibility.md`; this document reports what the tree
actually ships.

## Product boundary

The released product is wolfBoot, wolfTrust, the selected wolfSSL project
dependencies, and the product application. It contains no TF-M runtime and no
TF-M generated build product. An existing standards-compliant application
replaces its TF-M provider with the wolfTrust client library and keeps its
`psa_*` source calls; build and board configuration may change, application
security behavior is not rewritten.

## Compatibility register

| Area | Specification and version | Status | Evidence |
| --- | --- | --- | --- |
| Framework model | Arm FF-M 1.0 IPC, with FF-M 1.1 framework and isolation discovery | 1.0 IPC implemented under a published bounded per-call transfer budget (`WT_FFM_TRANSFER_BYTES`, deviation register); 1.1 exposed as discovery only (stateless, SFN, and memory-mapped IOVEC are reserved but unadvertised) | `port/stm32h563/manifest.json:5` (`isolation_profile: 3`); `docs/requirements/framework.md:41-46`; framework-version discovery derives from the loaded manifest and is clamped to the compiled public contract (`src/ffm.c` `wt_ffm_framework_version`) |
| Client IPC API | FF-M 1.0 | Full: `psa_framework_version`, `psa_version`, `psa_connect`, `psa_call`, `psa_close` | `include/psa/client.h` |
| Secure Partition API | FF-M 1.0 | Full: `psa_wait`, `psa_get`, `psa_read`, `psa_skip`, `psa_write`, `psa_set_rhandle`, `psa_reply`, `psa_notify`, `psa_clear`, `psa_eoi`, `psa_irq_enable`, `psa_panic` | `include/psa/service.h` |
| Crypto | PSA Crypto API, provided by the wolfPSA client over the mediated relay | Client-level API parity through the wolfPSA/wolfCrypt stack; wolfTrust serves crypto as an opaque wolfHSM wire-packet relay (`SERVICE_HSM`), not a per-function PSA Crypto IPC surface | `lib/wolfPSA/wolfpsa/psa/crypto.h:25-26` (declares 1.4); `include/wolftrust/services/hsm_relay.h:27-33`. Version reconciliation: see open items |
| Internal Trusted Storage | PSA ITS 1.0 | Full core: `psa_its_set`, `psa_its_get`, `psa_its_get_info`, `psa_its_remove`; per-caller namespaced | `include/psa/internal_trusted_storage.h:31-32`; `src/services/storage_service.c` (`SERVICE_ITS`) |
| Protected Storage | PSA PS 1.0 | Core `set`/`get`/`get_info`/`remove` full, AES-GCM sealed with rollback protection; optional `psa_ps_create`/`psa_ps_set_extended` return `NOT_SUPPORTED` (`psa_ps_get_support()` reports 0) | `include/psa/protected_storage.h:40-41`; `src/services/storage_service.c:189-194` |
| Initial Attestation | PSA Initial Attestation 1.0, provided by the wolfPSA client | Full: `psa_initial_attest_get_token`, `psa_initial_attest_get_token_size`; EAT claims in a COSE_Sign1 token served by the scheduled `SERVICE_ATTEST` partition | `lib/wolfPSA/wolfpsa/psa/initial_attestation.h:32-33`; `src/services/attestation_service.c`, `attestation_cose.c`; `docs/requirements/framework.md:210` |
| Firmware Update | PSA Firmware Update 1.0 | Full pre-reboot state machine (`query`/`start`/`write`/`finish`/`install`/`cancel`/`clean`/`reject`/`request_reboot`); the optional TRIAL/accept flow is not offered (deviation) | `include/psa/update.h:37-38`; `src/services/fwu_service.c` |
| Status codes | PSA Status Code API 1.0 | Maintained subset of the PSA status namespace, by design | `include/psa/error.h` |
| Security lifecycle | FF-M 1.0 chapter 5 | `psa_rot_lifecycle_state` reports the platform lifecycle; state and implementation-substate masks are spec-accurate (`0xff00` / `0x00ff`) | `include/psa/lifecycle.h`; `src/arch/armv8m/spm_sp_api.c:227` |
| Isolation | FF-M Level 3 | Full: every Secure Partition is isolated from every other partition, and the privileged SPM from all partitions and Non-secure domains | `port/stm32h563/manifest.json:5`; `include/wolftrust/ffm_domain.h` |
| Boot and authentication | wolfBoot authenticates wolfTrust; wolfTrust measures and authenticates each guest | Full measured chain with anti-rollback; runtime re-verification available | `include/wolftrust/boot_handoff.h`, `include/wolftrust/guest_verify.h`; `docs/requirements/framework.md:127` |
| Reference platform | STM32H563 (NUCLEO-H563ZI), emulator and silicon | Qualified on both the M33MU emulator and hardware | `docs/requirements/validation-log.md` |

## Deviation register

Each entry is either **parity-or-better** (wolfTrust is at least as strict as a
conforming TF-M build, called out so a migrating application is not surprised)
or a **scoped roadmap** item (a specification feature not exposed in the first
production profile, gated for a later release), not a silent gap.

### Committed-install firmware update — no TRIAL/accept (scoped roadmap)

wolfTrust installs an update at the next authenticated-launch and anti-rollback
reboot instead of offering the optional PSA Firmware Update TRIAL state.
`psa_fwu_accept` returns `PSA_ERROR_NOT_SUPPORTED`, and no component persists
`TRIAL`/`REJECTED`/`UPDATED` across the swap. The wolfBoot authenticated-launch
and anti-rollback path is the trust anchor, so the update is live only after it
re-authenticates the swapped image; a self-test-then-accept window would require
a second mutable trust decision after launch. The pre-reboot state machine is
complete. Evidence: `include/psa/update.h:26-27`, `src/services/fwu_service.c`
(`WT_FWU_OP_ACCEPT` returns `NOT_SUPPORTED`), `docs/requirements/framework.md`
(`WT-FWU-0002`).

### Stateless services not exposed in the first profile (scoped roadmap)

Every shipped service is connection-based. The manifest schema validates the
FF-M 1.1 stateless fields for forward compatibility, but a 1.0 build rejects a
stateless service (`WT_MANIFEST_ERROR_SERVICE` / `_FEATURE`), and `psa_connect`
on a non-connection-based service returns `PSA_ERROR_NOT_SUPPORTED`.
Framework-version discovery derives from the loaded manifest and is clamped to
the compiled public contract (`PSA_FRAMEWORK_VERSION`), so every caller path of
this build reports 1.0 and never advertises the unenforced stateless
capability. No first-profile
service needs the stateless model; exposing unused routing would only add attack
surface. Evidence: every service entry in `port/stm32h563/manifest.json` and
`manifest-vnet.json` (`"connection_based": true`),
`docs/requirements/framework.md` (`WT-FFM-0042`, `WT-FFM-0043`),
`docs/requirements/validation-log.md`.

### Memory-mapped IOVEC disabled in Level 3 (parity-or-better)

Services use SPM-mediated `psa_read`/`psa_skip`/`psa_write` copies; client memory
is never mapped into a service partition. Direct mapping would weaken the Level 3
guarantee, so a partition's MPU table needs only its own code and private
regions and faults on any cross-partition or SPM data access. A future port may
enable memory-mapped IOVEC only with an architecture-specific proof that the
profile stays enforced. Evidence: `docs/requirements/compatibility.md:48-52`,
`docs/requirements/framework.md:63` (`WT-FFM-0041`),
`include/wolftrust/ffm_domain.h:64-71`.

### SFN partition model reserved, not advertised (scoped roadmap)

The manifest intermediate representation reserves the IPC and SFN model
distinction. The shipping STM32H563 build advertises only IPC
(`--supported-features 0x1` in `mk/secure-armv8m-stm32h563.mk`), so its generation
rejects an SFN partition, and the runtime fails closed regardless: `wt_ffm_init`
(`src/ffm.c`) refuses any partition whose model is not IPC before the SPM starts.
The generator itself remains a general FF-M tool and accepts an SFN partition for
a target that advertises the SFN feature, so adding SFN later needs no generator
change and does not change the common SPM boundary. Evidence:
`docs/requirements/framework.md:44` (`WT-FFM-0043`);
`tests/host/manifest/test_generator.py` (`test_sfn_zero_signal_is_accepted`,
generation under an SFN-advertising target).

### Partition entry is compile-time, not manifest-dispatched (parity-or-better)

Every Secure Partition is a coroutine inside one monolithic secure image, so
each is launched by its compile-time entry function in `wt_ffm_boot_start_sched`
(`src/ffm_boot.c`). The manifest `domain->entry_point` is a boot integrity gate,
not a dispatch address: `wt_domain_validate_entry_and_stack` (`src/domain.c`)
requires it to lie inside the domain's own executable region, and in this build
it equals the domain's flash base rather than a linked function address, so
branching through it would fault. Per-binary entry-address dispatch (a generator
emitting linker-resolved entry symbols) is a roadmap item for a multi-image port
and does not change the common SPM boundary. Evidence:
`src/ffm_boot.c` (`wt_ffm_boot_start_sched`); `src/domain.c`
(`wt_domain_validate_entry_and_stack`); `tools/manifest/generate.py:451`.

### Shared image text in every SP thread domain (scoped deviation)

The same monolithic image means every scheduled Secure Partition executes
shared library code (libc, wolfCrypt, the SP API), so each SP thread MPU table
grants the image text RX up to `_e_secure_text` and the image tail read-only
XN, alongside the domain's declared non-executable resources
(`wt_spm_sched_add_common`, `src/arch/armv8m/spm_svc.c`). The manifest's
narrow per-SP executable window lies inside that shared text and is not
separately enforced; code is immutable flash, never writable, and all private
data stays confined to the declared domain resources, so the deviation is
confined to execute/read visibility of shared code. Per-partition text
narrowing is tracked (task #26) and lands with the multi-image port above.
Evidence: `src/arch/armv8m/spm_svc.c` (SP table build); the cross-domain and
`vnetneg` negatives in `docs/requirements/validation-log.md`.

### Single mediated path — raw wolfHSM transport retired (parity-or-better)

Every non-secure client request reaches a secure service only through the SPM
CMSE gateway; the raw non-secure-to-wolfHSM transport is retired from
production. The default Secure build exports only the five `WolfTrust_FFM_*`
veneers, and a build-time symbol check rejects any other non-secure-callable
veneer. This is stricter than a typical TF-M deployment; it is recorded here so
a migrating application knows the direct-transport shortcut is intentionally
absent. Evidence: `docs/requirements/framework.md:156` (`WT-FFM-0054`),
`docs/requirements/validation-log.md` (single-mediated-path milestone),
`README.md:18-25`.

### Crypto served as a mediated relay, not a PSA Crypto IPC partition (parity-or-better)

TF-M ships a crypto partition with a per-function PSA Crypto IPC surface.
wolfTrust instead mediates opaque wolfHSM wire packets through one service
(`SERVICE_HSM`) and lets the wolfPSA/wolfCrypt client provide the PSA Crypto
API shape above it, binding each request to the SPM-stamped caller so a guest
cannot reach another guest's keys or the attestation signing key. The
application still calls the `psa_*` crypto functions; the mediation boundary is
different and tighter. Evidence: `include/wolftrust/services/hsm_relay.h:27-33`,
`docs/requirements/framework.md` (`WT-FFM-0046`, `WT-FFM-0047`, `WT-FFM-0059`).

### Vault is Secure-only; virtual network is off by default (parity-or-better)

`SERVICE_VAULT`, the key/object/counter/entropy backend behind ITS, PS, and
crypto, has no Non-secure access (`"nonsecure_clients": false`). The optional
mediated virtual-network switch (`SERVICE_VNET`) is compile-time gated and absent
from the base manifest; enabling it must not reintroduce any non-secure-callable
entry point outside the FF-M client ABI, and when enabled the partition runs
unprivileged in its own manifest domain — image code, its stack, and a dedicated
vnet data band carved from the RAM tail — with no scheduler path remaining that
can grant a partition the whole Secure address space (`WT-FFM-0065`). Evidence:
`port/stm32h563/manifest.json` (`SERVICE_VAULT`),
`docs/requirements/framework.md:101,171-173`; the `vnet`/`vnetneg` scenarios on
M33MU and H563 silicon.

### PSA Crypto multi-part operations not implemented (parity-or-better)

wolfTrust implements no PSA Crypto multi-part operation. `PSA_OPERATION_INCOMPLETE`
is defined in `include/psa/error.h` for parity with the published PSA status
namespace and pinned by `tests/host/psa_headers`, but it has no runtime consumer:
a single-shot service never returns it. Evidence: `include/psa/error.h`;
`tests/host/psa_headers/main.c`.

### Bounded IPC transfer budget (parity-or-better)

A single FF-M call's aggregate input, or aggregate output, is bounded to
`WT_FFM_TRANSFER_BYTES` (1024) by the SPM copy buffer; a larger aggregate is
refused with `PSA_ERROR_INVALID_ARGUMENT` — the status the Arm ACS itself pins
for an oversized vector set, which is why the framework does not substitute
`PSA_ERROR_INSUFFICIENT_MEMORY`. Level 3 copies client memory rather
than mapping it, so the budget is a fixed, deterministic SRAM allocation, not a
dynamic one, which is a security property of the zero-allocation design. Every
advertised public maximum is derived from this budget so it is actually
deliverable: `PSA_FWU_MAX_WRITE_SIZE` (1008) is the budget minus the marshalled
request header, pinned by a compile-time guard and a boundary round-trip in
`tests/host/psa_ffm_client`. Every shipped service and the full Arm ACS run
within the budget; a service needing more streams across successive calls. A
future port may enlarge the budget where SRAM allows. Evidence: `include/wolftrust/ffm.h:36` (`WT_FFM_TRANSFER_BYTES`);
`src/ffm.c` (vector preparation); the Arm ACS runs in
`docs/requirements/validation-log.md`.

### Zero-capacity attestation token buffer status (ACS-pinned)

`psa_initial_attest_get_token` with a non-NULL token buffer of capacity zero
returns `PSA_ERROR_INVALID_ARGUMENT`, not `PSA_ERROR_BUFFER_TOO_SMALL`: the Arm
ACS attestation suite pins that status (test_a001 check 8, "zero as token
size"), and the ACS run on M33MU and H563 silicon is the conformance
authority. A nonzero-but-small capacity returns `PSA_ERROR_BUFFER_TOO_SMALL`
as the API specifies. Evidence: the `devattest` runs in
`docs/requirements/validation-log.md`.

### Conformance authority is the Arm ACS on hardware (methodology)

The authoritative FF-M conformance evidence is the Arm Architecture Compliance
Suite run unmodified on M33MU and on H563 silicon (the `confboot` scenario,
85/89 applicable tests). The host-side `tests/host/psa_ff_upstream` fixture is a
fast smoke cross-check whose server dispatch is keyed to the suite's
expectations rather than an independently spec-derived clean-room server, so it
is treated as a sealed oracle, not a second conformance authority. Evidence:
`tests/host/psa_ff_upstream/README.md`; the Arm ACS silicon runs in
`docs/requirements/validation-log.md`.

## Open reconciliation items

- **PSA Crypto version — tracked pending pin bump.** The PSA Crypto API 1.5
  baseline is the target. The vendored client is temporarily held at API
  version 1.4 (`lib/wolfPSA/wolfpsa/psa/crypto.h:25-26`) on the minimal wolfPSA
  branch `wolftrust-v5.9.1-minimal` (pin `dd557dc`); the pin bumps once the
  upstream wolfPSA fix merges, tracked in `docs/requirements/task-list.md`
  ("bump the wolfPSA submodule pin after the upstream PR merges"). No action is
  needed beyond that tracked bump and re-qualification.

The Initial Attestation baseline was corrected to 1.0
(`docs/requirements/compatibility.md`) to match the shipped wolfPSA client
(`lib/wolfPSA/wolfpsa/psa/initial_attestation.h:32-33`); it is no longer an open
item.

## Migration from TF-M

An existing PSA-Certified application swaps its TF-M provider for the wolfTrust
client library and keeps its `psa_*` source calls. Security behavior is not
rewritten; build and board configuration change.

1. **Replace the provider, keep the calls.** Remove the TF-M secure build and its
   generated interface, and link the wolfTrust client library. The client IPC,
   crypto, storage, attestation, and firmware-update calls compile unchanged
   against wolfTrust's `psa/*` headers and the wolfPSA client.
2. **Confirm header parity.** wolfTrust's `psa_msg_t` member order (`type` before
   `handle`) and lifecycle masks (`0xff00` / `0x00ff`) match the specification
   and the published TF-M interface headers, so a partition ported from a TF-M
   layout sees the same structures.
3. **Map the manifests.** Translate each TF-M partition manifest into a wolfTrust
   manifest entry: domain id, framework version (1.0, IPC), model (IPC),
   services (SID, version, connection-based, Non-secure access), and
   dependencies. The generator rejects an over-declared or unenforced feature, so
   a mismatched manifest fails the build rather than degrading silently.
4. **Select an enforceable isolation profile.** Choose the profile the platform
   can enforce (Level 3 on the reference platform). A profile that exceeds the
   platform's capability fails the build.
5. **Adjust firmware-update expectations.** An application relying on the
   TRIAL/accept flow moves to the committed-install model: the update is live
   after the next authenticated reboot. Remove `psa_fwu_accept` handling — it
   returns `NOT_SUPPORTED`.
6. **Route any direct-transport code through the SPM.** Code that reached an HSM
   or crypto backend outside the SPM must issue `psa_call` to the mediating
   service; the raw transport is absent in production.
7. **Validate.** Run the application's own PSA conformance and behavior tests
   against the wolfTrust client. The compatibility register above lists the
   surface wolfTrust verifies on the emulator and on silicon.

## References

The pinned specifications are recorded in `docs/requirements/sources.md`. The
requirement-level baseline and profile policy are in
`docs/requirements/compatibility.md` and `docs/requirements/framework.md`; the
per-change evidence is in `docs/requirements/validation-log.md`.
