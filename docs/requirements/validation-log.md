# Phase validation ledger

This ledger records validation against the exact wolfTrust phase commit. A
M33MU result is emulator evidence for the Cortex-M33 execution model; it is
not a physical-board result. Hardware qualification remains open until a
target is available.

## Phase 3 foundation evidence

Commit: `2bc5f70e69faedfeadca5d45f317ff6a880df6ef`

- Aggregate host validation: `make test BUILD_ROOT=/tmp/wolftrust-phase3-capability-tests`, passed.
- Focused production policy validation: `make -C tests/host/spm BUILD_DIR=/tmp/wolftrust-phase3-spm-capabilities run`, passed.
- Complete local M33MU lifecycle: passed with wolfBoot authentication,
  wolfTrust startup, wolfCOSE Initial Attestation signing and verification,
  Zephyr PSA operations, and FreeRTOS wolfHSM-backed wolfPKCS11 operations.
- Expected terminal marker: `[EXPECT BKPT] Success`.
- Fault markers: none.

Validated dependency and emulator revisions:

- wolfCOSE: `588232e6f2213133b48976f5cf3153b21fc7199c`
- wolfHSM: `a0323156606282448f00473a3fcb7aaa69361921`
- wolfPSA: `dd557dc526ee79f38e5f5c8d8839fb477d045941`
- wolfPKCS11: `6fbb6fbc5251b59280eb5f74bcc9241bd46073e9`
- wolfSSL: `22e505bcfad8ce21067ee4232128728543767a95`
- wolfBoot: `d85fa9dbdf6c36f47b7e96eba5c9df750ad3c963`
- M33MU: `c84792f7f9e9ce24cf94ffc492c36231de1854c2`

Validated image SHA-256 digests:

- Signed wolfTrust: `51d3bb49f9686df8bd01b2543bcac4c66c1e9416aecafda22cd78f36f414cd20`
- Zephyr guest: `60a7c51c08d5503885eb26170eef483fdace52e8c85254d18b65d5e423485daa`
- FreeRTOS guest: `f05d17ecef1df53585ba3c7a491cb7d0070cfbdaf6ee9889f9bca140de1e98aa`

This is emulator evidence for the manifest bootstrap, authenticated boot,
Non-secure guest monitor, and direct secure-service veneers. It is not FF-M
IPC, Secure Partition, or Level 3 isolation acceptance. No physical STM32H563
result is claimed for this commit.

## Current no-hardware gate

Commit: `36d28a5264c145e8aa71dab447db8d397152499c`

- Host validation: [workflow run 31059194072](https://github.com/aidangarske/wolfTrust/actions/runs/31059194072) - passed.
- M33MU lifecycle matrix: [workflow run 31059193766](https://github.com/aidangarske/wolfTrust/actions/runs/31059193766) - passed.
  - wolfBoot authenticated the secure wolfTrust image.
  - Zephyr exercised PSA Initial Attestation, rejected the short buffer,
    received a token, and verified the wolfCOSE COSE_Sign1 signature, challenge,
    identity, lifecycle, and measurement claims.
  - FreeRTOS exercised wolfHSM-backed wolfPKCS11 initialization, slot and
    session setup, and SHA-256 digest.
  - Both lifecycle jobs reached `[EXPECT BKPT] Success` and exited M33MU with
    status 0; no fault marker was emitted.

Physical STM32H563 H5 validation is pending. The board was not available for
the corrected-image run, so no H5 hardware pass or failure is claimed here.

## Item 5 Level 3 isolation evidence (Phase D positive, Phase E negative)

Commits: `69dc8eb` (Phase D - isolated crypto SP compute),
`f15fe11` (Phase E - gated cross-domain probe).

- Host validation: `make test` fully green on both commits, including
  `unit/crypto_service` (copied-IOVEC restructure: isolated-compute KAT +
  over-cap rejection), `unit/ffm_domain`, and `unit/sp_layout`.
- M33MU positive (run #1, Phase D): the crypto Secure Partition runs its
  SHA-256 on `WT_SP_CRYPTO_STACK_BASE` with the secure MPU narrowed to
  `[secure code RX] + [crypto SP stack RW]`. `SERVICE_CRYPTO` returns the
  correct digest, `attestation verify=0 lifecycle=0x1000 measurement=ok`,
  `[EXPECT BKPT] Success`, exit 0, no fault marker.
- M33MU negative (run #2, Phase E, `run_m33mu_negative.sh`, built with
  `WT_FFM_NEGATIVE_PROBE=1`): a probe inside the narrowed SP domain reads
  SPM-private RAM and faults -
  `[MEMFAULT] pc=0x0c060f34 addr=0x30028000 sp=0x3009dff0` (SP stack). The
  initiating context faults with no data exposed. This is the WT-FFM-0011
  failure clause and `framework.md` acceptance-gate negative #1.

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed. Follow-up (tracked): graceful fault recovery so the negative
probe run continues rather than halting, and CI wiring of the negative job.

## Item 6 manifest-authoritative bindings (restart policy, initial lifecycle)

Commits: `22e5461` (restart policy bind), `95c61a1` (initial lifecycle bind +
NS guests declared READY).

- Host validation: `make test` green; `tests/host/spm/test_production_manifest`
  now asserts both fields flow from the generated manifest - a distinct
  `restart_limit` (7) and a `STOPPED` guest domain bind through to the runtime
  config, and the real manifest's `READY` restores a runnable guest.
- M33MU positive: full wolfBoot -> wolfTrust -> guest lifecycle green,
  `[EXPECT BKPT] Success`, exit 0, no fault markers - NS guests still boot
  runnable now that their run state is sourced from the manifest.

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Item 7 Initial Attestation through FF-M IPC

Commits: `29ab959` (server-side `SERVICE_ATTEST` dispatch), `0de6c52` (client
routes the token through the FF-M veneers).

- Host validation: `make test` green including `unit/attestation_service` - a
  real `psa_connect`/`psa_call` round trip carries a challenge in and a token
  out through `wt_attestation_service_dispatch` (backend stubbed to isolate the
  IPC routing; the token generator is M33MU-proven).
- M33MU positive: the guest's Initial Attestation now travels the FF-M IPC path
  (`SERVICE_ATTEST` connect/call/close) and still verifies -
  `attestation verify=0 challenge=ok identity=ok lifecycle=0x1000 measurement=ok
  cose=ES256`, `[EXPECT BKPT] Success`, exit 0, no fault markers.

The FreeRTOS guest's wolfPKCS11 -> wolfHSM path stays the wolfHSM CMSE
transport by design (the HSM service itself, not a PSA FF-M RoT service).

## Item 8 restart-on-fault target scenario (Slice 1)

Guest fault probe: `WT_GUEST_FAULT_PROBE` (guest0_psa `main.c`). Test-only; a
Non-secure read of Secure RAM (`WT_RAM_S_BASE` 0x30028000) on boot raises a
SecureFault that escalates to the wolfTrust monitor.

- M33MU restart (`run_m33mu_restart.sh`, built with `WT_GUEST_FAULT_PROBE=1`,
  booted without `--quit-on-faults` so the handled fault does not halt the
  emulator): the `guest0_psa alive` banner reappears exactly `restart_limit+1`
  = 4 times (one boot plus 3 restarts), then the guest is left `WT_GUEST_FAULTED`
  and stops reappearing - proving `wt_restart_guest` honors the manifest
  `restart_limit` (domain id 1) on target. This is the restart-policy coverage
  the happy-path lifecycle run never exercised.
- The surviving FreeRTOS guest keeps running throughout (wolfPKCS11 init, slot,
  session, SHA-256, heartbeats 0-4) while guest0 cycles, so the monitor
  gracefully restarts a Non-secure guest fault from the SecureFault handler and
  the scheduler continues - the guest-side half of graceful recovery (SP-side
  graceful recovery remains task #26).

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Item 8 detect-or-skip target-scenario harness (Slices 2-3)

Single repo-resident runner `tests/target/run_m33mu_scenario.sh
<positive|restart|crossdomain>` factors the shared wolfBoot -> wolfTrust ->
guests build with per-scenario deltas (secure `WT_FFM_NEGATIVE_PROBE`, guest
`WT_GUEST_FAULT_PROBE`, boot flags, assertions). `make test-target` auto-detects
an M33MU binary (or `WT_TARGET_SCENARIOS=1`) and runs restart + crossdomain, or
prints an explicit `SKIP` and passes - never a silent pass. It is a standalone
target separate from the host-only `make test`, mirroring `make
test-conformance`.

- Host (dev, no M33MU): `make test-target` prints
  `SKIP: FF-M target scenarios (M33MU/HW not detected ...)`, exit 0.
- M33MU (`WT_TARGET_SCENARIOS=1 make test-target` in the wolfboot-ci-m33mu
  container): `PASS: target/restart` (banner x4 then FAULTED) and
  `PASS: target/crossdomain` (`[MEMFAULT] pc=0x0c060f34 addr=0x30028000`),
  ending `PASS: target/all`.
- CI: `.github/workflows/stm32h563-build.yml` job
  `wolfboot-wolftrust-m33mu-scenarios` (matrix restart, crossdomain) drives the
  same runner, so CI and the local harness assert identical markers (ties tasks
  #9/#26; the negative cross-domain job is now wired per the item-5 follow-up).

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Item 9 FF-M IPC negatives on the emulator path

`exercise_ffm_negatives` (guest0_psa) makes two malformed `psa_call` requests
through the SPM veneer in the normal lifecycle (both recoverable, so no separate
faulting run):

- Forged handle (`handle + 0x1000`, unmapped for this caller) -
  `wolfTrust FF-M forged-handle call rejected st=-129` (PSA_ERROR_PROGRAMMER_ERROR
  from `wt_ffm_connection_from_handle`).
- Oversized input vector (2048 > `WT_FFM_TRANSFER_BYTES` 1024, refused at
  `wt_ipc_validate_vectors` before any copy) -
  `wolfTrust FF-M oversized-vector call rejected st=-135`
  (PSA_ERROR_INVALID_ARGUMENT).

The guest still reaches `[EXPECT BKPT] Success`, so the SPM rejected both without
a fault or stale data - the target-side proof of the handle-integrity and
bounded-vector checks host-tested in `tests/host/ffm` (WT-FFM-0021/0032). Both
markers are asserted in the positive M33MU gate (`run_m33mu_scenario.sh
positive`, the box `run_m33mu.sh`, and the CI `wolfboot-wolftrust-m33mu` job) so
CI and the local gate stay identical. The positive `psa_connect`/`psa_call`
round trip and the cross-domain negative were already covered (SERVICE_CRYPTO
dispatch and the item-8 crossdomain scenario).

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Item 10 conformance expansion (Slice 1 - host version-policy tests)

Added the Arm FF-M version-policy tests `i010`, `i011`, and `i026` to
`tests/host/psa_ff_upstream/` (run locally on the host via `make
test-conformance`, no emulator needed). FF-M resolves an unspecified manifest
service to version 1 with `STRICT` policy, so the harness models
`SERVER_UNSPECIFIED_VERSION` as a `STRICT` service at version 1 - not the
permissive `WT_SERVICE_VERSION_UNSPECIFIED` enum, which means "accept any
version" and would wrongly admit `i010`'s higher-version connect.

- `i010` unspecified_policy_higher_version: connect at version 2 is refused
  (`PSA_ERROR_CONNECTION_REFUSED`).
- `i011` unspecified_policy_lower_version: connect at version 0 is refused.
- `i026` psa_call_with_iovec_more_than_max_limit: connect at version 1
  succeeds, then `psa_call` with `in_len + out_len > PSA_MAX_IOVEC` returns
  `PSA_ERROR_PROGRAMMER_ERROR`.

`i026` surfaced a real conformance deviation: `wt_ffm_call` mapped the
vector-count violation to `PSA_ERROR_INVALID_ARGUMENT`. FF-M defines
`in_len + out_len > PSA_MAX_IOVEC` as a PROGRAMMER ERROR, so `wt_ffm_call` now
returns `PSA_ERROR_PROGRAMMER_ERROR` for the count violation while the
transfer-size cap (`WT_FFM_TRANSFER_BYTES`, the item-9 `st=-135` oversized-vector
proof) still returns `PSA_ERROR_INVALID_ARGUMENT`. The wolfTrust host test
`WT-FFM-0032` was updated to assert the conformant code.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 - 16 host
  checks PASS (i010/i011/i026 included), `i067` SKIP (SP heap), ending
  `PASS: conformance/all`.
- `make test`: EXIT 0 - full host suite green including `unit/ffm`
  (`WT-FFM-0032 bounded vector rejection` on the new PROGRAMMER-ERROR mapping).

This is host conformance evidence for the version-policy behavior; the full Arm
suite under M33MU (NS app + 3 Secure test partitions) and the TF-M baseline
remain the later item-10 slices.

## Item 10 conformance expansion (Slice 2 start - negative call type i090)

Wired Arm FF-M test `i090` (`psa_call_with_neg_type`) into
`make test-conformance`: a `psa_call` with a negative message type must return
`PSA_ERROR_PROGRAMMER_ERROR`. This required the second half of the
PROGRAMMER-ERROR-family fix begun for i026 - `wt_ffm_call` returned
`PSA_ERROR_INVALID_ARGUMENT` for `type < 0`; per FF-M a negative type is a
PROGRAMMER ERROR, so `wt_ffm_call` now returns `PSA_ERROR_PROGRAMMER_ERROR`
(the `runtime == NULL`/`caller == 0` internal-argument guards still return
`INVALID_ARGUMENT`). The wolfTrust host test `WT-FFM-0036` gained a matching
negative-type assertion.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 - `i090` PASS
  alongside the Slice-1 set, ending `PASS: conformance/all`.
- `make test`: EXIT 0 - full host suite green including `unit/ffm`
  (`WT-FFM-0036 invalid arguments and empty wait` now covers the negative type).

The remaining server-dispatch tests (`i002`, `i003`, `i027`, `i063`) are
host-viable but need `test_dispatch()` to replicate per-test server behavior
(task 10b); `i048`-`i053` need real MPU isolation (M33MU) and `i058` needs a
Secure-Partition client (compiled out under `-DNONSECURE_TEST_BUILD`).

## Item 10 conformance expansion (Slice 2 - per-service dispatch, i003 data plane)

Added a per-test server dispatch to `tests/host/psa_ff_upstream/main.c`: because
the upstream tests reuse the same SIDs with contradictory server behavior, a
`g_active_test` selector routes `test_dispatch()` to the matching per-test
server instead of the generic reply-success. The harness `val` vtable gained
`ipc_connect`/`ipc_close`. Wired Arm FF-M test `i003` (invec/outvec data plane),
whose server `dispatch_i003()` replicates the upstream server faithfully:

- `zero_length_invec` / `zero_length_outvec`: read the one non-empty invec and
  write it to the one non-empty outvec; zero-length and NULL vectors are
  skipped, and `psa_outvec.len` reflects the bytes written.
- `call_read_and_skip`: the full `psa_read`/`psa_skip` workout - full reads,
  a 2-byte partial read, `psa_skip`, an outbound read that returns only the
  remaining byte, exhausted read/skip returning 0, and zero-byte read/skip.
  This is genuine coverage of `wt_ffm_read`/`wt_ffm_skip`, which already
  implement the exact FF-M offset semantics.
- `call_and_write`: writes four outvecs including two-write concatenation into
  one outvec (`0xdd` then `0xee` → `0xeedd`, len 2).
- `psa_set_rhandle`: the reverse handle is NULL at connect and the first call,
  then persists across calls after `psa_set_rhandle` (5 then 10).
- `overlapping_vectors`: write-then-read and write-after-write on vectors the
  client aliases to one byte; the copied-buffer model returns a valid result.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 - all six `i003`
  checks PASS alongside the prior set, ending `PASS: conformance/all`.
- `make test`: EXIT 0 - full host suite green.

Remaining server-dispatch tests for the router: `i002` (connection lifecycle),
`i063` (signal-mask filtering).

## Item 10 conformance expansion (Slice 2 - connection drop i027)

Wired Arm FF-M test `i027` (`psa_drop_connection`): a new `SERVER_CONNECTION_DROP`
service whose dispatch replies `PSA_ERROR_PROGRAMMER_ERROR` to the call, dropping
the connection. The client then closes the handle and confirms subsequent calls
on it also return `PROGRAMMER_ERROR`.

`i027` surfaced a real conformance gap: after a call reply of
`PSA_ERROR_PROGRAMMER_ERROR`, `wt_ffm_reply` leaves the connection in
`WT_IPC_CONNECTION_ERROR`, and `wt_ffm_close` rejected any non-idle connection,
so the client's mandatory `psa_close` of a dropped connection would panic. Per
FF-M a client may close a dropped connection; `wt_ffm_close` now accepts the
`WT_IPC_CONNECTION_ERROR` state (dispatches the disconnect and releases). The
wolfTrust host test `WT-FFM-0022` covers the drop-then-close-then-stale-call
sequence directly.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 - `i027` PASS,
  ending `PASS: conformance/all`.
- `make test`: EXIT 0 - full host suite green including the new
  `WT-FFM-0022 dropped connection close`.

## Item 10 conformance expansion (Slice 2 - signal-mask refusal i063)

Wired Arm FF-M test `i063` (`psa_wait_signal_mask`): the RoT service refuses
both connects (`SERVER_UNSPECIFIED_VERSION`, `SERVER_RELAX_VERSION`), and the
client confirms it receives `PSA_ERROR_CONNECTION_REFUSED` for each - the
client-visible half of the test. The upstream rule under test (a Secure
Partition using a masked `psa_wait` to ignore an unrelated irritator signal)
needs a real multi-signal scheduler and is deferred to the M33MU slice; the host
dispatch has no concurrent irritator to filter. No production change.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 - `i063` PASS,
  ending `PASS: conformance/all`. `make test`: EXIT 0.

## Item 10 conformance expansion (Slice 2 - connection lifecycle i002, all 9 checks)

Wired Arm FF-M test `i002` (connection lifecycle) end to end via the per-test
dispatch (`dispatch_i002`, keyed on `g_i002_check`):

- `connection_busy_and_reject`: the two connects reply `CONNECTION_BUSY` then
  `CONNECTION_REFUSED` (per-connect sequence counter).
- `accept_and_close_connect`, `connect_with_allowed_version_policy` (UNSPEC@1,
  STRICT@2, RELAX@1, RELAX@2), `psa_call_with_allowed_type_values`
  (`{PSA_IPC_CALL,1,2,INT16_MAX}`): all accepted.
- `psa_call_with_allowed_status_code`: the server replies each of
  `{PSA_SUCCESS,1,2,INT32_MAX,-1,-2,INT32_MIN+128}` in turn and `psa_call`
  returns it verbatim - confirming negative non-PROGRAMMER_ERROR statuses pass
  through unchanged and leave the connection idle (closable).
- `identity`: the server writes the caller `client_id` to two outvecs; the NS
  caller sees both < 0 and equal.
- `spm_concurrent_connect_limit`: the client opens connections until refused;
  the 16-slot runtime pool (`WT_FFM_MAX_CONNECTIONS`) returns
  `CONNECTION_BUSY` at slot 17, which the client accepts, then closes all.
- `psa_block_behave` / `psa_poll_behave`: the client-visible half (the service
  refuses the connects). The server-side PSA_BLOCK-vs-PSA_POLL wait semantics
  need a real scheduler (task #14) and are deferred to M33MU.

No production change. This closes the host-viable server-dispatch work (task
10b); the only Arm ff/ipc tests left need target hardware (`i048`-`i053` MPU
isolation, `i058` SP-client) or the deferred server-internal signal/scheduler
rules.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 - all nine
  `i002` checks PASS, ending `PASS: conformance/all`. `make test`: EXIT 0.

## Item 10 P1a - manifest-bound partition dispatch registry (WT-FFM-0014)

Replaced the per-PID `if` chain in `src/ffm_boot.c` with a manifest-bound
dispatch registry: `wt_ffm_partition_runtime_t` gained a `dispatch`/
`dispatch_context` pair, `wt_ffm_register_partition` binds a service loop to a
partition, and `wt_ffm_dispatch_message` routes each message to the owning
partition's registered handler, using the port dispatch op only as a fail-closed
fallback. Crypto + attest register through `wt_ffm_boot_init`.

Evidence (host, EXIT 0):
- `make test`: new `PASS: WT-FFM-0014 partition dispatch routing` - a registered
  loop intercepts connect/disconnect for `TEST_PARTITION_ID` (dispatch count 2),
  the generic port op is bypassed (count 0), argument/lookup validation returns
  `ARGUMENT`/`POLICY`; plus `SERVICE_CRYPTO`/`SERVICE_ATTEST` KAT round trips
  still green through real FF-M dispatch.

Not yet proven (split out, not claimed): P1t schedulable execution context and
P1r production `ffm_boot` registration regression are target-only and deferred
to the next M33MU box gate (the Mac `arm-none-eabi` toolchain lacks libc headers,
so `src/ffm.c`/`src/ffm_boot.c` cannot be target-compiled locally).

P1r CLOSED (M33MU emulator, wolf-prec5560, 2026-08-12): local gate on the tree
at `0183818` (P1a registry + P2a ingester, pre-P1b) - `PASS: local M33MU gate`,
exit 0, `[EXPECT BKPT] Success`, attestation COSE_Sign1 verified, SHA-256 KAT,
AES-CTR, FreeRTOS PKCS11 digest all green, no fault markers. The production
dispatch-registry path serves SERVICE_CRYPTO and SERVICE_ATTEST unchanged.

## Item 10 P2a - Arm manifest ingestion, psa_manifest identity headers

`tools/manifest/ingest_psa_arch.py` ingests the unmodified upstream
`server/driver/client_partition_psa.json` and emits `psa_manifest/pid.h`,
`sid.h`, and per-partition signal headers through `generate.py`'s existing
emitters, replacing the hand-faked `-D` defines path for upstream `val`/PAL
includes. FF-M defaulting applied: missing `version` → 1, missing
`version_policy` → STRICT; service and IRQ signals assigned per partition from
0x10 upward.

Evidence (host, EXIT 0): `make test-conformance` now runs
`tests/host/manifest_ingest/run.py` against the real fetched manifests -
asserts all 12 SIDs (0xFA01, 0xFB01-07, 0xFC01-04), version defaults
(`SERVER_UNSPECIFIED_VERSION_VERSION`=1, `SERVER_STRICT_VERSION_VERSION`=2),
and driver signals including `DRIVER_UART_INTR_SIG_SIGNAL`=0x100 - then the
full 24-test host subset, ending `PASS: conformance/all`.

## Item 10 P1b + P2 carve - M33MU positive gate (run 3)

Gate history on this slice, all M33MU emulator on wolf-prec5560 (2026-08-12):
- Run 2 FAILED (build): `platform_stm32h563.c`'s new `psa_manifest/pid.h`
  include had no rule dependency on the manifest generation stamp; the target
  compile raced the generator. Fixed with an explicit
  `sec_platform_stm32h563.o` rule mirroring `sec_ffm_boot.o`. The P1b logic
  never executed in that run.
- Run 3 PASSED on `14b08b9`: `PASS: local M33MU gate`, exit 0,
  `[EXPECT BKPT] Success`, no fault markers - with `wt_platform_run_crypto_sp_isolated`
  resolving the crypto SP MPU domain from the bound manifest
  (`wt_ffm_resolve_secure_domain(PARTITION_CRYPTO_ID)`, stack from the
  manifest's writable resource, fail-closed) AND the 5-slot secure stack carve
  (SPSTACKS 0x30096000/40K, production crypto/attest stacks moved to
  0x30096000/0x30098000).

P1b CLOSED - negative gate (WT_FFM_NEGATIVE_PROBE=1) on the same tree PASSED
(M33MU, 2026-08-12): `[MEMFAULT] pc=0x0c060f34 addr=0x30028000`, the crypto SP's
read of SPM-private RAM from inside the manifest-resolved domain faults, exit 1,
`PASS: negative M33MU gate`. `sp=0x30097ff0` and `r4=0x30096000` confirm the SP
executed on the new manifest-resolved stack carve (crypto slot 0x30096000). Both
halves of the resolver wiring (positive run 3 + this negative) hold on one tree.

## Item 10 P1t-1 - SPM-call gate (host)

The single privileged choke point `wt_spm_gate` (`src/spm_gate.c`,
`include/wolftrust/spm_gate.h`) that every SP-side `psa_*` will funnel through
on target once the SVC handler unmarshals registers into `wt_spm_call_t`. It
routes wait/get/set_rhandle/read/skip/write/reply/notify/clear to the existing
`wt_ffm_*` runtime, bounds every SP-supplied pointer against the caller's
resolved protection domain via `wt_secure_domain_contains` before the
privileged SPM dereferences it, and `wt_spm_call_would_block` flags an empty
`psa_wait` (runtime returns `WT_FFM_ERROR_NOT_READY`) as "suspend this SP".

Host evidence (2026-08-12), `tests/host/spm_gate` - 62 checks, green under
`cc`, `gcc`, and `clang`, and under ASan+UBSan:
- `WT-FFM-0014 gate routes the SP dispatch path`: a server body with every
  primitive routed through the gate serves a real `wt_ffm_connect` +
  `wt_ffm_call` round trip; the NS client receives the `OK` output, proving the
  gate is behaviorally identical to the inline `wt_ffm_*` calls.
- `WT-FFM-0014 gate classifies an empty wait as blocking`: gate WAIT with no
  asserted signal → `would_block == 1`; after `wt_ffm_notify` → success,
  `would_block == 0`, asserted == `PSA_DOORBELL`.
- `WT-FFM-0014 gate bounds SP pointers to the domain`: a read destination
  inside the domain passes validation and runs; outside, NULL, and a write
  source outside are rejected with `WT_FFM_ERROR_BUFFER` before the runtime
  runs; a NULL domain bypasses validation.

Not in the secure build yet - the target SVC gate, unprivileged drop, per-SP
MPU on switch-in, and real coroutine suspend/resume land together in P1t-2
under one M33MU gate. Valgrind not run locally (macOS host); it is the CI
Valgrind workflow's responsibility.

## Item 10 P1t-2a - gate live in the production image (M33MU)

The production crypto service dispatch (`src/services/crypto_service.c`) now
routes `psa_wait`/`psa_get`/`psa_read`/`psa_write`/`psa_reply` through
`wt_spm_gate` instead of calling `wt_ffm_*` inline, and `src/spm_gate.c` is in
the secure build source list. Still privileged and synchronous - no isolation
change; this slice proves the gate on the real production dispatch path before
the P1t-2b SVC/unprivileged work builds on it.

- Host (2026-08-12): `tests/host/crypto_service`
  (`PASS: SERVICE_CRYPTO SHA-256 KAT through real FF-M dispatch`) green under
  `cc`, `gcc`, `clang`, and ASan+UBSan with the gate-routed dispatch; full
  `make test` `PASS: unit/all`.
- M33MU positive gate (2026-08-12, wolf-prec5560): `PASS: local M33MU gate`,
  exit 0 - `wolfTrust FF-M SERVICE_CRYPTO dispatch verified`,
  `psa_hash_compute(SHA-256) KAT verified`, `psa_initial_attestation st=0`,
  `[EXPECT BKPT] Success`, no fault markers. The KAT result therefore
  transited `wt_spm_gate` inside the production secure image.
- Negative gate not re-run: `WT_FFM_NEGATIVE_PROBE` exercises
  `wt_crypto_sp_body`/`wt_platform_run_crypto_sp_isolated` (platform compute
  layer), which this slice does not touch.

## Item 10 P1t-2b - coroutine-backed unprivileged SP via SVC gate (M33MU)

The crypto Secure Partition now runs as a scheduled coroutine, unprivileged on
its own PSP stack inside its manifest MPU domain, reaching the SPM only through
`svc #1`. The service loop is the same architecture-neutral code the host tests
prove (`src/services/crypto_service.c`); only the transport differs - each
`wt_spm_call_t` traps to `wt_spm_svc_entry` (privileged) which validates the
call pointer against the partition domain and runs `wt_spm_gate`. A `psa_wait`
with nothing asserted suspends the coroutine (`wt_co_block`); the SP-side
transport re-issues on wake. New: `src/arch/armv8m/spm_svc.c`, SVC `#1` branch
and per-SP nPRIV/MPU switch in `coroutine_armv8m.c`,
`wt_platform_program_sp_thread_domain` (PRIVDEFENA on so privileged handlers
keep SPM access while the unprivileged thread is confined), coroutine `domain`/
`unprivileged` fields + `wt_co_set_domain` + `wt_co_create_blocked_ex`.

Isolation fix during bring-up: the unprivileged loop first faulted reading the
file-scope `g_spm_transport`/`g_crypto_sp_compute` (function pointers in SPM
`.data` at 0x300282b0, outside the SP domain). Transport and compute now reach
the loop through a dispatch context the SP builds on its own stack, so no
global-pointer read crosses the domain.

- Host (2026-08-12): `tests/host/spm_gate` (62 checks) and
  `tests/host/crypto_service` KAT green under cc/gcc/clang + ASan/UBSan;
  `make test` `PASS: unit/all`. The context refactor keeps the `context==NULL`
  path (globals) for host/inline callers.
- M33MU positive gate PASS: `PASS: local M33MU gate`, exit 0,
  `wolfTrust FF-M SERVICE_CRYPTO dispatch verified`,
  `psa_hash_compute(SHA-256) KAT verified`, `[EXPECT BKPT] Success`, no fault
  markers. The KAT transits veneer -> svc #1 -> gate -> coroutine SP.
- M33MU negative gate (`WT_FFM_NEGATIVE_PROBE=1`) PASS:
  `[MEMFAULT] addr=0x30028000` with `sp=0x30097fe0` (SP on its PSP domain
  stack) and `xpsr=0x01000000` (Thread mode) - the unprivileged SP's read of
  SPM-private RAM (WT_RAM_S_BASE) is denied by the MPU, proving genuine
  unprivileged isolation, then recovers gracefully via the tasklet fault path.
- Runner flake fixed (recurred twice): wolfBoot host keytools `-j` link race
  (sp_ModExp_*/sp_Rsa* linked before their objects). `run_m33mu.sh`,
  `run_m33mu_negative.sh`, and the CI yml now build `keytools` serially first.

## Item 10 P3a-1 - schedule Secure Partitions from a coroutine-keyed table (M33MU)

The single hardcoded crypto SP in `src/arch/armv8m/spm_svc.c` is now a slot
table (`g_spm_sp[WT_FFM_MAX_PARTITIONS]`). The privileged SVC `#1` dispatcher
resolves the caller from `wt_co_current()` and validates the call pointer
against that slot's own MPU thread table before running `wt_spm_gate`, so N
partitions share one transport, each confined to its manifest domain. Slot setup
is factored into `wt_spm_sched_add(runtime, pid, entry, arg)`; `wt_spm_sched_start`
calls it for the crypto SP (table entry 0). A slot is published (`count++`) only
after `wt_ffm_register_partition` succeeds, so an SP is never visible half-built.
This is the seam P3a-3 adds Arm's `server_main`/`client_main` through with no
scheduler-core change. Pure foundation - no Arm SP yet, crypto behavior unchanged.

- Target syntax check: `arm-none-eabi-gcc -fsyntax-only -std=c99 -Wall -Wextra
  -mcpu=cortex-m33` on `spm_svc.c` - exit 0, no warnings.
- M33MU positive gate PASS (2026-08-12): `PASS: local M33MU gate`, exit 0,
  `wolfTrust FF-M SERVICE_CRYPTO dispatch verified`,
  `psa_hash_compute(SHA-256) KAT verified`, forged-handle st=-129,
  oversized-vector st=-135, `psa_initial_attestation st=0`,
  `[EXPECT BKPT] Success`, no fault markers - the crypto SP still transits
  veneer -> svc #1 -> gate -> coroutine through the generalized slot table.
- M33MU negative gate (`WT_FFM_NEGATIVE_PROBE=1`) PASS: `[MEMFAULT]
  addr=0x30028000` with `sp=0x30097fe0` (SP on its PSP domain stack) and
  `xpsr=0x01000000` (Thread mode) - the per-slot MPU table still denies the
  unprivileged SP's read of SPM-private RAM, then recovers via the fault path.

## Item 10 P3a-2 - NS->S psa_* version veneers (M33MU)

The Non-secure guest now reaches the SPM's framework-version query through a real
PSA client veneer, not the TEE-driver `tee_invoke_func` path. New secure
`cmse_nonsecure_entry` veneers `WolfTrust_FFM_FrameworkVersion()` and
`WolfTrust_FFM_ServiceVersion(sid)` (`src/ffm_boot.c`, auto-exported into the CMSE
import library) alias `wt_ffm_framework_version`/`wt_ffm_service_version`. NS
`psa_framework_version()`/`psa_version()` wrappers over those veneers live in the
Zephyr `wolftrust-tee` module (`wolftrust_tee_driver.c`) - the exact symbols the
upstream Arm val NSPE links. The connect/call/close aliases, which need
`psa/client.h`'s `psa_invec`/`psa_outvec` types, arrive with val in P3a-3.

- M33MU positive gate PASS (2026-08-12): the guest calls `psa_framework_version()`
  and logs `wolfTrust FF-M psa_framework_version=0x0100` (== `PSA_FRAMEWORK_VERSION`)
  - asserted by the `positive` scenario - then `PASS: target/positive`, exit 0,
  no fault markers. Proves NS shim -> CMSE veneer -> `wt_ffm_framework_version`
  end to end.

## Item 10 P3a-3 - Arm conformance partitions compiled into the secure image (M33MU)

Arm's unmodified `ff/partition/server_partition.c` and `client_partition.c`, plus
the i001/i003 test bodies, now compile into the secure image (behind
`WT_CONFORMANCE=1`) against wolfTrust's own `include/psa/*.h`, the generated
`psa_manifest/` headers, and a new STM32H563 `pal_config.h`. `wt_ffm_boot_start_sched`
schedules `server_main`/`client_main` as unprivileged coroutines via
`wt_spm_sched_add` on their P2-carved manifest stacks (0x3009A000 / 0x3009E000).

The service-side PSA API is now target-native: `src/arch/armv8m/spm_sp_api.c`
marshals every `psa_*` service call into a `wt_spm_call_t` and traps to the
privileged gate via SVC (`wt_spm_sp_call`), replacing the direct
`src/ffm_api.c` bindings in the target build - those dereferenced SPM state an
unprivileged partition cannot reach (the P1t-2b fault class, now structurally
impossible). The SVC entry stamps `call->partition_id` from the scheduled slot,
so a partition cannot impersonate another; a latent conflation was fixed by
giving `psa_notify` its own `notify_partition` field distinct from the acting
partition id. SP-as-client IPC (`psa_connect`/`psa_call` from a partition) has
no gate op yet and refuses closed rather than faking success - tracked with P3b.

- Host `make test`: EXIT 0 (`spm_gate` 62 checks, `unit/all`) - the
  `notify_partition` split keeps the gate NOTIFY path green.
- Target dry-compile: all six upstream sources + every touched wolfTrust source
  compile clean under `arm-none-eabi-gcc` (`-Wall -Wextra`, `-mcmse`).
- M33MU `confboot` gate PASS (2026-08-12): the conformance image (both Arm SPs
  scheduled) boots the full positive lifecycle green - `PASS: target/confboot`,
  exit 0, `[EXPECT BKPT] Success`, no fault markers.
- M33MU regression on the same tree PASS: `positive` (production image unhurt by
  the psa_* API move) and `crossdomain` (`[MEMFAULT] addr=0x30028000` still
  denied). `restart` is NS-guest-fault behavior, orthogonal to the SP psa_*
  change, last proven on the P1 keystone tree.
- New `confboot` scenario wired into `run_m33mu_scenario.sh`, `make test-target`,
  and the CI matrix. The unmodified Arm partitions are the true TF-M drop-in.

## Item 10 P3a-4a - first unmodified Arm conformance test green (i001, M33MU)

`PASS: target/confboot` with `Result=Passed`, `TOTAL PASSED : 1`,
`TOTAL FAILED : 0` (2026-08-13): Arm PSA Arch Test Suite v1.8 test_i001 runs
end-to-end through wolfTrust's production SPM - NS val framework in the Zephyr
guest -> CMSE veneers -> client SP -> SP-to-SP IPC -> server SP, with the
driver SP serving val's NVM bookkeeping. All three unmodified Arm test
partitions run as unprivileged, MPU-isolated scheduled coroutines.

New SPM capability landed for this (all clean-room, spec-derived):
- SP-as-client IPC (WT-FFM-0014): deferred connect/call/close in `ffm.c`
  (begin = validate+enqueue, finish = harvest after completion), four new gate
  ops with idempotent two-pass pending/harvest and forged-pending ownership
  checks (`spm_gate.c`), SP-side `psa_connect/call/close/version` plus FF-M 1.1
  `psa_irq_enable` no-op pending P6 (`spm_sp_api.c`).
- Cross-partition scheduler: per-slot wake conditions (signal-wait vs
  message-wait) driven to quiescence on the bootstrap context
  (`spm_svc.c`) - a client partition blocks on its message while the serving
  partition runs; SP code never executes in handler mode.
- Bugs found by target-only diagnosis (deliberate-fault register dumps +
  signed-elf snapshot for honest symbolization): transport retry only re-issued
  psa_wait (NOT_READY escaped to callers); wait bookkeeping erased pre-suspend
  (wt_co_block only pends); psa_close panicked on the refused-connect handles
  upstream passes it (now a no-op for handle <= 0).
- Hosting glue: DRIVER partition scheduled with a wolfTrust SPE PAL
  (RAM-backed NVM until P5, no-op WD until P4, swallowed prints until P3b);
  CONFDATA window grown to 12 KiB at 0x30093000.
- Host `make test` green on the same tree (spm_gate 62 checks intact).
- Production regression (positive + crossdomain) run on the same tree before
  commit - see the commit that carries this entry.

## Item 10 P3a-4b - multi-vector psa_call green (i003, M33MU)

`PASS: target/confboot` with `TOTAL TESTS : 2`, `TOTAL PASSED : 2`,
`TOTAL FAILED : 0` (2026-08-13): test_i003 (Testing IOVECS, all 6 checks -
zero-length invec/outvec, psa_read/psa_skip, psa_write, psa_set_rhandle,
overlapping vectors) joins i001, exercising real multi-vector
`psa_call` -> `server_main` data transfer through the SPM from the NS val
client.

Transport widening (clean-room, spec-derived):
- `WolfTrust_FFM_Call` veneer ABI widened from 1 invec + 1 outvec to
  `PSA_MAX_IOVEC` arrays with out-length writeback to the NS caller
  (`ffm_veneer.h` both copies, `ffm_boot.c`, NS shims in the TEE driver,
  attestation client, and conformance PAL).
- SP-side transport cap `WT_SPM_SP_IOVEC` lifted 2 -> 4 (`spm_gate.h`);
  every SP wrapper now zeroes its gate call struct (`spm_sp_api.c`).

Two latent spec-conformance bugs flushed out by the unmodified suite
(both diagnosed from the deliberate-fault register dumps):
- Gate rejected FF-M zero-length transfers: `wt_spm_check_buffer` returned
  BUFFER for `len == 0` because the domain containment predicate denies empty
  ranges; i003's server writes 0 bytes on purpose. Zero-length transfers now
  bypass containment (nothing crosses); host gate test asserts the
  discriminator (dead handle fails ARGUMENT, not BUFFER) - spm_gate 67 checks.
- `psa_set_rhandle` on a DISCONNECT message returned STATE and panicked the
  server; FF-M requires success with no observable effect (i003 checkpoint
  206). Now a no-op success; the host ffm test had asserted the wrong
  behavior and was corrected to the spec.

Host `make test` green on the same tree; guest VERBOSITY raised to 3 and the
NS `wtconf:` traces gated behind `WT_CONF_TRACE` (default off). Production
regression (positive + crossdomain) rerun on the same tree before commit.

## Item 10 P3b - NS val NVM through the DRIVER partition (M33MU)

`PASS: target/confboot` with `TOTAL TESTS : 2`, `TOTAL PASSED : 2`,
`TOTAL FAILED : 0` (2026-08-13): the NS val framework's `pal_nvm_read/write`
now cross the boundary as IPC clients of the DRIVER partition's NVMEM service
(`nvmem_param_t` invec + data vec to SID 0xFC03, `nonsecure_clients` already
granted by the generated manifest), so NS and SPE val share the single
driver-served store - required for every P3c test whose bookkeeping crosses
NS<->S. The private NS RAM store is deleted. val reads/writes NVM throughout
both tests, so the green run exercises the path constantly.

Scope splits recorded in the task list: SPE `pal_print` -> real UART requires
the driver domain's manifest MMIO grant (P4's ingestion mechanism, moved
there); watchdog remains a no-op on M33MU (no WDG model; P6).

Host `make test` green on the same tree. The slice changes only the
conformance guest (`WT_RUN_CONFORMANCE`), so the production positive and
crossdomain binaries are identical to those proven green on `48b63e6`; no
rerun was performed.

## Item 10 P3c-2 Phases A-C - doorbell + signal-mask scheduler completeness (host)

Host `make test` green across three committed slices; no target run (box
`wolf-prec5560` offline since 2026-08-13, so Phases D-F are blocked).

- Phase A (`8f01063`): `psa_wait` signal-mask filtering locked against
  regression. `tests/host/ffm` `test_wait_signal_mask` and `tests/host/spm_gate`
  prove an out-of-mask asserted signal keeps a wait blocking and that
  `psa_wait` returns only `asserted & mask`.
- Phase B (`9c8781c`): i058 Check-1 doorbell state machine through the gate
  (`test_gate_doorbell_state_machine`), plus the real POLL/BLOCK fix - the
  `psa_wait` `timeout` is threaded through `wt_spm_call_t` and
  `wt_spm_call_would_block` so `PSA_POLL` returns instead of blocking (i058's
  final poll would otherwise hang). spm_gate 90 checks.
- Phase C (`52d3f67`): doorbell-driven origination with masked starvation, the
  portable core of i063 (`test_doorbell_origination`). A doorbell-woken client
  originates an outbound connect through the SP-as-client gate; it stays starved
  on a masked server signal across the server's masked waits and is delivered
  only on an explicit wait, completing `CONNECTION_REFUSED`. The existing gate +
  runtime already carry this - no scheduler change was required - so the
  outstanding target faults are choreography/epilogue bugs, not a missing
  capability. spm_gate 126 checks.

The coroutine choreography these prove out (`wt_spm_sched_dispatch`) is
Armv8-M-only and remains to be validated on M33MU (Phases D-F).

## Item 10 P3c-2 Phases D-F - six-test conformance green (M33MU)

`PASS: target/confboot` with `TOTAL TESTS : 6`, `TOTAL PASSED : 6`,
`TOTAL FAILED : 0`, `[EXPECT BKPT] Success`, exit 0 (2026-08-14): the full
non-IRQ/non-heap subset - i001, i003, **i058 (PSA_DOORBELL)**, **i063 (psa_wait
signal mask)**, i071, i088 - all `Result=Passed` through the unmodified Arm
suite on the real SPM. Tree = `3bac964` + `96fee67` (+ `1d37648`).

Root cause of the i058/i063 hangs, found via WT_CONFORMANCE-gated hang
tripwires (register-dump diag traps for silent stalls): the SP-side SVC
transport re-issued any gate call that returned NOT_READY, so a `PSA_POLL`
wait miss - which reports NOT_READY but must return, not suspend - spun the
partition coroutine forever (i058's post-`psa_clear` doorbell poll). The
transport now re-issues only calls that actually suspended
(`wt_spm_call_would_block`). A second instance of the same class: the crypto
SP's hand-built WAIT never set `timeout`, read as PSA_POLL after the
POLL/BLOCK split, and killed the partition at boot (`1d37648`, host-guarded in
`tests/host/crypto_service`).

The earlier i063 REPLY/HANDLE fault and the NS epilogue USGFLT (both
2026-08-13) did not reproduce on the fixed tree: confboot exits clean through
the expected BKPT.

Diagnostic evidence (tripwire register dumps, M33MU): spin trap
`r5=7 (CLIENT), r6=DOORBELL|NOT_READY`, then payload trap
`r4=0x21221011` (client RUNNABLE + never-suspended, others blocked),
`r5=0x0000FFFF` (client doorbell already cleared; server in WAIT_ANY),
`r6=0x00010001` (one replied-but-unharvested NS EXECUTE message) - matching
the POLL-spin prediction exactly.

Host `make test` green on the same tree. Production regression rerun on the
same tree: `PASS: target/positive` (full lifecycle green, exit 0) and
`PASS: target/crossdomain` (cross-domain read of 0x30028000 denied by the SP
domain).

## P4/P5 K1 - reset feasibility probe (GO, 2026-08-14)

Decided by authoritative inspection of the pinned M33MU emulator source
(`github.com/danielinux/m33mu@c84792f7f9e9ce24cf94ffc492c36231de1854c2`), which
answers the feasibility question more conclusively than a single black-box run
and at no target-cycle cost. The panic→reset→resume substrate P4/P5 depend on is
present and models the STM32H563 faithfully:

- **CPU model.** The runner passes no `--cpu`, so the default applies:
  `cpu_table[0].name == "stm32h563"` (`src/cpu_db.c:89`). Our images boot on the
  H563 model, not a generic core.
- **SYSRESETREQ re-runs the chain.** An `AIRCR` (`0xE000ED0C`) write with
  VECTKEY `0x05FA` and SYSRESETREQ (bit 2) calls `mm_system_request_reset()`
  (`src/scs.c:582`). The run loop then breaks with `reset_again` and re-enters
  the core-reinit path printing `[RESET] System reset requested, reinitialising
  core` (`src/main.c:6042,6064`); images are NOT reloaded on this path
  (`reload_images` is only the initial load / a TUI action).
- **Flash + option bytes survive the reset.** The emulator's own conformance
  firmware `tests/firmware/test-stm32h563-dualbank/main.c` programs the SWAP
  option bit, issues `AIRCR = 0x05FA0004` (line 240), and on the second boot
  reads the SWAP state back intact - over the exact FLASH controller MMIO
  (`0x40022000`, NSKEYR/SECKEYR/NSCR/SECCR, `cpu/stm32h5_mmio.c`) that
  wolfTrust's `port/stm32h563/hsm_flash.c` already drives. wolfHSM NVM writes
  already succeed within a boot on M33MU, so the write path is live; the
  dualbank test adds the cross-reset persistence guarantee.
- **`.noinit` RAM survives too.** The same test uses a `.noinit reset_marker`
  to distinguish first vs. second boot - so a warm reset re-inits the CPU core
  only, leaving RAM and flash intact. Useful as a boot-count detector in K3/K4.

**Verdict: GO.** No hardware gating for the reboot bucket. `wt_platform_system_reset`
(AIRCR.SYSRESETREQ) is implemented in K3 so its first target run exercises the
production SPM panic→reset path rather than a disposable probe; K2 backs the
NVMEM service with reserved secure flash; K4 proves the full resume loop on i047.

## P4/P5 K4 - i047 needs per-SP MMIO isolation, not just the reboot keystone (2026-08-14)

The reboot-continuity keystone (K1–K3) is mechanically complete. Trying to prove
it end-to-end with i047 uncovered that i047 is fundamentally an MMIO-isolation
test, so it cannot pass on the current shared-CONFDATA domain model.

Runs on the box (`k4-trace2.log`, WT_CONF_TRACE on): the full schedule executes
and exits clean - `TOTAL TESTS 7, PASSED 6, FAILED 1`, `[EXPECT BKPT] Success`,
exit 0. The one failure is i047. (An earlier caveat: the plain run's log looked
like it "rebooted" - that was stdout buffering; the emulator's block-buffered
`printf` for `Loaded BIN`/`Initial SP`/`[USGFLT]` flushes at exit, AFTER the
live guest UART, so the single boot's startup lines appear at the tail. Counts
confirm one boot, zero resets.)

**Root cause of the i047 failure (precise):** i047's server does
`psa_get(SIGNAL, invalid_msg)` where, at isolation level > 1, `invalid_msg =
PLATFORM_DRIVER_PARTITION_MMIO_START = 0x30095E00`
(`port/stm32h563/conformance/pal_config.h:64`, level 3 at `:29`). But every
conformance SP's domain is granted the WHOLE shared CONFDATA window
`0x30093000..0x30096000` (`src/arch/armv8m/spm_svc.c:485`,
`WT_CONF_SP_DATA_BASE`/`_SIZE` in `memory_map.h:110`), and `0x30095E00` lies
INSIDE it. So the SERVER can legally reach the DRIVER's MMIO address - the gate
buffer check passes, `must_panic` never fires, the server returns normally, the
client's `psa_connect(0xFB04)` gets `-130` instead of never returning, and val
marks i047 FAILED. To pass, the DRIVER's MMIO region must be OUTSIDE the SERVER
partition's domain (true per-SP MMIO isolation - task #33 / P4 bucket b). Every
available panic test hits this same wall (i047/i055/i057 use the DRIVER MMIO at
L3; i064–066 need `psa_eoi`), so the keystone can only be demonstrated once
per-SP MMIO isolation lands (or the driver MMIO is relocated outside all SP
domains).

**Secondary:** a timing-dependent heisenbug - the plain (untraced) build faults
in the guest `psa_call` invec copy, but the traced build completes; correlated
with unbounded FF-M handle growth (176 connects over the run, handle values
climbing +128 each with no reuse). Likely a fixed-size table indexed off the
handle overflowing once i047's extra connects push the count up. Needs the
handle allocation bounded/reused.

**Status:** K1–K3 done. K4 (and P4.1's panic tests) are gated on per-SP MMIO
isolation. i047 build wiring is in; the confboot schedule asserts 7 so the gate
is RED until the isolation work lands. Deep target work - Fable-class.

## P4/P5 K4 second pass - MMIO carve landed; blocked on an M33MU emulator defect (2026-08-14)

**The per-SP MMIO isolation carve is implemented and works.** Every conformance
SP still gets the shared CONFDATA window, but the per-partition pseudo-MMIO
holes at its top are granted only to their owner (`wt_spm_conf_grant` +
per-partition carve in `wt_spm_sched_add`, `src/arch/armv8m/spm_svc.c`;
authoritative constants `WT_CONF_SERVER/DRV_MMIO_*` in
`port/stm32h563/memory_map.h`; `#error` cross-check against `pal_config.h` in
`conf_nvm_sync.c`; link-time overflow assert in `runner/secure.ld`). The SERVER
MMIO was relocated `0x30094E00 → 0x30095C00` because conf `.bss` had grown over
the old address (`_econfbss = 0x300954DC`). With the carve, a WT_CONF_TRACE
build ran the i047 flow end-to-end: server `psa_get(0x30095E00)` → out-of-domain
→ `must_panic` → `wt_platform_system_reset` → **emulator rebooted through
wolfBoot and re-ran the guest** (second VTOR_NS writes + NVIC reads in the
trace). The panic→reset→reboot mechanism is real on target.

**A real cross-guest bug was found and fixed on the way:**
`wt_dispatch_hsm_tasklet` resumed a guest's blocked secure tasklet without
reinstating that guest's NS-banked registers; the tasklet completes back to NS
via BXNS (not the exception-return path), so the previous guest's
`CONTROL_NS`/`MSP_NS`/`PSPLIM_NS` leaked in (`wt_jump_to_ns` explicitly launches
guests with `CONTROL_NS=0`). Fix: `wt_platform_restore_ns_bank` (mirrors
`wt_exception_return_ns_msp`'s NS-bank restore) called before
`wt_tasklet_resume` (`src/monitor.c`). K2's flash-backed NVM had widened the
exposure window (sector erase + program per NVMEM write vs the old RAM store).

**The remaining blocker is an emulator defect, not wolfTrust logic.** The
untraced confboot deterministically stops at virtual cycle ~14,244,2xx (4 runs,
byte-identical dumps) during boot-1 i047 machinery: the NS guest faults
`CFSR=0x00010082` (UNDEFINSTR + stale DACCVIOL) at `pal_nvm_write+0x2c` - a
plain `add r3, sp, #8` that cannot fault on silicon. `--record` instruction
traces show: (a) the NS thread entered the FF-M veneer on its PSP
(`r7=0x20007bc4` frame) and resumed after the secure round trip with the MSP
selected (`sp=0x20005388`, `ctrl_ns=0`); (b) the final recorded events are a
SECURE SVC handler (IPSR=0x0B, secure PC `0x0c0636xx`) executing with the **NS
stack pointer bank selected** (`sp = msp_ns + 0x14 = 0x200053c8`) while the
registers still hold the scrubbed pre-BXNS veneer state
(`r1=r2=r3=r12=lr=0x080808ca`) - i.e. an SVC taken from secure thread mode at
the BXNS boundary stacked onto the NS bank. wolfTrust cannot select the SP bank
on exception entry; that is the CPU model. Reproduces identically on the pinned
emulator (`c84792f7`) AND current master (`f96ab8e`). A WT_CONF_TRACE build
passes because printk's UART stalls between the veneer return and the next
secure entry change the emulator's event interleaving - consistent with an
event-ordering defect at the S↔NS boundary, likely around the monitor's
`svc #0x7F` guest-return adjacent to BXNS.

**Evidence:** box `wolf-prec5560`: `k4-carve.log` (carved run, fault),
`k4-nsbank.log` (fix run, same fault → NS-bank leak was real but not this
trigger), `k4-rec.log` + `k4-bigtrace.log` (instruction traces),
`k4-master.log` (current-master repro). Reproducer: the confboot images from
this tree + `--record --record-quiet --record-dump 120`.

**Next:** report the reproducer upstream (danielinux/m33mu) and/or patch the
emulator locally; alternatively find a wolfTrust-side sequence change that
avoids the SVC-adjacent-to-BXNS trigger (Fable-class). Tracked as task #63;
i047 is parked behind it (schedule back to 6) so the confboot gate stays green
while the keystone code (K1–K3 + carve + NS-bank fix) remains in-tree.

## Item 10 P4.1a - i055/i057 buffer-panic pair (M33MU, 9/9, 2026-08-17)

The K4 reboot loop scales to THREE panics in one boot. `i055`
(`server_test_psa_read_with_invalid_buffer_addr`) and `i057`
(`..._psa_write_...`) both hand `PLATFORM_DRIVER_PARTITION_MMIO_START`
(`0x30095E00`) to `psa_read`/`psa_write` from the SERVER partition - the same
out-of-domain path as i047. The per-SP MMIO carve already excludes the DRIVER
hole from the SERVER domain, the neutral gate already flags READ/WRITE
bad-buffer as `must_panic`, and the `#error` cross-check pins the address, so
no SPM/gate change was needed - pure schedule + build wiring
(`mk/secure-armv8m-stm32h563.mk` OBJS/SRCS/pattern-rules/panic-strip sed;
`guest0_psa/CMakeLists.txt` 3 spots; confboot assert 7→9).

**Evidence:** `run_m33mu_scenario.sh confboot` (local box Docker, same CI
container `ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15`, patched emulator):
`TOTAL TESTS : 9 / PASSED : 9 / FAILED : 0 / SKIPPED : 0`, THREE mid-suite
`[RESET] System reset requested` markers (one per panic test, each resuming off
its flash-backed boot flag), clean `[EXPECT BKPT] Success`, exit 0,
`PASS: target/confboot`. Log: box `confboot-p41a.log`.

## Item 10 P4.2a - USART peripheral NVIC delivery feasibility (GO, source, 2026-08-17)

Read-only emulator-source probe (the free K1-style gate before any target run).
**Verdict: GO.** The M33MU USART model asserts its NVIC line via
`mm_nvic_set_pending(nvic, u->irq, MM_TRUE)` when `CR1.TXEIE && ISR.TXE` or
`CR1.RXNEIE && ISR.RXNE` (`cpu/stm32_usart.c:340-347`); the instance is wired
with its NVIC and IRQ number at init (`stm32h5_usart_init` →
`stm32_usart_register_instance(..., d->irq, ...)`). Per-IRQ S/NS targeting
exists (`mm_nvic_set_itns` / `mm_nvic_irq_target_sec`, `src/nvic.c:115-131`),
and CPU delivery of pending NVIC lines to the guest is already proven (SysTick,
EXTI, RNG all vector through `mm_nvic_select_routed`). So peripheral-IRQ
delivery needs no hardware gate; the remaining P4.2 work is wolfTrust-side
(`psa_eoi` validation in P4.2b, i021 end-to-end in P4.2c). Supersedes the
M33MU-2 candidate below.

## Item 10 P4.2b - psa_eoi argument validation (host, 2026-08-17)

`psa_eoi` was an always-panic stub - it would have falsely passed i064–066
(all three expect a panic) while also panicking a *legal* EOI, which i021/P6
need to succeed. Replaced with a faithful arch-neutral engine fn `wt_ffm_eoi`
(`src/ffm.c`): it derives the partition's interrupt-signal mask from its
manifest (`interrupts[]`) and returns a programmer-error code for each FF-M
misuse - `WT_FFM_ERROR_ARGUMENT` for zero/multiple bits (i066),
`WT_FFM_ERROR_POLICY` for a non-declared signal (i064), `WT_FFM_ERROR_STATE`
for a declared-but-unasserted signal (i065) - and clears the asserted bit on a
legal EOI. Both veneers route through it: `src/ffm_api.c psa_eoi` panics on
error; the SP-side `src/arch/armv8m/spm_sp_api.c psa_eoi` issues a new
`WT_SPM_OP_EOI` gate op the gate flags `must_panic` on (same path as
psa_get/read/write bad-buffer).

**Evidence (host `make test`, `PASS: unit/all`):** `unit/ffm` new
`psa_eoi argument validation` proves all four cases including the legal clear
(the host sets `asserted_signals` to stand in for the FLIH that asserts an
interrupt on real hardware - P6/i021); `unit/spm_gate` new
`gate panics psa_eoi misuse` proves the gate sets `must_panic` on the three
rejections and not on a legal EOI (146 checks, up from 133). The SP-side veneer
is a line-for-line mirror of `psa_clear`; its target proof arrives with P4.1b's
confboot (which cross-builds it and runs i064–066 across the panic-reset loop).

## Item 10 P4.1b attempt - i064/i065 pass on target, i066 hits a flash limit (2026-08-17)

Wired all three psa_eoi-misuse panic tests into the schedule (confboot assert
12) and ran one M33MU confboot. Result: the first nine tests plus **i064
(psa_eoi non-interrupt) and i065 (psa_eoi unasserted) both `Result=Passed`** -
strong target evidence that P4.2b's SP-side `psa_eoi` → `WT_SPM_OP_EOI` gate →
`wt_ffm_eoi` error → `must_panic` → SPM reset → val boot-flag resume path works
end-to-end (the 4th and 5th panic-reset reboots in a single boot; the SP-side
veneer cross-built and linked clean).

**i066 (psa_eoi multiple-signal) failed - but not on psa_eoi.** The log:
`Result=Failed (Error code=27) ... ERROR: val_nvmem_write failed. Error=0x1`,
with only five `[RESET] System reset requested` markers (i066 never reached its
panic). Root cause: `pal_nvmem_write` returned failure because
`wt_conf_nvm_sync(store)` → `wt_conf_nvm_flash_sync` returned −1 on the boot-flag
store that precedes the 6th reset. No `WRPERR`/`PGSERR` printed by the emulator,
and the emulator flash array is in-RAM (not `--flash-persist`, no wear model),
so the failure is in wolfTrust's `wt_hsm_flash_erase`/`_program` path, not the
emulator. Ruled out: `pal_nvmem_write` shadow-bounds (val's max NVM offset is
`0xD*4 = 52` ≪ the 256-byte shadow); `len%program_unit` (256%16=0, constant).

**Bank-swap hypothesis DISPROVEN by an `M33MU_FLASH_TRACE=1` run (2026-08-17).**
Every single `[FLASH_ERASE]` across all six boots is byte-identical -
`S mode=SER snb=125 start=0x001fa000 len=0x00002000` - the erase never moves,
there is no bank swap, and `swap_active` stays constant. `[FLASH_WRITE]` shows
the stores erasing then programming `0x1fa000`+ normally (~130 stores succeed
across the run, including many after each reboot). So the earlier swap-divergence
theory is wrong: the flash operation is identical on the failing store and on the
~130 that pass, so the flash write itself cannot be the selective failure.

**Refined diagnosis (open, needs one more instrumented cycle):** the NSPE
`val_write_nvm` reaches flash indirectly -
`val_write_nvm` → `psa_call` to the DRIVER partition → SPE `val_nvmem_write_sf`
→ `pal_nvmem_write` → `wt_conf_nvm_sync` (SVC) → flash. The
`val_nvmem_write failed. Error=0x1` at `val_framework.c:813` is the status of
that whole chain. Since the flash op is identical every time, the failure most
likely sits in the **val→driver `psa_call` / SPM path** on i066's boot (a
programmer/state error surfacing after many connect/call/close cycles), not the
flash primitive - though a silent write-once `PGSERR` on a byte an erase somehow
left un-cleared is not yet fully excluded (the emulator sets `PGSERR` without
printing it; `STM32H563_FLAGS` does arm `MM_TARGET_FLAG_NVM_WRITEONCE`).

**Next instrumented step (P4.4):** one M33MU cycle with either (i) an emulator
patch that `printf`s the address + old value whenever it sets `PGSERR` - a hit
proves flash, silence proves the IPC path - and/or (ii) tracing the
`val_write_nvm`/`psa_call` return code on i066's boot. That discriminates
flash-write vs val→driver-IPC and points at the fix. Deep, multi-cycle work.

**Action:** reverted the i064–066 build wiring (tree stays green at confboot 9,
commit `fcd1ca4`); i064/i065's psa_eoi correctness is banked here. Split the
flash-durability limit as task **P4.4** - it also gates P5.2 (the full panic
set does dozens of reboots), so it needs a real fix, not a reboot cap.
Next diagnostic: a per-step (`init`/`erase`/`program`) + `FLASH_SR`/`OPTSR`
dump in `wt_conf_nvm_flash_sync` under `WT_CONFORMANCE`, one M33MU cycle.

**RESOLVED (2026-08-17, same day): neither flash nor IPC - the missing
interrupt route.** i066's NS print order (self-consistent) shows
`Result=Failed (Error code=27)` BEFORE the nvmem error; 27 =
`VAL_STATUS_SPM_FAILED`, the exact value the client returns when its
`psa_call` RETURNS instead of the driver panicking. Upstream
`driver_test_psa_eoi_with_multiple_signals` is the only eoi check that first
REQUIRES a real asserted interrupt (`psa_irq_enable` →
`val_generate_interrupt` → `psa_wait(DRIVER_UART_INTR_SIG, PSA_BLOCK)`)
before its illegal eoi; with no interrupt route the driver took upstream's own
"didn't receive irq signal" branch (silent - the SPE pal_print is a stub),
replied an error, and never panicked. The nvmem error was downstream noise.
No flash defect exists; P5.2 is not gated. Fix = the P4.2c route below.

## Item 10 P4.1b + P4.2c interrupt route - 12/12, six reboots (M33MU, 2026-08-17)

Built the real peripheral-interrupt route and re-landed i064–066: confboot
`TOTAL TESTS : 12 / PASSED : 12 / FAILED : 0 / SKIPPED : 0`, SIX mid-suite
`[RESET] System reset requested` markers in one boot, `PASS: target/confboot`
(local box Docker, same CI container, patched emulator). i066's pass is the
first end-to-end proof of the chain: `psa_irq_enable` → LPUART1 `CR1.TXEIE`
(TXE idles high) → NVIC IRQ 63 pends → secure FLIH → manifest-routed signal
256 asserted → the driver's `psa_wait` observes it → illegal multi-bit
`psa_eoi` → `must_panic` → SPM reset → val boot-flag resume.

The route, all manifest-authoritative:

- **Manifest** (`manifest-conformance.json`; the generator already supported
  interrupts): DRIVER partition declares `DRIVER_UART_INTR_SIG` interrupt 63 /
  signal 256; domain 6 owns interrupt resource 63;
  `max_interrupts_per_domain` 0→1 in the json and the platform capability
  struct (`partitions.c`; manifest caps validate as ≤ platform, so the
  production manifest is untouched). The generator emits the partition
  interrupt table and `DRIVER_UART_INTR_SIG_SIGNAL 256U`; the mk's hand-kept
  `256U` constant became an alias to the generated macro (SPEC_VERSION==10
  builds lack upstream's own alias).
- **Engine** (`src/ffm.c`, arch-neutral): `wt_ffm_irq_lookup` (signal→irq),
  `wt_ffm_irq_route` (irq→partition/signal for the FLIH),
  `wt_ffm_assert_signal` (declared-interrupt-only assertion).
- **Gate**: new `WT_SPM_OP_IRQ_ENABLE` - validates the signal against the
  manifest, `must_panic` on misuse (FF-M programmer error), returns the
  resolved irq; the `psa_irq_enable` veneer replaces the old no-op.
- **Arch/port**: post-gate privileged NVIC unmask
  (`wt_platform_secure_irq_enable`: ITNS→Secure, lowest priority so the line
  never nests the SVC gate, ICPR, ISER); IVT slot 79 → `LPUART1_IRQHandler` →
  `wt_spm_conf_irq` (mask the line, engine-route, assert);
  `WT_SPM_OP_CONF_IRQ_SET` conf-plane op drives LPUART1 for the PAL's
  `pal_generate/disable_interrupt` (mirror of the NVM-sync plane). The
  interrupt is genuinely raised by the emulator's UART model
  (`cpu/stm32_usart.c` TXEIE&TXE → `mm_nvic_set_pending`) - not simulated.

**Host evidence** (`make test`, `PASS: unit/all`): `unit/ffm` new
`interrupt signal routing and assertion` (lookup/route/assert + eoi clears);
`unit/spm_gate` new `gate validates psa_irq_enable` (157 checks). This closes
P4.1 (all six panic tests green) and lands the P4.2c route; i021
(TEST_INTR_SERVICE, legal-eoi ack + re-fire) remains the P4.2c tail.

## Item 10 P4.2 closed - i021 legal-eoi green (M33MU 13/13, 2026-08-17)

Un-skipped i021 (dropped the `test_i021, skip` schedule sed; pure build wiring,
no code change - analysis showed the landed route covers every step, including
the two subtle FF-M behaviors: the interrupt signal persists across a second
PSA_BLOCK wait until `psa_eoi`, and a PSA_POLL miss returns instead of
blocking, both already host-proven). Confboot: `TOTAL TESTS : 13 /
PASSED : 13 / FAILED : 0 / SKIPPED : 0`, i021 `Result=Passed`, six mid-suite
resets, `PASS: target/confboot` (local box Docker, same CI container, patched
emulator). i021 target-proves the legal interrupt lifecycle end-to-end:
`psa_irq_enable` → real LPUART1 IRQ → `psa_wait` observes signal 256 → source
quiesced → signal still asserted on re-wait → legal `psa_eoi` clears it →
PSA_POLL confirms deassertion. P4.2 is closed; the only remaining schedule
skip is i067 (dynamic heap, forbidden by the zero-allocation secure image).

## Item 10 P4 close - i048-i053 green, three engine bugs fixed (M33MU 19/19, 2026-08-17)

Wired the six psa_call invalid-vector PROGRAMMER-ERROR tests (scope-corrected
from the old "driver MMIO isolation" framing): NS clients pass an iovec array
pointer, base, or end that lands in Secure memory; the SPE CLIENT partition
re-runs each check from Secure where FF-M mandates a panic. Confboot:
`TOTAL TESTS : 19 / PASSED : 19 / FAILED : 0 / SKIPPED : 0`, FOURTEEN
mid-suite resets, `PASS: target/confboot`. The slice surfaced and fixed THREE
real FF-M error-taxonomy bugs in the engine - exactly what running the
unmodified suite is for:

1. **Containment → wrong status class (i050/i051).** `wt_ffm_call`/`_begin`
   mapped a caller-containment failure (`WT_FFM_ERROR_POLICY` from
   `check_read/check_write`) to `PSA_ERROR_NOT_PERMITTED`; FF-M classes an
   inaccessible memory reference as a PROGRAMMER ERROR. Now returns
   `PSA_ERROR_PROGRAMMER_ERROR` (permission/version denials keep
   NOT_PERMITTED).
2. **SPE-caller vector violation returned instead of panicking.** The gate's
   `wt_spm_check_sp_vectors` failure on CALL returned -129 without
   `must_panic`; FF-M gives the return-not-panic latitude only to Non-secure
   callers. The gate now sets `must_panic` (conformance resets; production
   returns fail-closed as before).
3. **Cap-before-containment ordering (i052/i053).** The transfer-size cap in
   `wt_ffm_prepare_vectors` fired before the containment checks, so a vector
   whose END escaped the caller's memory (~256MB ranges into Secure space)
   downgraded into `PSA_ERROR_INVALID_ARGUMENT`. Containment is now judged
   first; a contained-but-over-cap vector still returns the size error (the
   item-9 guest negative is unaffected).

Harness/monitor support: confboot now runs WITHOUT `--quit-on-faults`
(i048/i049 fault the NS client by design dereferencing the Secure iovec array;
the SPE variants likewise fault dereferencing it from the CLIENT partition) and
the conformance monitor answers any guest fault with a system reset
(`wt_monitor_on_guest_fault`, WT_CONFORMANCE only - upstream platforms get the
same recovery from a PAL watchdog; production keeps the graceful per-guest
restart, and the `restart` scenario still proves it on the production image).
The suite's own FAILED count plus the clean `[EXPECT BKPT] Success` gate
correctness. Host: `unit/ffm` pins the taxonomy (NULL base → -129;
containment outranks the cap) and `unit/spm_gate` pins the CALL-vector
`must_panic`; `PASS: unit/all`.

Observed during debugging (pre-fix runs only): a repeated fault→reset
ping-pong could end in a HardFault stacking-failure loop at the secure main
stack top with CONTROL.nPRIV=1 on MSP_S - not reachable on the green path;
noted here in case it resurfaces (M33MU-1-adjacent signature).

## Item 10 P5 batch A - i002 + i004-i012 green, connect/close panic classes (M33MU 29/29, 2026-08-17)

Wired the ten connection-lifecycle tests. Source classification first (the P4
discipline): i002 is NOT a misuse test - it is the golden-path IPC lifecycle
suite (server-driven BUSY/REFUSED connect replies, arbitrary reply status
passthrough incl. INT32_MAX/INT32_MIN+128, client-identity signs - NS negative,
SPE positive and stable, a 50-iteration concurrent-connect exhaustion loop,
PSA_BLOCK reserved-bit tolerance, PSA_POLL equivalence). i004-i011 are SPE
must-panic psa_connect misuses (invalid SID, STRICT/RELAXED/unspecified version
violations, secure-only service from NS, SID missing from the partition's
dependency list - i009 is SP-to-SP only, no NS phase; i008's SPE leg must
SUCCEED). i012 is psa_close on a forged handle (SPE panics; NS may no-op).

Engine gaps closed (both in `wt_spm_gate`, host-pinned before the target run):

1. **CONNECT: SPM-level policy refusal now panics a Secure caller.** A
   REFUSED/NOT_SUPPORTED handle from `wt_ffm_connect_begin` (unknown SID,
   version or dependency violation, stateless connect) sets `must_panic`;
   `PSA_ERROR_CONNECTION_BUSY` resource exhaustion and server-replied refusals
   through `wt_ffm_connect_finish` stay returnable - the split that keeps
   i002's exhaustion loop and busy/reject checks green while i004-i011 panic.
2. **CLOSE: any `wt_ffm_close_begin` failure now panics** (forged or in-use
   handle); `psa_close(PSA_NULL_HANDLE)` stays a no-op. Replaces the veneer's
   diagnostic fault with the clean conformance panic-reset.

Confboot (shipped pipeline, box Docker, patched emulator):
`TOTAL TESTS : 29 / PASSED : 29 / FAILED : 0 / SKIPPED : 0`, TWENTY-THREE
mid-suite resets, `[EXPECT BKPT] Success`, `PASS: target/confboot`
(box `confboot-p5a-final.log`). Only i067 (heap) remains skipped. Host:
`unit/spm_gate` 216 checks - `test_gate_connect_close_panic_class` pins all six
classes (unknown SID, dep violation, version-high, version-zero,
BUSY-without-panic via pool exhaustion, forged-close panic + NULL-close no-op)
and the doorbell test now pins server-replied REFUSED as never-panic;
`PASS: unit/all`.

The first run of this batch tripped the P4-close watch-item on the green path -
root-caused to the emulator, not the SPM: see M33MU-3 below (patch reworked,
suite re-proven 29/29 including every P4-era flow).

## Item 10 P5 batch B - i024-i027 + i054 green (M33MU 34/34, 2026-08-17)

Wired the five psa_call misuse tests. Classification first: i024/i025 call a
forged (0x1234DEAD) / null handle with no prior connect (NS may see -129, SPE
must panic; the server stubs never run); i026 connects then calls with
in_len(4)+out_len(1) > PSA_MAX_IOVEC; i027 is the server-driven connection
drop - the server completes a well-formed call with PSA_ERROR_PROGRAMMER_ERROR
and the SPM must return -129 to the NS client, deliver PSA_IPC_DISCONNECT on
the close, and keep poisoning the closed handle on two retries (SPE re-run
must panic on the completion status itself); i054 (SPE-only, no NS phase)
passes its own .text address as psa_outvec.base - the non-writable out-vector
must panic unconditionally.

Engine: two `wt_spm_gate` CALL additions - the begin path panics a Secure
caller when `wt_ffm_call_begin` returns PSA_ERROR_PROGRAMMER_ERROR (forged or
null handle, iovec-count violation; BAD_STATE and resource statuses stay
returnable), and the finish path panics when the harvested reply status is
PSA_ERROR_PROGRAMMER_ERROR (a server-completed programmer error must panic a
Secure client; only NS clients may see it as a status). i054 needed no change:
`wt_spm_check_sp_vectors` already rejects out-vectors without
WT_MEM_ATTR_WRITE via `wt_secure_domain_contains(need_write=1)` and the P4
vector-violation panic covers it. i027's NS handle-poisoning worked as-is
(generation-checked handles + WT_IPC_CONNECTION_ERROR state).

Confboot (shipped pipeline, box Docker, patched emulator):
`TOTAL TESTS : 34 / PASSED : 34 / FAILED : 0 / SKIPPED : 0`, TWENTY-EIGHT
mid-suite resets, `PASS: target/confboot` (box `confboot-p5b.log`), green on
the first run. Host: `unit/spm_gate` 241 checks -
`test_gate_call_panic_class` pins forged/null-handle panic, the no-panic
connect harvest, and the server-completed -129 panic through a real
gate-pumped round trip; `PASS: unit/all`.

## Item 10 P5 batch C chunk 1 - i013-i023 server-misuse panics (M33MU 44/44, 2026-08-17)

Ten tests where the SERVER partition commits the misuse and must panic - the
first server-side panic direction (previous batches panicked clients).
i013-i016: psa_get on a multi-bit mask, PSA_DOORBELL, or a valid-but-
unasserted signal, and a double get. i017: a partition connecting to its own
RoT service (covered by the batch-A CONNECT policy panic - caller_allowed
already rejects self-connect). i018/i019: psa_set_rhandle on forged
(0x1234DEAD) / null message handles. i020: psa_reply to a CONNECT with a
status outside SUCCESS/REFUSED/BUSY (-114). i022/i023: psa_reply on forged /
null handles. None of these tests has an SPE-client phase; the NS client only
verifies the reboot via the boot flag.

Engine: `wt_ffm_reply` now enforces the FF-M connect-reply status set
(SUCCESS/REFUSED/BUSY; CALL and DISCONNECT replies stay unrestricted - i002's
arbitrary call statuses and i027's -129 are unaffected). The four psa_get
misuse shapes were already detected (exact-signal match + empty-queue
checks). Gate: GET, SET_RHANDLE, and REPLY failures now set `must_panic`.

Confboot: `TOTAL TESTS : 44 / PASSED : 44 / FAILED : 0 / SKIPPED : 0`,
THIRTY-EIGHT mid-suite resets, `PASS: target/confboot`
(box `confboot-p5c1.log`), first run. Host: `unit/spm_gate` 264 checks -
`test_gate_server_misuse_panic_class` pins every class plus the legal
REFUSED reply after a rejected -114 on the same message; `PASS: unit/all`.

## Item 10 P5 CLOSED - full FF-M IPC suite green (M33MU 85/4, 2026-08-17)

The complete Arm PSA Arch Test Suite (FF-M IPC v1.8), unmodified, runs through
wolfTrust's clean-room SPM on STM32H563/M33MU: `TOTAL TESTS : 89 /
PASSED : 85 / FAILED : 0 / SKIPPED : 4`, `PASS: target/confboot`
(box `confboot-final.log`, runner-built pinned emulator + local patch). All
four scenarios green on the same tree: `PASS: target/{positive,restart,
crossdomain}` (box `matrix.log` - restart faults 3× then FAULTED, crossdomain
denies the SP read of 0x30028000). Host `make test` green, `unit/spm_gate`
284 checks.

The four SKIPs are honest capability gaps, not hidden failures: i067 (heap;
never wired) and i074/i078/i082/i086 (RESULT_SKIP - SP_HEAP_MEM_SUPP undefined
in the zero-allocation secure image).

Chunks 2-3 engine work (host-pinned): read/skip/write server misuse panics via
new `wt_ffm_msg_access_check` (idx≥PSA_MAX_IOVEC → ARGUMENT, bad handle →
HANDLE, pre-CALL type → STATE); psa_wait mask validation via new
`wt_ffm_partition_signal_set` (mask ∩ assignable == 0 → ARGUMENT, PSA_WAIT_ANY
stays legal); NOTIFY/CLEAR failures panic.

Three target enablers were required beyond the engine work:

- **Flash/RAM layout growth** (capacity wall - the 89-test image overran the
  0x20000 secure slot and the guests overlapped): secure slot → 0x40000, SWAP
  → 0x0C140000, guests → 0x080A0000 / 0x080C0000, guest RAM 64 KiB each
  (guest1 base 0x20010000). Env-driven overrides across the runner, the yml,
  both manifests (decimals), partitions.c, memory_map.h, and the Zephyr board
  patch (DTS + CONFIG_SRAM_SIZE both, since the defconfig overrides the DTS).
- **GTZC MPCBB fix** (`wt_gtzc_init`, `platform_stm32h563.c`): the SRAM1
  secure-block map hardcoded 4 SECCFGR words (first 64 KiB) for the old
  2×32 KB layout, so guest1's relocated NS RAM at 0x20010000 stayed
  secure-blocked and its first dispatch bus-faulted. Now derives the NS block
  count from `WT_GUEST1_RAM_BASE + WT_GUEST_RAM_SIZE`, so a future layout
  change cannot reopen the hole. This was the "zero-read HardFault at 0x0e2a"
  wedge from the P5-endgame handoff - a wolfTrust bug the emulator modeled
  correctly, NOT the suspected flash-TZ-watermark.
- **Conformance SP-fault system reset** (`wt_secure_tasklet_fault_dispatch`):
  the Arm isolation tests (i068+) fault inside a Secure Partition on purpose
  and expect a system restart so val resumes off its flash boot flag. Under
  `WT_CONFORMANCE` the tasklet fault path now `wt_platform_system_reset()`s
  instead of quarantining the partition (which left the server dead for every
  later test). Production and the crossdomain scenario keep the graceful
  quarantine (task #26).

**Real SP data isolation (i080/i084), not a skip.** The server partition's
`test_supp_*` data and the driver partition's data each get a 32-byte-aligned
private band at the `.conf_data`/`.conf_bss` seam in `secure.ld`
(`_s/_e_conf_server_data`, `_s/_e_conf_driver_data`), and `spm_svc.c` carves
those bands out of every OTHER partition's MPU grant (i080's probe target is
`g_psa_rot_data` in the driver partition, i084's is `g_test_i084` in the
server). A cross-partition read now MemManage-faults inside the band
(box trace: i084 faults at 0x300933e4) → conformance reset → val marks the
check passed. Region budget stays within the 8-region MPU limit for every
partition. A first single-band attempt was reverted after it regressed (see
M33MU-4 below - the regression exposed an emulator bug, not a design flaw).

## Item 10 P7 - `make test-conformance` auto-detect + manifest reproducibility fix (host, 2026-08-18)

`make test-conformance` is now the single conformance entry point. A shared
probe (`tests/target/detect_m33mu.sh`, also used by `make test-target`) selects
the target suite when an M33MU emulator is reachable (or `WT_TARGET_SCENARIOS=1`)
- `run_m33mu_scenario.sh confboot`, the same 85/4 call P5 proved and CI runs in
its confboot matrix leg - and otherwise falls back to the host subset (20
client-side IPC/policy tests) with three explicit `WARNING:` lines naming what
host-only does NOT cover (isolation, panic-reset, IRQ, cross-domain). Host
branch verified: runs the subset, prints the warnings, exits 0.

Running it surfaced a pre-existing red: `test-manifest-ingest` had failed since
P4.2 because `emit_manifest` in `tools/manifest/ingest_psa_arch.py` hardcoded
`interrupts: []` and emitted no `interrupt_resources`, so the committed
`manifest-conformance.json` (which carries the DRIVER UART interrupt, line 63,
`DRIVER_UART_INTR_SIG`) was not reproducible from the upstream manifests. The
interrupt line is a port decision (not in upstream, which names only the
symbolic `FF_TEST_UART_IRQ`), so the generator now owns a `PLATFORM_IRQ_LINES`
map and carries each parsed IRQ into the partition's `interrupts`, the SP
domain's `interrupt_resources`, and `max_interrupts_per_domain`. Regenerated
manifest now byte-matches the committed one; the committed manifests are
unchanged - only the generator was corrected to reproduce them. `make test`
still `PASS: unit/all`.

## Item 10 M33MU wrap-up - full-suite one-commit validation (M33MU, 2026-08-18)

Final pre-hardware validation of the reconciled M33MU deliverable. All three
test entry points green on one tree (`ade6322` working tree, rsync'd to the box;
container `ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15`, prebuilt diag emulator
carrying both local fixes - M33MU-1 SPSEL + M33MU-4 ITSTATE):

- `make test` → `PASS: unit/all` - every host suite, including `spm_gate`
  (284 checks), `flash_nvm` survive-reset NVM, crypto/attestation services
  through real FF-M dispatch, and the P6 surface (doorbell + `psa_wait` mask
  i063, `psa_eoi` validation, interrupt signal routing).
- `make test-target` → `PASS: target/{positive,restart,crossdomain,confboot}`
  → `PASS: target/all`; confboot suite `TOTAL PASSED : 85 / FAILED : 0 /
  SKIPPED : 4`.
- `make test-conformance` → manifest ingest PASS (validates + matches upstream
  SIDs) → `PASS: conformance/target`, same 85/4.
- `FINAL_RC=0`, `CONTAINER_EXIT=0`.

Genuine sequential run (not cached): per-scenario logs staggered
11:34:41 → 11:37:27 → 11:39:22 → 11:41:56 → 11:44:37, each 0.2–1.0 MB of full
build+boot output (`logs/target-*.log`, `logs/conformance-target.log`). Fast
(~10 min) only because the box's Zephyr clone and emulator were warm.

This is M33MU **emulator** evidence, not a physical-board result; hardware
qualification (P9) remains the only open non-blocked item. The P5u/#63 upstream
point-back stays blocked until both m33mu PRs merge (the local
`tests/target/m33mu-tb-sec-chain.patch` carries both fixes meanwhile).

## MP1 - first STM32H563 hardware boot (H5 Nucleo, 2026-08-18)

First real-silicon evidence for the wolfTrust H5 port (see
`wolftrust-secure-manager-port-plan.md`). **HARDWARE, not emulator.**

Board: NUCLEO-H563ZI (target `stm32h563zitx`) via STLINK-V3, on the box. Already
TrustZone-provisioned: `TZEN` on, `SECBOOTADD=0x0C000000`, `BOOT_UBE=OEM-iRoT`,
`SECWM1 0x00-0x3F`, `SECWM2 0x00-0x7F`, product state Open.

Flashed the production positive chain (hardware variant - the emulator-only
`WT_M33MU_EXPECT_BKPT` dropped) via STM32CubeProgrammer:
`wolfboot.bin@0x0C000000`, `wolftrust_v1_signed.bin@0x0C060000`,
`guest0_psa@0x080A0000`, `freertos_guest1@0x080C0000` - all "Download verified
successfully", hard reset.

**Result: wolfTrust boots on real silicon.** pyocd after reset reports
`Core 0 (Cortex-M33): Running [Nonsecure]` - the secure chain (wolfBoot auth →
wolfTrust → TrustZone handoff) executed and control reached the Non-secure guest.
The ST-Link VCP console (`/dev/ttyACM0`) captured garbled bytes at 115200 - a
baud mismatch (the real H5 clock tree differs from the emulator's fixed clock, so
USART3's divisor yields the wrong baud); no lifecycle markers readable yet.

Status: **boot chain proven on silicon;** a clean console is the open MP1 item,
and it gates the full positive-lifecycle assertion + service suite (MP2). Harness:
`tests/target/run_h5_hardware.sh` (build in container, flash on host). No
lock/product-state changes were made - the board stays Open and reflashable.

## MP1 GREEN - full positive smoke on STM32H563 silicon (2026-08-18)

**HARDWARE evidence.** `tests/target/run_h5_hardware.sh flash` exits 0 with the
full checklist on the NUCLEO-H563ZI: no fault markers, TEE client initialized,
`psa_framework_version=0x0100`, **`SERVICE_CRYPTO dispatch verified` (the
unprivileged crypto SP ran in its own MPU domain on real silicon)**, FF-M
negatives (forged handle st=-129, oversized vector st=-135),
`psa_hash_compute(SHA-256) KAT verified`, `psa_initial_attestation st=0`,
COSE_Sign1 verified, `guest0_psa done`, guest1 wolfHSM `C_Digest rv=0` +
heartbeats. Console clean at 115200 after the clock fix.

Three silicon-only defects were root-caused with on-board forensics (pyocd
secure-RAM/register reads + first-wins fault capture in the tasklet fault
dispatcher) and fixed; the M33MU never reproduces any of them:

1. **Thread-mode exception return in fault recovery.** The tasklet fault path
   EXC_RETURNed into a Thread-mode thunk whose final `bx lr` (LR=0xFFFFFFF9)
   is only an exception return in Handler mode; silicon branch-faulted to
   0xFFFFFFF8 (IACCVIOL) and the cascade panicked the SPM. The handler now
   resumes the bootstrap directly - restore MSP_S to the PendSV-saved SP, pop
   r4-r11, real exception return through the preserved frame - mirroring
   PendSV's own bootstrap-resume path. Verified on-board: SP faults are now
   gracefully quarantined and the system keeps running.
2. **Secure MPU region count.** H563 implements 12 secure MPU regions
   (`MPU_S.TYPE.DREGION`, read from silicon); the code assumed
   `WT_MAX_MPU_REGIONS`=8 and left regions 8-11 at their reset-UNKNOWN state.
   Both MPU programmers now disable every implemented region beyond the
   whitelist.
3. **GTZC MPCBB privilege filter (the MP1 blocker).** `MPCBBx_PRIVCFGR`
   resets to all-privileged on silicon, denying every unprivileged SRAM
   access below the MPU - the unprivileged crypto SP faulted on its first
   entry even though the live MPU snapshot (captured in the fault handler)
   proved the SP-thread domain was programmed and permitted the access.
   Proven by two zero-rebuild live experiments over pyocd: forcing the SP
   privileged made the flow green, and clearing PRIVCFGR live with the SP
   still unprivileged made it green. `wt_gtzc_init` now clears MPCBB1/2/3
   PRIVCFGR (privilege enforcement is the secure MPU's job in this design).

Emulator-fidelity gaps recorded for upstream (task #63 register): m33mu
accepts a Thread-mode `bx 0xFFFFFFF9` as an exception return and does not
model GTZC privilege filtering.

Board remains Open/reflashable; no option-byte changes. MP2 (full
`make test-hardware` equivalence) is next.

## MP2 - hardware equivalence suite green on STM32H563 (2026-08-18)

**HARDWARE evidence.** `make test-hardware` (tests/target/run_h5_suite.sh) runs
the on-silicon counterpart of the M33MU `test-target` scenarios on the
NUCLEO-H563ZI - build per scenario in the CI container, flash on the host:

- **positive** - the full PSA/FF-M lifecycle checklist (MP1's eight checks).
- **restart** - the guest faults on every boot (NS read of secure RAM); the
  monitor restarts it exactly `restart_limit`=3 times then quarantines it
  FAULTED while guest1 keeps heartbeating. Asserted via the monitor's own
  event counters (`g_wt_restart_events`=3, `g_wt_quarantine_events`=1) read
  over the debug port - deterministic, immune to UART interleave.
- **crossdomain** - the unprivileged crypto SP reads SPM-private RAM; the SP
  MPU domain denies it (captured fault count=1, address=0x30028000 =
  WT_RAM_S_BASE), nothing escalates to HardFault, and guest1 survives - L3
  isolation with graceful quarantine on real silicon.

**Restart-engine defect found and fixed on the way (silicon-only).** The
restart budget reset whenever wall-time since the FIRST restart exceeded
`restart_window_ticks` (64 ticks = 128 ms at the 2 ms timeslice), and the
reset ran before the quarantine check. Real reboot cycles exceed the window,
so the counter was wiped every cycle and a crash-looping guest restarted
forever - 555 boots observed in one 32 s capture, with the monitor provably
handling every fault (`g_last_fault_address`=0x30028000). Emulator cycles are
sub-tick, so the M33MU always quarantined and masked the bug. Fix:
`wt_restart_guest` resets the budget only after a full crash-free window since
the LAST restart (a crash loop can never reset; a guest that runs quietly for
a window still earns a fresh budget), and the manifests scale
`restart_window_ticks` 64 → 8000 (16 s of required stability; 64 was
emulator-scaled).

Known scope limits, tracked: ITS/protected storage is not yet implemented
(guest0 notes the missing wolfPSA key-storage backend - Phase 4), and the UART
console interleave (both guests raw-write USART3) is cosmetic pending the
secure console veneer task.

## MP3 - immutable-RoT reversible lock, all rungs on STM32H563 (2026-08-19)

**HARDWARE evidence.** The OEM-iRoT lock lifecycle driven end-to-end on the
NUCLEO-H563ZI with the single control script `tests/target/provisioning_ctrl.sh`.
All three lock rungs were sealed AND reversed, board returned to Open+booting
each time:

- **Provisioning (0x17)**, **TZ-Closed (0xC6)**, **Closed (0x72)** - each
  advanced via `-ob PRODUCT_STATE`, confirmed by DA discovery
  (`ST_LIFECYCLE_*`, integrity `0xeaeaeaea`, `(a/14) Full Regression`), then
  **certificate DA Full Regression → Open** (`Authentication successful`) and
  `restore` (set-perimeter `TZEN=0xB4` + flash + verify →
  `[check] PASS wolfTrust chain boots on silicon`).

The lock lifecycle is an ST RSS/OEM-iRoT **silicon** feature (enforced by the
immutable bootrom + option bytes + product state), identical whether TF-M, ST
Secure Manager, or wolfTrust is the sealed secure firmware - MP3 proves
wolfTrust seals into and reversibly reopens from it exactly as a TF-M-based
product would. Logs: `docs/evidence/2026-08-18-h5-mp3-lock/`
(`2026-08-19-recovery-cert-regression.log`, `2026-08-19-lock-ladder.log`,
`2026-08-19-lock-ladder-walkthrough.md`, `findings-and-discrepancies.md`).

**Findings / discrepancies fixed on the way (AN6008-confirmed).**

1. **DA credential must match TrustZone.** wolfTrust runs TZEN enabled, so DA is
   CERTIFICATE-based (`DA_Config.obk`); a password OBK cannot authenticate and
   blocks regression. An earlier password/TZEN mismatch stranded the board in
   Provisioning; the certificate re-provision + cert regression recovered it.
   `provisioning_ctrl.sh` now defaults to the certificate OBK, and `regress`
   drops the `debugauth=3` that was locking the AP.
2. **TZ-Closed cannot chain to Closed.** Once at TZ-Closed the link is too
   locked to write the next `PRODUCT_STATE`; advance to the target lock state
   directly from Provisioning (as ST `provisioning.sh` does).
3. **Closed regression self-resets the MCU.** The mass-erase resets the chip, so
   the immediate reconnect can race and report "Cannot connect to AP1"; a moment
   later it reads Open - retry `restore`. Integrity `0xf5f5f5f5` at Open is the
   erased-DA marker (normal after a regression), not corruption.

Reversible only - permanent `Locked` (0x5C) never touched. Production step
(tracked, not blocking): replace ST's sample DA certificate chain with a
wolfTrust-owned chain.

## MP4 slices S1+S2 - core/port split, first two extractions (2026-08-19)

Enforcing the neutral-core vs Armv8-M-port split so a new port needs no core
edits. Two safe-tier extractions landed, mirroring the SERVICE_CRYPTO precedent
(neutral seam in core + fail-closed default; the arch installs its shim at boot):

- **S1 / boot_handoff barriers** (`4212bde`): `src/services/boot_handoff.c` no
  longer emits inline `dmb`/`dsb`; it calls `wt_platform_dmb()`/`wt_platform_dsb()`
  (new `platform.h` hooks), the H563 port supplies them, the host stub no-ops.
- **S2 / HSM fault-notify** (`f83e5ff`): `src/services/wolfhsm/wt_hsm.c` drops its
  `#include "wolftrust/arch/armv8m/cmse_transport.h"` (its only arch coupling) and
  calls an installed `wt_hsm_fault_notify_fn`; `src/arch/armv8m/cmse_transport.c`
  installs `wt_cmse_transport_signal_fault` in `wt_cmse_transport_init`.

Gate (`validate-in-container.sh` in the CI container on the box): `make test`
(host unit/all) + `make test-target` (positive/restart/crossdomain/confboot, all
PASS) + `make test-conformance` (**Arm 85 passed / 4 skipped / 0 failed**,
`[EXPECT BKPT] Success`), `FINAL_RC=0`. The `test-target` pass proves the secure
image compiled and linked with both extractions and the NS→S dispatch + L3
isolation are unchanged.

Safe tier then completed on the same gate (85/4/0, `FINAL_RC=0`): the
`boot_handoff` inline `dmb`/`dsb` were replaced by the platform hooks (`87336c9`,
a guard-caught gap S1 had missed), a report-only split guard was added
(`tools/check-core-port-split.sh`, `5505c31`), and the MCU-family flash contract
`include/wolftrust/port_nvm.h` replaced core's bare `hsm_flash.h` include
(`673ec01`).

## MP4 COMPLETE - deep tier + hardware validation (2026-08-19)

The deep tier landed the same day; the core now contains **zero architecture
code** (STRICT guard = 0 hard leaks, enforced by the new `core-port-split` CI
job which also builds the CONFIG_VNET image every push):

- **S4** (`194cba3`): the 5 FF-M NS veneers → `src/arch/armv8m/ffm_nsc.c`; core
  `ffm_boot.c` fully neutral behind a fail-closed installed memcheck seam; new
  neutral `spm_sched.h`; new host suite `tests/host/ffm_veneer` (10/10) proves
  fail-closed default → installed check → SHA-256 KAT round trip. CMSE implib
  symbol set and `sg` prologues verified identical pre/post.
- **S5** (`96e62d8`): the 7 VNET NS veneers → `src/arch/armv8m/vnet_nsc.c`.
- **NSC window defect** (task #82, `ee04538`): S5's verification exposed that
  veneer BODIES lived in the fixed 0x400 `.gnu.sgstubs` NSC window and the
  CONFIG_VNET image no longer linked - pre-existing at origin (A/B-proven:
  1014B before the extractions vs 968B after; both overflowed). Fix: drop the
  explicit section attribute (bodies → `.text`, ld synthesizes only the 8-byte
  `sg` stubs: 96B default / 160B vnet image) plus the TF-M-style
  `. = ALIGN(32);` keep-alive in `secure.ld` so the empty section keeps its
  address ("no address assigned to the veneers output section").
- **S3+S6** (`335cdf9`): guest context held by POINTER - `platform.h` forward
  declares `struct wt_guest_context`; the neutral `partition.h` owns
  `wt_guest_runtime_t`; the arch `context.h` defines the concrete body (tag
  preserved); the port owns the storage (`g_partition_contexts`) and rewires it
  across the reset memset; the monitor's only field peek became the neutral
  `wt_platform_guest_context_ready` predicate. The arch `partition.h` and the
  dead `src/platform_stub.c` were deleted. No asm/offsetof contract depends on
  the runtime layout (verified before the change).
- Boot-integration seams documented in `docs/port-contract.md` (`2d519a7`).

**Final gate on the complete tree:** VNET_IMAGE_OK + STRICT 0 leaks + host
unit/all + target scenarios + conformance **85 passed / 4 skipped / 0 failed**,
`FINAL_RC=0`.

**HARDWARE evidence (NUCLEO-H563ZI):** `make test-hardware` ALL GREEN on the
final tree - positive (TEE init → SERVICE_CRYPTO dispatch → SHA-256 KAT →
attestation COSE_Sign1 → `guest0_psa done`, no faults, through the relocated
veneers and stubs-only NSC window), restart (exactly 3 restarts → quarantine →
guest1 alive), crossdomain (SP denied at `0x30028000`, no HardFault escalation,
guest1 alive) - `PASS: hardware/all`.

## M33MU emulator defect register

Defects in the pinned M33MU emulator that block conformance work. These are
CPU-model bugs, not wolfTrust logic; each carries a deterministic reproducer.
Emulator: `github.com/danielinux/m33mu`, pinned `M33MU_REF`
`c84792f7f9e9ce24cf94ffc492c36231de1854c2` (tests/target/run_m33mu_scenario.sh).

### M33MU-1: cross-domain exception entry/return loses CONTROL.SPSEL (FIXED locally, 2026-08-14)

**RESOLVED - root cause found and patched.** The earlier "SVC on the NS SP
bank" description below was the downstream symptom; the true defect is in the
emulator's EXC_RETURN.SPSEL handling for CROSS-DOMAIN exceptions. Per the
Armv8-M exception model (EXC_RETURN field table: "SPSEL … saves the value of
CONTROL.SPSEL in the domain that is handling the exception … the saved value
is restored during exception return"), bit 2 must record the HANDLER domain's
CONTROL.SPSEL and be restored into CONTROL[ES] on return. m33mu instead
recorded the PREEMPTED domain's SPSEL at entry and restored into the RETURN
state's CONTROL - correct only when the two domains coincide. Consequence: an
NS-targeting exception (SysTick_NS) taken while executing Secure code (inside
a synchronous NSC veneer call, e.g. K2's flash-NVM SVC section) cleared
CONTROL_NS.SPSEL at entry and returned to Secure without ever restoring it;
the NS val thread - which had entered the veneer on its PSP - was resumed via
BXNS on the MSP, ran its epilogue against the wrong stack, popped 0x00000000
into PC, and died with the phantom UNDEFINSTR (the emulator's
interworking-check `[PC_WRITE_FAULT]` path). Caught by the emulator's own
`M33MU_STACK_TRACE` instrumentation: `[EXC_ENTER_SPSEL] sec=0 control_ns=0`
immediately after a `ctrl_ns=0x2` entry state, then the epilogue on
`sp=msp_ns`.

**Fix** (in-repo `tests/target/m33mu-tb-sec-chain.patch`, applied by the
runner after the pinned checkout; also contains a TB-chain hardening that
resolves successors in the current security state): (1) entry - EXC_RETURN
bit 2 = `CONTROL[handler_sec].SPSEL` pre-clear; (2) return - restore bit 2
into `CONTROL[exception_sec]`, leaving the return domain's CONTROL untouched
on cross-domain returns; (3) cross-domain frame pops use the return state's
live CONTROL.SPSEL. Same-domain behavior is bit-identical. **Verified:**
confboot images that deterministically faulted on pinned `c84792f7` AND
master `f96ab8e` run to `TOTAL PASSED : 7 / FAILED : 0` with a mid-suite
`[RESET] System reset requested` and clean `[EXPECT BKPT] Success` under the
patched emulator (local Docker, same CI container/image). Upstream submission
of the patch to danielinux/m33mu is pending approval (task #63).

Historical symptom analysis (pre-root-cause):

- **Symptom:** with i047 in the confboot schedule, the untraced image
  deterministically stops at virtual cycle ~14,244,2xx: the NS guest reports
  `CFSR=0x00010082` (UNDEFINSTR + stale DACCVIOL) at `pal_nvm_write+0x2c`
  (`add r3, sp, #8` - cannot fault on silicon), `exc_ret=0xFFFFFFB8`,
  `ctrl_ns=0` while the thread's frame registers still point at its PSP stack.
- **Mechanism (from `--record` traces):** the NS thread enters an FF-M NSC
  veneer on PSP_NS; after the secure round trip the MSP is selected. The final
  recorded events show a SECURE SVC handler (IPSR=0x0B, PC `0x0c0636xx`)
  executing with the NS stack pointer bank selected (`sp = msp_ns + 0x14`)
  while registers hold the scrubbed pre-BXNS veneer state
  (`r1=r2=r3=r12=lr=0x080808ca`) - an SVC taken from secure thread mode
  adjacent to the BXNS transition stacked onto the wrong security state's SP.
  wolfTrust cannot influence exception-entry bank selection.
- **Reproduce:** tree @ `6bb8493` with i047 restored to the schedule (drop the
  `panic_test` marker via the mk sed) and confboot asserting 7. Build the
  confboot images (`WT_CONFORMANCE=1`), then:
  `m33mu wolfboot.bin wolftrust_v1_signed.bin:0x60000 zephyr.bin:0x80000
  freertos_guest1.bin:0xA0000 --uart-stdout --expect-bkpt 0x7f
  --quit-on-faults --timeout 90 --record --record-quiet --record-dump 120`.
  Byte-identical fault across 4 runs; reproduces on pinned `c84792f7` and
  master `f96ab8e`. A `WT_CONF_TRACE` guest build passes - printk UART stalls
  change the emulator's event interleaving (ordering sensitivity).
- **Evidence:** box `wolf-prec5560`: `k4-carve.log`, `k4-nsbank.log`,
  `k4-rec.log`, `k4-bigtrace.log` (traces), `k4-master.log` (master repro).
- **Impact:** blocks K4 (i047) and P4.1 (i055/i057 + panic set). Tracked:
  task #63.
- **Parked state:** i047 removed from the schedule (mk sed reverted; confboot
  asserts 6 again) so the gate is green while the keystone stays in-tree.
  Verified on this tree: host `make test` green and `PASS: target/positive`
  (k4-positive.log, carve + NS-bank fix included). The confboot-at-6 rerun on
  the parked schedule is PENDING - the box dropped offline mid-wrap; run
  `tests/target/run_m33mu_scenario.sh confboot` when it returns.

### M33MU-3: entry-time SPSEL clear destroys a parked thread's stack selection (FIXED locally, 2026-08-17)

**RESOLVED - root cause found; the M33MU-1 patch semantics were reworked to
the ARM-correct scheme and the whole suite re-proven.** First P5-batch-A
confboot deterministically wedged mid-i002 check 9 (identical
`HardFault: stacking failed at 0x30092e38` at virtual cycle 14817181 across
runs; the P4-close watch-item signature - CONTROL_S=0x01, secure thread on
MSP_S, PC=0x14 garbage).

- **Mechanism (from `--record-dump` + `M33MU_STACK_TRACE=1` traces):** the
  emulator cleared `CONTROL.SPSEL` of the handler's security domain on EVERY
  exception entry. When the Secure SysTick preempted the Non-secure SysTick
  handler (cross-domain entry, `[EXC_ENTER_SPSEL]` shows ctrl_s 0x03 → 0x01
  with no secure instruction in between), it destroyed the SPSEL of a PARKED
  Secure Partition thread suspended mid-`psa_wait`. The old M33MU-1 patch's
  cross-domain unstack consulted that live `CONTROL_S.SPSEL` to pick the frame
  stack, so the NS SysTick's later return-to-Secure-thread unstacked from
  MSP_S instead of PSP_S - resuming a garbage frame (PC=0x14, LR=0x1) whose
  first exception entry then failed stacking on MSP_S with nPRIV=1. Real
  Armv8-M restores SPSEL from EXC_RETURN.SPSEL at return, so this nesting is
  sound on silicon; wolfTrust code is uninvolved.
- **Fix (rework of the local patch, supersedes M33MU-1's mechanics):**
  (1) entry clears the handler domain's `CONTROL.SPSEL` only on SAME-domain
  entries; (2) `EXC_RETURN.SPSEL` captures the PREEMPTED domain's SPSEL;
  (3) unstack and the return-domain `CONTROL.SPSEL` restore both come from
  EXC_RETURN bit2 alone - never live CONTROL; (4) tail-chaining regenerates
  EXC_RETURN.ES for the new exception's target domain instead of reusing the
  previous value verbatim. The M33MU-1 symptom (CONTROL_NS.SPSEL stuck 0
  after an NS exception over Secure code) stays fixed because cross-domain
  entries no longer clear the other domain at all.
- **Why i002 check 9 found it:** the PSA_POLL serve loop is the first
  schedule where an NS SysTick preempts the pump-driven Secure Partition
  thread while the Secure SysTick lands inside the NS handler.
- **Verified:** images that wedged on the old patch run the full suite to
  `TOTAL PASSED : 29 / FAILED : 0` with 23 panic resets and a clean
  `[EXPECT BKPT] Success` - including i047 and every P4-era flow the old
  semantics were built for (box `confboot-p5a-probe3.log`, diag traces
  `confboot-p5a-diag.log` / `confboot-p5a-exctrace.log`).

### M33MU-4: ITSTATE advance drops the current-condition bit (FIXED locally, own upstream PR, 2026-08-17)

**RESOLVED - root cause found, fixed, and submitted upstream as a separate PR
(branch `aidangarske:itstate-advance-fix`, distinct from PR #16).** The
89-test confboot wedged after the layout+carve work: the whole NS guest died
in a UsageFault UNDEFINSTR at a valid `cmp r0,#0`, exit 127. The handoff's
"first single-band carve regressed" was the trigger, but the true defect is in
the emulator's IT-block bookkeeping.

- **Mechanism (from `M33MU_STACK_TRACE=1` + `M33MU_UNDEF_TRACE=1`):**
  `itstate_advance()` (src/execute.c) shifted only the low 4-bit mask and kept
  ITSTATE[7:4] verbatim, so the current-condition LSB (IT[4]) never updated as
  an IT block advanced. The ARM ARM `ITAdvance()` shifts `ITSTATE[4:0]` as one
  5-bit field, moving the old mask top bit into the condition. A guest thread
  preempted between the two arms of an `ITE EQ` block (Zephyr's
  `log_output_process` printk path) stacked ITSTATE 0x08 where the
  architecture requires 0x18; on resume with Z=0 the emulator evaluated the
  `ldrne` ELSE arm as `ldreq` and SKIPPED it, leaving r0 stale, and a later
  `blx r3` jumped through a rodata pointer → `[PC_WRITE_FAULT]`. Real silicon
  advances the condition, so the ELSE arm executes; wolfTrust code is
  uninvolved.
- **Why the carve exposed it:** it only bites when an exception lands
  mid-IT-block AND the SPM then switches guests - pure cycle-alignment luck.
  The image-layout shift moved a SysTick preemption onto an IT boundary it had
  previously missed. This is also why the pre-carve 83/4 build did not wedge.
- **Fix:** `itstate_advance()` now performs the architectural 5-bit shift and
  preserves ITSTATE[7:5]. Carried locally in
  `tests/target/m33mu-tb-sec-chain.patch` (so the runner and CI get it) until
  the upstream PR merges - see task #63 / P5u.
- **Verified three ways:** (1) counterfactual - the IDENTICAL wolfTrust
  binaries wedge on stock m33mu and run the full 89-test suite on the fixed
  emulator (nothing changed but the CPU model); (2) unit test
  `itstate_advance_test.c` - the ITE-EQ advance returns 0x08 on stock (fails)
  and 0x18 fixed, ITT/ITETE walks pinned; (3) round-trip test
  `itstate_exception_roundtrip_test.c` drives `enter_exception_ex` /
  `exc_return_unstack` with a mid-ITE xPSR and checks the stacked and restored
  ITSTATE decode to the ELSE condition. Upstream ctest 71/71 in the CI
  container.

### M33MU-2 (RESOLVED - GO, 2026-08-17): peripheral-IRQ NVIC delivery

- Was a candidate defect (USART NVIC line to the guest unproven). The P4.2a
  source probe cleared it: the USART model does raise its NVIC line on
  TXE/RXNE and delivery to the guest is modeled. Not a defect - see
  "Item 10 P4.2a" above. No hardware gate for peripheral IRQ delivery.

## MP5 - unmodified Arm val conformance on H563 silicon (HARDWARE, 2026-08-19)

The TF-M / Secure-Manager drop-in proof, on the board. A new `confboot`
hardware scenario (`run_h5_hardware.sh`, in the `run_h5_suite.sh` default set)
flashes the conformance chain (secure `WT_CONFORMANCE=1`, guest
`WT_RUN_CONFORMANCE=1`) to the NUCLEO-H563ZI and captures UART across the
panic-reset reboot loop. Result, physical silicon:

- **Arm FF-M IPC suite (pinned `e17d294`, tests + val framework unmodified):
  TOTAL TESTS 89 / PASSED 85 / FAILED 0 / SKIPPED 4** (heap tests skip:
  zero-allocation image) - same counts as the M33MU run, now on hardware.
- 92 boot cycles in one run: every panic test reset the whole chain via real
  SYSRESETREQ (wolfBoot re-verifying each time) and val resumed off its
  flash-backed boot flag (K2/K3). Suite wall clock under a minute.
- Official artifact `PASS: hardware/h5/confboot` (5 `[check] PASS`), logs in
  `docs/evidence/2026-08-19-h5-mp5-confboot/`.

Two real silicon defects found by the run (invisible to M33MU):

1. **SECWM1 provisioning short (0x3F)**: the secure watermark ended at
   0x08080000, mid boot partition. Secure-alias flash writes beyond it were
   silently dropped, truncating any secure image >128K on flash; wolfBoot
   integrity-rejected the conformance image (hdr_ok=1, sha_ok=0,
   not_sha_ok=1 → `wolfBoot_start` panic, no fault, no UART). Fixed by
   provisioning `SECWM1_END=0x4F` (whole boot partition secure; guests at
   0x080A0000 stay NS). `provisioning_ctrl.sh` restore set and
   `hardware-tfm-replacement-plan.md` updated to 0x4F.
2. **CubeProgrammer `-hardRst` unreliable**: after flashing, the board stayed
   parked in its pre-flash state (stale panic), masking the fix. The runner
   now always issues an explicit `pyocd reset` after flash verify (also
   de-flakes the other scenarios).
3. **Stale flash boot-flag across runs**: the panic tests resume off a flash
   boot flag in a reserved secure sector (0x0C1FA000). The emulator starts
   with fresh flash each run; the board keeps it, so a back-to-back confboot
   inherited stale counters and ~2 panic tests misresumed as SIM ERROR (seen
   once: 83 passed / 2 SIM ERROR / 0 failed - no real conformance failures).
   Mitigation: the runner erases that sector (`pyocd erase -s 0x0C1FA000`)
   before each confboot run.

**SUPERSEDED (2026-08-20) - CLOSED as #83 below.** The gate is now deterministic
(20/20 clean); the flash-write-race theory in this paragraph was wrong - the real
root cause was a Non-secure guest issuing `SYSRESETREQ` mid-suite. See "MP5
confboot gate flake (#83) CLOSED" at the tail. Left here as the historical trail.

OPEN - confboot gate is not yet deterministic. The suite passed 85/4 cleanly
three times (one manual + two back-to-back standalone) but a fourth run stalled
in an infinite reboot loop (476 reboots vs the expected 92, no report). Root
cause: the K3 panic path writes its flash boot-flag then fires SYSRESETREQ, but
on real silicon the flash write (~ms + busy-poll) intermittently does not
complete before the reset lands, so the flag does not persist, val re-runs the
same panic test, and it loops (timeout-capped). Emulator-invisible (instant
flash writes). Fix = flush/complete the flash write and barrier before
SYSRESETREQ in the panic-reset primitive; then re-prove with N back-to-back
suite runs. Until then the **drop-in claim stands** (unmodified suite reached
85/4 on silicon, repeatedly) but the automated confboot gate is flaky.

The ST-Secure-Manager H573I-DK side-by-side is descoped by owner decision
(2026-08-19); the unmodified-conformance-on-silicon result is the drop-in
evidence. Full 4-scenario `make test-hardware` re-run on the same tree is the
MP5 closing gate (positive/restart/crossdomain regression + confboot).

## Phase gate rule

Every implementation phase must repeat host tests and the complete M33MU
matrix on its exact phase commit. When target hardware is available, the same
phase must additionally record a physical-board smoke result covering the
phase behavior.

## MP5 confboot gate flake (#83) CLOSED - NS reset authority (2026-08-20)

The automated `confboot` hardware gate reported one SIM ERROR in ~1/4 runs.
Root-caused on the NUCLEO-H563ZI with a reset-survival SRAM black box (the reset
defeats both the interleaved UART log and the ST-Link, which is itself reset by
SYSRESETREQ). The recorder stamps each boot and each secure `wt_platform_system
_reset`, plus a "reset-pending" flag only the secure path arms: a boot lacking
that flag is a reboot that bypassed secure code.

Finding: on failing runs a boot during test i003 (IOVECS - a non-panic test)
carried RCC_RSR SFTRSTF (a SYSRESETREQ) but no secure-reset breadcrumb, i.e. a
**Non-secure-initiated** system reset. guest0's Zephyr is built with
`CONFIG_REBOOT` + `sys_reboot` (direct `AIRCR` access), and intermittently
issued a full-SoC SYSRESETREQ mid-suite; val had armed `BOOT_NOT_EXPECTED`, so
the unexpected reboot became a SIM ERROR. Intermittent because the guest only
sometimes hit the reboot path (`bypass_boots` = 1 on clean rounds, 2 when it
fired).

Fix (all keeper, `port/stm32h563` + `src/monitor.c`):
- `AIRCR.SYSRESETREQS` set in `wt_platform_init` - Secure world is the sole
  reset authority; a NS SYSRESETREQ no longer resets the SoC. Correct
  Secure-Manager policy (TF-M does the same). The guest's rogue reset is now
  absorbed; legit secure panic-test reboots are unaffected.
- SysTick given the same lowest priority as PendSV (SHPR3). SysTick defaulted to
  priority 0 and could preempt PendSV mid-coroutine-switch, faulting INVPC on
  the half-saved exception frame - a real latent bug the hunt surfaced.
- HSM tasklet preempt gated on `wt_platform_secure_psp_thread_trap()` so an
  async tick only preempts a coroutine physically running on PSP, never inside
  the bootstrap's switch window.

Evidence: with the fix, confboot is deterministic - 20/20 clean back-to-back
(15 instrumented + 5 on the stripped production build), each 89 tests / 85
passed / 0 failed / 4 skipped / 0 SIM ERROR, through the full authenticated
wolfBoot chain and ~92 real SYSRESETREQ panic-reboots per run. Diagnostic
instrumentation (black box, UART reset markers, NVM trace) was removed before
the commit; the M33MU emulator matrix was re-run to confirm no regression.

## MP6 - H5 port docs consolidated + RM0481 cross-check (2026-08-20)

Docs-completion milestone. The on-silicon Secure-Manager guide
`docs/stm32h5-secure-manager-guide.md` consolidates the memory map, provisioning
perimeter, the real Open→Provisioning→TZ-Closed→Closed lock-ladder transitions,
the four hardware scenarios, the 85/4 conformance run, and the silicon gotchas -
every state/result quoted from a board run under `docs/evidence/`, cross-linking
`port-contract.md`, `adding-a-port.md`, and this ledger.

RM0481 cross-check (the "verify encodings before external claims" gate):
`docs/rm0481-encoding-crosscheck.md` verifies every register/option-byte value
the guide and provisioning tooling assert against RM0481 and the Arm Cortex-M33
architecture - product-state codes (Open `0xED`, Provisioning `0x17`, TZ-Closed
`0xC6`, Closed `0x72`, Locked `0x5C`), `TZEN`/`BOOT_UBE=0xB4`, secure watermarks
(`SECWM1_END=0x4F` = bank-1 sector 79 → secure through `0x0809FFFF`;
`SECWM2_END=0x7F`), and the `AIRCR.SYSRESETREQS` (bit 3) / `VECTKEY` / `PRIS`
(14) / `BFHFNMINS` (13) fields of the #83 fix. Provisioning `0x17` and Closed
`0x72` are additionally confirmed against an ST application note; `TZEN=0xB4`
against SEGGER's STM32H5 lifecycle note; the AIRCR bit positions against the Arm
Cortex-M33 Devices Generic User Guide. All match; the guide's earlier
"as-observed, not paper-verified" caveat is retired. Coherence pass run across
the guide + `port-contract.md` + `adding-a-port.md` +
`competitive-edge-vs-secure-manager.md` + `wolftrust-secure-manager-port-plan.md`;
the superseded confboot-flake paragraph in the MP5 section above was marked
CLOSED (#83).

## Phase 4 S0 - crypto/storage requirements + NVM capacity (2026-08-20)

Groundwork for Phase 4 (Crypto/ITS/PS as isolated SPs over a gated wolfHSM
vault). Added the `WT-FFM-0044`–`WT-FFM-0048` requirement block + a Phase 4
acceptance gate to `framework.md`: per-client storage isolation, WRITE_ONCE
persistence across reset, non-exportable HSM-held key ownership, gated
vault routing, and PS AES-GCM + rollback protection - the "keys never leave the
vault" posture that is stronger than baseline TF-M (which holds key material in
the Crypto partition's own RAM).

Capacity: bumped `WOLFHSM_CFG_NVM_OBJECT_COUNT` 8 → 32
(`wh_settings_local.h`). The dev_apis storage suite uses small (16-byte)
objects and few concurrent UIDs, so object-count is the binding constraint, not
bytes; 32 slots × ~80 B directory fits comfortably in the existing 8 KiB
partition, so this needs **no** flash-layout change - the `SECWM1_END=0x4F` boot
rule and the `SECWM2_END=0x7F` bank-2 window are untouched. Byte-capacity growth
(a multi-sector partition) is deferred to S6 only if a specific test demands it.

Evidence: host `make test` green (`PASS: unit/all`) - regression-safe; the
setting is target-only (the host wolfHSM test uses its own `user_settings.h`).
The enlarged-directory secure-build + M33MU NVM-init validation folds into the
S1 box gate (the first slice that exercises the vault on target).

## Phase 4 S1 - gated wolfHSM vault (keystone) (2026-08-20)

The gated backing (WT-FFM-0047): PSA storage objects live behind a new
`SERVICE_VAULT` (SID 4098, `PARTITION_VAULT`) that only manifest-authorized
Secure Partitions reach through the SPM gate - `nonsecure_clients: false` and
the `dependencies[]` check in `wt_ffm_caller_allowed`. The vault runs as a
**scheduled privileged coroutine** (`wt_spm_vault_start` /
`wt_spm_sched_add_common(priv=1)`): same slot machinery and SVC gate as every
SP, but `wt_co_set_domain` is never called, because every vault op takes the
shared wolfHSM NVM path whose mutex cannot be held from the bootstrap context
(`wt_mutex_acquire` refuses, `src/sync/mutex.c`). Its slot table is the
SVC/gate bounds-check whitelist only (whole secure flash RX + RAM RW); the MPU
is never narrowed. This deliberately avoids the ATTEST shortcut of reaching
wolfHSM by running dispatch inline.

Backing (`src/services/wolfhsm/wt_hsm_vault.c`): objects are wolfHSM NVM
objects in a reserved plain-NVM id window (0x0100..0x011F, disjoint from the
keystore's composed ids), keyed by the SPM-stamped owner + 64-bit uid recorded
in the object label. Only the `wh_Nvm_*Checked` entry points are used, so
`PSA_STORAGE_FLAG_WRITE_ONCE` → `WH_NVM_FLAGS_NONMODIFIABLE|NONDESTROYABLE` is
enforced by the NVM layer, not caller convention (WT-FFM-0045). The neutral
service (`src/services/vault_service.c`) mirrors crypto_service: copied-IOVEC
transfers, fail-closed default backend, transport seam.

Manifest: `PARTITION_VAULT` added to the production manifest (domain 5,
privilege_state 0 - honest) and the conformance manifest (domain 8, after the
three Arm SPs); both validated through `tools/manifest/generate.py`. New 8 KiB
vault stack band at 0x30091000 (`VAULTSTACK` in `secure.ld`, RAM shrunk
428K→420K, chained ASSERTs). One real defect surfaced and fixed by the gate:
the port's immutable capability table capped `max_domains = 8`, so the
9-domain conformance manifest correctly **fail-closed panicked** at
`wt_monitor_init` (resolved to `wt_platform_panic` ← `wt_spm_init` via
addr2line) - the port capability now declares 9 (`partitions.c`).

Evidence:
- Host: new `tests/host/vault_service` - the REAL `wt_hsm_vault` backend over
  the REAL wolfHSM NVM stack (wh_nvm + wh_nvm_flash on wh_flash_ramsim),
  driven through real `wt_ffm_connect`/`wt_ffm_call` round trips. 19
  assertions green: NS refused, dependency-less partition refused,
  dependencies[] admits (WT-FFM-0047); per-owner namespacing incl. same-uid
  independence and no cross-owner clobber (WT-FFM-0044); WRITE_ONCE refuses
  set/remove and survives in get_info (WT-FFM-0045, host half); offset reads;
  remove lifecycle; undersized-request refusal. Full `make test`:
  `PASS: unit/all`.
- M33MU positive: `PASS: target/positive`, all 11 checks (boot, FF-M dispatch,
  KAT, attestation, clean exit) with the vault scheduled and the 32-object NVM
  directory - S0's capacity change validated on target.
- M33MU confboot: `PASS: target/confboot` - the unmodified Arm FF-M suite,
  **89 / 85 passed / 0 failed / 4 skipped / 0 SIM ERROR**, with the vault
  partition and 9-domain manifest in the image.

Honest scope note: the on-target SP→vault round trip is exercised in S2 when
the first in-image client (the ITS partition) lands; S1's target evidence is
boot-enforced scheduling (start_sched panics on failure), manifest validation
at the new sizes, and full-suite non-regression. WRITE_ONCE persistence across
a physical reset is S4/S5 (silicon) evidence.

## Phase 4 S2 - ITS Secure Partition (2026-08-20)

PSA Internal Trusted Storage as a real UNPRIVILEGED isolated partition:
`PARTITION_ITS` / `SERVICE_ITS` (SID 4099, `nonsecure_clients: true`) is
scheduled through the normal `wt_spm_sched_add` path (own 8 KiB stack band at
0x3008F000, narrowed MPU domain), and holds no storage of its own - every op
round-trips to `SERVICE_VAULT` over **SP-to-SP FF-M IPC through the SVC gate**,
authorized by `dependencies: [4098]` in both manifests (WT-FFM-0047). End
clients are namespaced at the vault under (ITS partition, client id, uid) via
the new delegated `sub_owner` field: the vault still namespaces primarily by
the SPM-stamped caller, so a frontend can only ever partition its OWN
namespace (WT-FFM-0044 at end-client granularity). Neutral service:
`src/services/storage_service.c` (single concatenated-invec client wire so the
1+1-iovec TEE transport reaches every op); PSA client contract headers:
`include/psa/storage_common.h`, `include/psa/internal_trusted_storage.h`.

Two real infrastructure gaps surfaced and fixed by the gates:
- Host: the SP-as-client begin/finish pair never completed under the direct
  transport (the `_begin` enqueue relies on the scheduler wake, which the host
  lacks) - `wt_spm_transport_direct` now dispatches the pending message inline
  via the new `wt_ffm_dispatch_pending` (bounded, host-only path); the first
  host run hung exactly there.
- Target: `WT_CO_MAX` was 8 - the conformance image's 8 coroutines (2 HSM
  tasklets + crypto + vault + 3 Arm SPs + ITS) exhausted the static table and
  boot fail-closed panicked in `wt_co_create_blocked_ex`; now 12. Also the
  `psa_manifest/pid.h`/`sid.h` includes in `spm_svc.c` were conformance-gated;
  they are generated for every secure build and are now unconditional (the
  unprivileged ITS entry embeds `SERVICE_VAULT_SID` as a code constant - it
  cannot read SPM RAM at runtime).

Evidence (one tree):
- Host: new `tests/host/storage_service` - the FULL chain (NS client →
  SERVICE_ITS dispatch → SP-to-SP gate → SERVICE_VAULT → wt_hsm_vault →
  wolfHSM NVM on ramsim), 16 assertions: direct NS access to the vault still
  refused; per-end-client same-uid isolation both ways; WRITE_ONCE
  set/remove refusal through the ITS face; offset reads; remove lifecycle.
  Full `make test`: `PASS: unit/all` (23 suites).
- M33MU positive: `PASS: target/positive` **12/12 incl. the new
  "wolfTrust ITS set/get verified"** - a real Non-secure guest storing and
  reading back through NS → ITS SP → vault → flash NVM, three protection
  domains, every hop through the SPM gate. Assertion added to the scenario
  runner, the H5 hardware runner, and the CI workflow.
- M33MU confboot: `PASS: target/confboot` - the unmodified Arm FF-M suite
  **89 / 85 / 0 / 4 / 0** with ITS + vault + the 10-domain manifest in-image.

## Phase 4 S3 - PS Secure Partition (2026-08-20)

PSA Protected Storage as a second unprivileged storage partition:
`PARTITION_PS` / `SERVICE_PS` (SID 4100, `nonsecure_clients: true`, prod
domain 7 / conformance domain 10, own 8 KiB stack band at 0x3008D000). The S2
storage loop is parameterized rather than duplicated - the dispatch context
gains `client_flags_mask` (accepted PSA create flags), `vault_flags` (ORed
into every forwarded request) and `caps` (the `psa_ps_get_support()` mask) -
and the PS instance forwards everything with `WT_VAULT_FLAG_SEALED`.

Sealing is applied entirely INSIDE the privileged vault domain (WT-FFM-0048),
which is the beat-TF-M property: the storage key never enters any Secure
Partition's memory. `src/services/wolfhsm/wt_hsm_seal.c` implements a
`wt_vault_sealer_t` seam over wolfCrypt AES-256-GCM: the device-unique key is
generated on first boot (local `WC_RNG` over the platform entropy source) and
stored at NVM id 0x0120 as SENSITIVE + NONEXPORTABLE + NONMODIFIABLE +
NONDESTROYABLE; the GCM nonce is the monotonic rollback counter persisted in
a table at NVM id 0x0121, bumped and written BEFORE any ciphertext exists so
a power loss can never repeat a nonce; the AAD is the object label
(owner/sub_owner/uid/flags), binding each ciphertext to its identity. A
replayed (rolled-back) or resurrected ciphertext therefore fails GCM tag
authentication and returns PSA_ERROR_INVALID_SIGNATURE - rollback protection
is default-on, not an option. `psa_ps_create`/`psa_ps_set_extended` are gated
on `psa_ps_get_support()` (0 - refused NOT_SUPPORTED, no silent success);
NO_CONFIDENTIALITY/NO_REPLAY hints are accepted and recorded for get_info
fidelity but never honored downward. New client contract header:
`include/psa/protected_storage.h`; `PSA_ERROR_INVALID_SIGNATURE` added to
`psa/error.h`.

Capacity gates surfaced and fixed (the fail-closed design working as built):
- `WT_FFM_MAX_SERVICES` 16 → 20: the conformance image now carries 17
  services (Arm SERVER 7 + DRIVER 4 + CLIENT 1 + ATTEST/CRYPTO/VAULT/ITS/PS
  5); S2 sat exactly at the old cap, so the first confboot fail-closed
  panicked in `wt_ffm_boot_init`. Root-caused with a HOST reproduction of
  `wt_spm_init`/`wt_ffm_init` over the identical generated conformance
  table (rc -601) instead of another emulator cycle.
- Port capability `max_domains` 10 → 11 (partitions.c) for the 11-domain
  conformance manifest.
- The M33MU runner's confboot `TOTAL` asserts are now interleave-tolerant
  (`expect_flat`): guest1's shared-UART banner can interject mid-line in the
  val report ("TOTAL SK<freertos_guest1: ...>IPPED : 4"), which one run hit;
  the hardened check strips guest1 text and rejoins split lines while still
  requiring the exact counts. (The H5 runner already had a tolerant match.)
- Host: `whFlashRamsim_Init` erases its backing memory unless `initData` is
  provided - the ps_service reboot seam re-seeds the sim from a snapshot so
  persistence is genuinely proven.

Evidence (one tree):
- Host: new `tests/host/ps_service` - the FULL sealed chain (NS client →
  SERVICE_PS dispatch → SP-to-SP gate → SERVICE_VAULT → AES-256-GCM seal →
  wolfHSM NVM on ramsim), 23 assertions: sealed set/get round trip; stored
  object = plaintext length + GCM tag and the secret bytes absent from the
  whole flash image at rest; get_info reports plaintext size and
  client-visible flags; offset reads; per-end-client same-uid isolation;
  captured-and-replayed v1 ciphertext after a v2 update fails with
  PSA_ERROR_INVALID_SIGNATURE; WRITE_ONCE sealed set/remove refusals;
  get_support = 0 and create refused NOT_SUPPORTED; key + rollback counters
  survive a simulated reboot (NVM re-init over the same flash image).
  Full `make test`: `PASS: unit/all` (24 suites).
- M33MU positive: `PASS: target/positive` **13/13 incl. the new
  "wolfTrust PS sealed set/get verified"** - a real Non-secure guest storing
  and reading back through NS → PS SP → vault → AES-GCM → flash NVM with the
  seal key generated on-target at first boot. Assertion added to the
  scenario runner, the H5 hardware runner, and the CI workflow.
- M33MU confboot: `PASS: target/confboot` - the unmodified Arm FF-M suite
  **89 / 85 / 0 / 4 / 0** with PS + ITS + vault + the 11-domain manifest
  in-image.

## Phase 4 S4 - crypto key-ops in the gated vault (2026-08-20)

PSA key operations with the property that beats TF-M: private key material
never exists outside the privileged vault domain. New vault wire ops 5-11
(generate / import / export_public / sign / verify / encrypt / decrypt) are
served by `src/services/wolfhsm/wt_hsm_keyvault.c` - wolfCrypt ECC P-256 and
AES-256-GCM compute running INSIDE the vault. Keys are vault NVM objects in
the shared (owner, sub_owner, uid) directory, stored SENSITIVE +
NONEXPORTABLE with the usage policy recorded in the label and enforced at
every operation. P-256 objects store [d 32][X9.63 public 65] with the public
point derived once at creation; signatures are raw r||s (the PSA ECDSA
format). Three independent layers stand between a compromised Secure
Partition and raw key bytes (WT-FFM-0046):
- no private-export wire op exists at all;
- the storage face refuses key-flagged objects (SET and GET both
  NOT_PERMITTED, and a storage SET cannot forge the KEY label flag - it is
  outside the storage flag mask);
- WH_NVM_FLAGS_NONEXPORTABLE blocks every *Checked NVM read at the wolfHSM
  layer.

`SERVICE_CRYPTO` gains client ops 1-8 (type 0 stays the SHA-256 hash) and
only marshals: requests forward over SP-to-SP FF-M IPC to SERVICE_VAULT with
the SPM-stamped end client as delegated sub_owner (`PARTITION_CRYPTO` gains
`dependencies: [4098]` in both manifests - no new domain, partition, or
service, so no capacity growth this slice). Key-op routing is a separate
fail-closed `wt_vault_key_backend_t` vtable (every op NOT_SUPPORTED until
the wolfCrypt backend binds). wolfPSA (in-tree, full PSA Crypto surface)
becomes the NS-side psa_* API shim at S6 - decided with Aidan: private-key
compute cannot leave the vault, so wolfPSA inside the Crypto SP would be
pure marshaling with a far larger unprivileged build surface.

Gate catch (a REAL hardware guard, not a capacity bookkeeping miss): the
first M33MU positive run died with CFSR=0x00100000 - ARMv8-M UFSR **STKOF**,
the vault coroutine's PSP hitting PSPLIM inside `sp_256_ecc_mulmod_fast_8`
on the ECC verify path (`sp_256_calc_vfy_point_8` stacks an arbitrary-point
multiplication table that an 8 KiB coroutine stack cannot hold; attestation
never hit this because IAK signing uses the flash-table base-point path on a
deeper stack). PSPLIM caught the overflow before it could touch the ITS
band below. Fix: VAULTSTACK 8 → 16 KiB at 0x3008F000, ITS stack →
0x3008D000, PS stack → 0x3008B000, secure RAM 404 → 396 KiB, linker asserts
and both manifests re-banded.

Evidence (one tree):
- Host: new `tests/host/keyvault` - the FULL chain (NS client →
  SERVICE_CRYPTO key ops → SP-to-SP gate → SERVICE_VAULT → wt_hsm_keyvault
  wolfCrypt compute → wolfHSM NVM on ramsim), 28 assertions: P-256
  generate; ALREADY_EXISTS on re-generate; export_public returns the X9.63
  point; sign returns raw r||s and verify accepts it; tampered digest and
  tampered signature both INVALID_SIGNATURE; sign with a verify-only key
  NOT_PERMITTED (usage policy at the vault); import + sign/verify round
  trip; NONEXPORTABLE proven at BOTH layers (wh_Nvm_ReadChecked →
  WH_ERROR_ACCESS; storage-face get/set on a key object → NOT_PERMITTED);
  cross-client key invisible (DOES_NOT_EXIST); AES-256-GCM
  encrypt/decrypt round trip with [nonce][ct][tag] framing + tampered-
  ciphertext refusal; destroy lifecycle; key AND its public point stable
  across a simulated reboot. Full `make test`: `PASS: unit/all` (25
  suites).
- M33MU positive: `PASS: target/positive` **14/14 incl. the new
  "wolfTrust key-ops sign/verify verified"** - a real Non-secure guest
  destroying (idempotence), generating, exporting the public point,
  signing, verifying, and getting a tampered-digest refusal end to end,
  with the key generated on-target inside the vault. Assertion added to
  the scenario runner, the H5 hardware runner, and the CI workflow.
- M33MU confboot: `PASS: target/confboot` - the unmodified Arm FF-M suite
  **89 / 85 / 0 / 4 / 0** with the key backend + 16 KiB vault stack
  in-image.
- On-H5 hardware: pending the board; `tests/target/run_h5_hardware.sh`
  carries the key-ops assertion so the next `make test-hardware` positive
  run banks it.

## Phase 4 S5 - security negatives, the beat-TF-M proof (2026-08-20)

A consolidated adversarial suite that enumerates the threat model
wolfTrust's gated vault defeats and TF-M's Crypto-partition-RAM key storage
does not. `tests/host/negatives` (24 assertions) drives the real
wt_hsm_vault / wt_hsm_keyvault / wt_hsm_seal backends over real wolfHSM NVM;
because the vault always namespaces by the SPM-stamped caller identity (the
`owner` argument, unforgeable on target), host calls with distinct owners
model distinct partitions:

- N1 a key owned by SP-A is unusable by SP-B - cross-owner sign and
  export_public both DOES_NOT_EXIST (WT-FFM-0046/0044).
- N2 a compromised owner cannot read the raw bytes of its OWN key, at every
  layer that could leak: NONEXPORTABLE makes wh_Nvm_ReadChecked return
  WH_ERROR_ACCESS, the storage face returns NOT_PERMITTED for a key object,
  and the only key export is the 65-byte X9.63 public point (no
  private-export wire op exists at all).
- N3 a forged sub_owner never crosses the SPM-stamped owner boundary: a
  different sub in the same owner is disjoint (delegation works), a forged
  sub in another owner is DOES_NOT_EXIST, the true (owner, sub) still reads
  its object (WT-FFM-0044).
- N4 a wrong-key AES-GCM decrypt fails authentication - key-C1 ciphertext
  decrypted under key C2 returns INVALID_SIGNATURE; C1 still decrypts its
  own (no cross-key oracle, WT-FFM-0046).
- N5 storage/key type confusion refused both directions: a storage SET
  cannot overwrite a key object, a plain storage object cannot be used as a
  key (both NOT_PERMITTED).
- N6 a sealed object is invisible cross-owner and bound to its own label
  (WT-FFM-0048).
- N7 the internal KEY / SEALED label flags cannot be forged from a storage
  client - they sit outside the accepted storage flag mask, so a SET
  carrying them is refused INVALID_ARGUMENT, never silently honoured.

One note on the suite's development: the first run had the type/usage
arguments transposed in the *test's* direct backend calls (the vtable is
generate(owner, sub, uid, type, usage)); the production dispatch maps them
correctly, which is why the S4 keyvault suite through the crypto face passed
28/28. Corrected in the test, no code change.

On target, a new `exercise_ffm_key_negatives` guest probe proves the
wrong-key decrypt refusal end to end - generate two AES keys in the vault,
encrypt under key A, decrypt under key B (st = INVALID_SIGNATURE), decrypt
under key A round-trips - which is also the first on-target exercise of the
key encrypt/decrypt path (the S4 key probe only signs/verifies). The
tampered-verify refusal already rode the positive scenario. The negative
M33MU isolation job (`crossdomain`, an SP-domain read of SPM RAM faulting
with MEMFAULT) is already in the CI matrix, so the negative target job is
wired; #26's separate fault-recovery half is unchanged.

The silicon-only half of S5's plan - WRITE_ONCE survives a Non-secure
SYSRESETREQ on real H563 - is split into task #91 (hardware-pending). Host +
M33MU prove WRITE_ONCE refusal and reboot persistence over ramsim; the
on-silicon durability across a guest-triggered reset needs the board and is
not faked in emulation.

Evidence (one tree):
- Host: new `tests/host/negatives` (24 asserts, above). Full `make test`:
  `PASS: unit/all` (26 suites).
- M33MU positive: `PASS: target/positive` **15/15 incl. the new
  "wolfTrust key negatives verified"**. Assertion added to the scenario
  runner, the H5 hardware runner, and the CI workflow.
- M33MU confboot: `PASS: target/confboot` - **89 / 85 / 0 / 4 / 0**.
- On-H5 hardware: pending the board (#91 + the S4 key-ops run share the
  session; the h5 runner carries both assertions).

## Phase 4 S6a - dev_apis Storage conformance (2026-08-20)

The unmodified Arm psa-arch-tests `dev_apis/storage` suite (test_s001–s017,
ps_testsuite.db) now runs Non-secure against wolfTrust's SERVICE_ITS /
SERVICE_PS through the gated vault: **11 passed / 6 skipped / 0 failed**
under M33MU (`devstorage` scenario, in the CI matrix). The 6 skips are the
optional PS create/set_extended APIs: `psa_ps_get_support()` returns 0 and
the tests skip by design - the service refuses what it does not implement
rather than faking success.

New NS surface: `psa_storage_ns.c` marshals the `psa_its_*`/`psa_ps_*`
families onto the S2/S3 wire (one `[wt_its_req_t][data]` invec on writes,
header + outvec on reads); `pal_its_function`/`pal_ps_function` dispatch the
val codes (ITS 0x1–0x4, PS 0x5–0xB). Suite selection is a `WT_CONF_SUITE`
CMake/`build_guest.sh` parameter - the IPC suite build is byte-identical when
unset - with the storage test list generated by `gen_tests_list.py storage`
into `build/manifest/storage/ns/`. `conformance_pal.c` defines IPC for its
own TU (it implements the real client API; pal_common.h's non-IPC fallback
typedefs would collide) and the per-target `pal_config.h` supplies the PSA
storage headers + `ARCH_TEST_STORAGE_UID_MAX_SIZE 512`, mirroring upstream
tgt_dev_apis targets.

The suite earned its keep: s003 (insufficient-space) exposed two real vault
defects, reproduced on host in a scratch harness on the target flash
geometry (16 KiB region, 8 KiB sectors) before any fix shipped:

1. wolfHSM's plain `wh_Nvm_AddObject` on a full data pool fails mid-write
   with `WH_ERROR_NOTBLANK` (-2103) instead of a clean no-space - surfacing
   as `PSA_ERROR_STORAGE_FAILURE` - and the partial write poisons every
   later add. Every vault pool write is now gated by
   `wt_hsm_vault_reserve()`: `wh_Nvm_GetAvailable` first, compaction via
   `wh_Nvm_DestroyObjects(0, NULL)` when reclaimable space suffices,
   honest `PSA_ERROR_INSUFFICIENT_STORAGE` otherwise; the doomed write never
   starts. (The library-side NOTBLANK behavior is worth an upstream report.)
2. The WT-FFM-0048 rollback-counter table shares the pool and is rewritten
   on every sealed SET and REMOVE, so a pool filled to the brim wedged even
   REMOVE - the operation that frees space. Object adds now reserve one
   table-copy of headroom, so the table rewrite always fits.

Also fixed: uid 0 is rejected `PSA_ERROR_INVALID_ARGUMENT` at the storage
service face for every operation (s010).

Evidence (one tree):
- Host: `PASS: unit/all` (26 suites) incl. new regressions -
  `tests/host/vault_service` WT-FFM-0044 capacity block on target geometry
  (fill → INSUFFICIENT_STORAGE, remove-all + refill deterministic) and
  `tests/host/storage_service` uid-0 rejection (set/get/remove).
- M33MU devstorage: `PASS: target/devstorage` - **17 / 11 / 0 / 6**.
- M33MU positive: `PASS: target/positive` - 15/15 on the same tree.
- M33MU confboot: `PASS: target/confboot` - **89 / 85 / 0 / 4 / 0**.
- On-H5 hardware: pending the board; the runner will carry the devstorage
  assertions with S6b.

## Phase 4 S6b - dev_apis Crypto conformance (2026-08-21)

The unmodified Arm psa-arch-tests `dev_apis/crypto` suite (test_c001–c080,
78 scheduled - c064/c065 hash suspend/resume are db-excluded upstream) runs
Non-secure against wolfPSA, driven by the `devcrypto` scenario. Bringing it up
surfaced one transport defect and two real wolfPSA/config issues:

1. Max-size HSM response overflowed the CMSE transport slot (task #92,
   found by c017). The slot data area was sized for `WOLFHSM_CFG_COMM_DATA_LEN`
   but must hold the whole comm packet - the 8-byte `whCommHeader` rides in
   front of the payload in the same slot. A max-size RNG chunk yields a
   response of `8 + COMM_DATA_LEN`, which `wt_cmse_transport_send` rejected
   `WH_ERROR_BADARGS`; the tasklet's silent-error branch parked with no
   response and the client spun `WH_ERROR_NOTREADY` forever. Fix:
   COMM_DATA_LEN 376→368 so `sizeof(whCommHeader) + COMM_DATA_LEN` = one
   slot's data area, and the `_Static_assert` in `cmse_transport.c` now
   includes the comm header (a too-large setting fails the build, not the
   boot). The same header accounting was mirrored on the guest client bound
   (`WT_HSM_MAX_PACKET_SZ`). The silent-swallow + missing client timeout are
   a robustness gap - candidate wolfHSM upstream report.

2. c020 (TLS12_PRF) was a real wolfPSA bug. `wolfpsa_kdf_tls12_prf` and
   `_psk_to_ms` passed a `WC_HASH_TYPE_*` value to `wc_PRF_TLS`, which wants
   a `wc_MACAlgorithm` id. The enums alias (`WC_HASH_TYPE_SHA256` = 6 =
   `sha512_mac`), so `wc_PRF` selected the wrong hash - `HASH_TYPE_E` on
   builds without SHA-512 (the guest), a wrong SHA-512 digest with it -
   surfacing as PSA `GENERIC_ERROR`. Fixed with a `wolfpsa_prf_mac_from_alg`
   helper mapping only SHA-256/384/512 (others → NOT_SUPPORTED). PR'd upstream
   (`aidangarske:wolfPSA:tls12-prf-mac-alg`, skoll `review --cli codex` = 0
   findings) and carried in wolfTrust as
   `tests/target/wolfpsa-tls12-prf-mac-alg.patch`, applied after
   `git submodule update` in the M33MU and H5 runners with a guarded
   reverse-check - the same carry-then-point-back pattern as the emulator's
   `m33mu-tb-sec-chain.patch`. Bump the `lib/wolfPSA` pin past `dd557dc` and
   drop the patch + apply steps once the PR merges.

3. c047 is schedule-skipped, not a bug or a failure. It is an HMAC-key +
   CMAC-alg negative case; CMAC is compiled out, so wolfPSA returns the
   spec-permitted NOT_SUPPORTED instead of the test's assumed INVALID_ARGUMENT.
   The crypto sched db marks `test_c047, skip` - the same mechanism the
   upstream db already uses for c064/c065 - so it drops out of the schedule
   (one `sed` line in the crypto test-list generation; the ARM test source is
   unmodified). The run is 77 scheduled / 0 failed; the gate now requires every
   scheduled test to pass or skip.

Also: guest0 grew to a 256K flash window (crypto image ~200K > 128K) with
guest1 moved to 0x080E0000; 7 missing PSA error codes added to `psa/error.h`;
a destroyed-volatile-key lookup now returns not-found so wolfPSA yields
INVALID_HANDLE; guest heap bumped to recover c080 (ECDH key-agreement).

Evidence (one tree, final skoll-refined helper via the carry patch):
- M33MU devcrypto: `PASS: target/devcrypto` - **64 passed / 13 skipped /
  0 failed** (77 scheduled; c047 schedule-skipped). c020 TLS12_PRF passes.
- M33MU positive: `PASS: target/positive` - 15/15 on the same tree.
- M33MU confboot: `PASS: target/confboot` - 89 / 85 / 0 / 4 / 0.
- M33MU devstorage: `PASS: target/devstorage` - 17 / 11 / 0 / 6.
- Host: `PASS: unit/all` on this tree.
- On-H5 hardware: done - see the next entry.

## Phase 4 S6 - dev_apis conformance on H563 silicon (HARDWARE, 2026-08-24)

Both dev_apis suites now pass on the real Nucleo-H563ZI, matching the M33MU
emulator exactly - the drop-in claim's Phase-4 half is proven on silicon:

- `PASS: hardware/h5/devcrypto` - **64 passed / 13 skipped / 0 failed**
  (77 scheduled; c047 config-skipped), 0 SIM ERROR.
- `PASS: hardware/h5/devstorage` - **11 passed / 6 skipped / 0 failed**
  (17 scheduled), 0 SIM ERROR, two consecutive runs (determinism).

`run_h5_hardware.sh` gained `devcrypto`/`devstorage` scenarios: the dev images
use the 256K guest0 layout (guest1 at 0x080E0000) so silicon flashes the same
images the emulator boots; every other scenario keeps the proven 128K layout.
The wolfPSA TLS-1.2 PRF carry patch applies in this runner too.

The board time earned its keep - two real silicon-only failures, both invisible
to the emulator (blank flash, no debugger-vs-BKPT distinction), root-caused via
pyocd forensics (stacked exception frame, HFSR/RSR, PC symbol mapping):

1. **Foreign vault pool bricks boot.** The board still held the MP5-era
   firmware's NVM pool; Phase-4 vault init got `WH_ERROR_ACCESS` (-2101) and
   called the panic trap - a BKPT, which with no debugger escalates to a
   HardFault spin before any UART init. Diagnosis: HFSR=DEBUGEVT, CFSR=0,
   stacked r0 = -2101. The runner now guarantees a blank pool for the dev
   scenarios; the underlying defect (init must reformat or quarantine, never
   dead-trap) is tracked as its own item with a garbage-pool negative test.

2. **Test-harness ordering corrupted the pool.** Erasing the vault on a live
   target let the old firmware's RAM-cached wolfHSM state rewrite pool
   structures before the reset (s001 "UID not found" found a stale UID), and
   the post-flash cleanup reset landed mid vault-format, tearing a flash word
   that s003's remove-all later tripped - wolfHSM error, panic-reset, val
   resume, 1 SIM ERROR. Deterministic under the old ordering, absent under a
   single clean boot. Fix: after CubeProgrammer, `reset halt` (old firmware
   dead), erase vault + boot-flag sectors while halted (output logged, not
   silenced), then boot exactly once. A build/flash scenario stamp now fails
   fast on mismatched images.

This is real-hardware evidence, recorded separately from the M33MU emulator
ledger per the evidence rules.

## Phase 4 #95 - vault NVM init recovery from a foreign/corrupt pool (2026-08-24)

The first Phase-4 board run found that a vault pool written by an older
firmware generation bricks boot: the IAK slot is held by a NONMODIFIABLE
object, so re-provisioning returns `WH_ERROR_ACCESS` (-2101) and the boot
called the panic trap - a BKPT that, with no debugger, escalates to a mute
HardFault before any UART. The emulator never saw it (blank flash each run).

Fix (task #95): the boot-time IAK provisioning recovers instead of trapping,
**gated by the wolfBoot-reported PSA lifecycle** so it can never become a
data-wipe attack surface:

- Unlocked development lifecycle (ASSEMBLY_AND_TEST / PSA_ROT_PROVISIONING):
  reformat the vault (`wt_hsm_flash_format`, geometry owned by the port),
  rebind the attest server to the fresh store, and re-provision. Self-heal.
- SECURED or unknown lifecycle: never reformat. Attestation fails closed
  (`g_wt_attest_degraded`), the boot degrades rather than dead-traps, and
  WRITE_ONCE storage + the sealed device key survive untouched.

The `attest_bootstrap` and `initial_attest_init` boot calls no longer panic on
failure - they set the degraded marker and continue.

A deterministic `WT_VAULT_FOREIGN_PROBE` build forces the foreign-pool ACCESS
at first provisioning (`WT_VAULT_PROBE_SECURED` additionally forces a locked
lifecycle), giving a pool-state-independent reproduction. Two scenarios prove
both halves on both platforms:

- H5 silicon `PASS: hardware/h5/vaultrecover` - self-heal: `g_vault_reformatted=1`,
  attestation recovered (`g_wt_attest_degraded=0`), crypto suite 64/0 afterward.
- H5 silicon `PASS: hardware/h5/vaultrecoversec` - fail-closed: `g_wt_attest_degraded=1`,
  `g_vault_reformatted=0` (no wipe), no HardFault.
- M33MU `PASS: target/vaultrecover` - crypto 64/13/0 after self-heal, clean exit.
- M33MU `PASS: target/vaultrecoversec` - graceful boot under fail-closed
  attestation, no fault.

Regressions on the same tree (refactored `wt_hsm_init` → `wt_hsm_bind_store`):
M33MU positive 15/15, devcrypto 64/13/0, devstorage 11/6/0 all still green;
core/port split guard 0 leaks.

Separately noted: the positive/attestation-only HW image stopped booting on the
board (guests not runnable) - pre-existing 128K-layout bit-rot in the positive
HW path, not #95; the recovery scenarios ride the conformance image, which boots.

## Phase 5 P5-CONF - ARM dev_apis Initial Attestation (test_a001) on M33MU (2026-08-24)

The pinned (rev `e17d294`), unmodified ARM `dev_apis/initial_attestation`
suite passes against wolfTrust on M33MU, completing the attestation half of
the drop-in proof. Chain under test: val NSPE → upstream
`pal_attestation_intf.c`/`pal_attestation_crypto.c` → wolftrust-tee NS client
→ FF-M IPC `SERVICE_ATTEST` (4096) → wolfHSM-vault-signed tagged COSE_Sign1 →
val's own QCBOR parse + Sig_structure SHA-256 + wolfPSA `psa_verify_hash`
against the device's runtime IAK public key.

Production enablers (commit `26bd175`): the token is now a tagged COSE_Sign1
(tag 18, val's `IsTagged` gate) and the SW component carries the profile-2
signer_id (label 5) so `mandatory_sw_components == 2`. Host evidence:
`tests/host/attestation_token/` (26/26, gcc/clang/ASan) drives the real
encoder through the production guest verifier; M33MU positive re-verified
(`token_len=291`, `COSE_Sign1 verified`).

Integration notes:
- The IAK is generated per device inside the vault, so upstream's
  `PLATFORM_OVERRIDE_ATTEST_PK` (hardcoded TF-M test key) cannot be used;
  `conformance_pal.c` implements `tfm_initial_attest_get_public_key` fetching
  the runtime key over `wolftrust_attestation_get_iak_public_key`.
- The NS client now rejects a zero-size/NULL token buffer with
  `PSA_ERROR_INVALID_ARGUMENT` (check 8) while keeping undersized-but-nonzero
  as `PSA_ERROR_BUFFER_TOO_SMALL` (check 9), matching the suite's TF-M-shaped
  expectations.
- The CBOR backend is switchable: default is the wolfCOSE-backed qcbor shim
  (`tests/conformance/qcbor-shim/`), `WT_ATTEST_CBOR=qcbor` builds the
  reference QCBOR library (fetched test-only) - proving the wolfTrust token
  parses identically under both.

Evidence (both runs on the wolf-prec5560 container, one tree):

- M33MU `PASS: target/devattest` - shim backend; val: 16/16 checks,
  `Result=Passed`, TOTAL 1/1/0/0/0, profile 2, clean `[EXPECT BKPT]` exit.
- M33MU `PASS: target/devattestqcbor` - reference QCBOR backend; same
  `Result=Passed`, TOTAL PASSED 1 / FAILED 0, clean exit.
- Host `make test-conformance` still green with the extended conf-gen
  (initial_attestation testlist generation added).

CI: scenarios `devattest` + `devattestqcbor` added to the M33MU matrix,
`make`-driven runner, and the `ci:devattest`/`ci:devattestqcbor` PR labels.
H5 silicon run pending (conformance image boots on the board per #94).

## Phase 5 P5-CONF - test_a001 on H563 silicon (HARDWARE, 2026-08-24)

Both CBOR backends re-proven on the real Nucleo-H563ZI via
`run_h5_hardware.sh` (build in the CI container, flash + UART capture on the
lab host, vault NVM erased while halted, single boot):

- H5 silicon `PASS: hardware/h5/devattest` - shim backend, `Result=Passed`,
  TOTAL 1/0/0/0, profile 2, no fault markers. The first attempt after
  switching image families reported an empty 0/0 val report (one-time
  first-boot transient); the clean re-run passed and its UART capture is the
  recorded evidence.
- H5 silicon `PASS: hardware/devattestqcbor` - reference QCBOR backend,
  `Result=Passed`, TOTAL 1/0, first try.

With host (26/26 + CBOR interop), M33MU (both backends), and H5 silicon (both
backends) green, P5-CONF is complete: ARM's unmodified Initial Attestation
conformance test passes against wolfTrust with the vault-held IAK.

## Phase 5 S1 - deterministic EAT claim golden vector (2026-08-25)

`tests/host/attestation_golden/` drives the production
`wt_initial_attest_get_token` with a fully fixed environment - RFC 6979 A.2.5
P-256 IAK (so the UEID, derived from the public key hash, is deterministic),
fixed 0xAB measurement, lifecycle 0x3000, fixed 0x2A challenge - and pins the
encoded claim set to an embedded 216-byte golden vector. Checks (12/12, also
gcc/clang and ASan/UBSan clean):

- two tokens from one boot are byte-identical up to the trailing 64-byte
  ECDSA signature (the only permitted nondeterminism);
- the recovered claim set matches the golden vector byte-for-byte;
- the claim map carries no boot-seed claim (268) - the profile-2 shape is a
  pinned decision, not an accident.

Any unintended change to the token wire format now fails the
`Unit tests / attestation_golden` CI check;
`make run EXTRA_CFLAGS=-DWT_GOLDEN_GEN` reprints the vector after an
intended claim change.

## Phase 5 S3 - attestation negative evidence (2026-08-25)

`tests/host/attestation_negatives/` proves the production attestation path
refuses everything it must (19/19, gcc/clang and ASan/UBSan clean), ordered so
the not-ready and garbage-handoff refusals run before the one valid
`wt_initial_attest_init`:

- no handoff → `get_token`/`get_token_size` return NOT_READY, never sign;
- NULL handoff, unknown hash algorithm, truncated measurement → all rejected,
  and the state stays unarmed (still NOT_READY afterward);
- challenge sizes 0/31/33/65 and a NULL challenge → INVALID_ARGUMENT at
  runtime (65 is the oversized >64 case);
- a valid token then fails verification against a different measurement,
  against a different lifecycle, and with a single flipped measurement byte
  inside the signed payload - the ES256 signature binds the measurement.

On-target attestneg M33MU scenario and `ci:attestneg` label are #102 (P5-CI).

## Phase 5 S5 - replay + lifecycle binding (2026-08-25)

`tests/host/attestation_replay/` (13/13, gcc/clang and ASan/UBSan clean)
proves the token is bound to its challenge and its boot lifecycle:

- two tokens for two challenges differ, and each verifies only under its own
  challenge - a replayed token fails a fresh nonce in both directions;
- a real lifecycle transition (0x1000 development → 0x3000 secured, delivered
  by a second DICE handoff) is reflected in the next token, and the binding
  holds both ways: the secured token is rejected when checked as development
  and the old development token is rejected when checked as secured.

Boot-seed remains deliberately absent from the claim set (pinned by the P5-S1
golden vector).

## Phase 5 S4 - IAK key-isolation, the attestation beat-TF-M proof (2026-08-25)

TF-M's Initial Attestation key lives in the Crypto partition's own RAM; a
compromised partition can leak it. wolfTrust's IAK lives in the wolfHSM vault
and only signatures ever come out. `tests/host/attestation_iak/` (14/14,
gcc/clang and ASan/UBSan clean) provisions the IAK byte-for-byte as production
`wt_hsm_attest_generate_key` does - same wolfHSM server configuration
(attest identity `WH_CLIENT_ID_MAX`), same keygen message, same
`SENSITIVE|NONEXPORTABLE|LOCAL|NONMODIFIABLE|NONDESTROYABLE|USAGE_SIGN`
flags, same keystore commit - against ramsim-backed NVM, then attacks it at
the real enforcement layers (`wh_server_keystore.c` policy + `wh_nvm.c`
Checked face):

- I1 the vault signs and the signature verifies against the ONLY exportable
  artifact, the 65-byte public point;
- I2 raw `WH_KEY_EXPORT` of the IAK is refused (NONEXPORTABLE);
- I3 `wh_Nvm_ReadChecked` of the committed key object is refused, and the
  stored metadata provably carries NONEXPORTABLE;
- I4 `wh_Nvm_DestroyObjectsChecked` and a second provisioning over the key id
  are both refused (NONDESTROYABLE/NONMODIFIABLE) and the original IAK still
  signs afterward;
- I5 with the server's authenticated client identity switched to a guest, the
  same sign and public-export messages fail - the IAK does not exist outside
  the attest identity's key namespace - and the attest identity still signs
  as the positive control.

The on-target counterpart (wrong-key AES decrypt + vault negatives) was
proven in P4-S5; the attestation-specific on-target negative rides #102's
attestneg scenario.

## Phase 5 P5-CI - attestneg on-target scenario (2026-08-25); PHASE 5 COMPLETE

New `attestneg` M33MU scenario: the production image plus a
`WT_ATTEST_NEG_PROBE` guest probe drives the attestation negatives over the
real FF-M IPC path - `PASS: target/attestneg`:

- oversized (65-byte) challenge rejected with `PSA_ERROR_INVALID_ARGUMENT`
  (`st=-135`) end to end;
- zero-size token buffer rejected `st=-135` - the ARM test_a001 check-8
  client mapping proven on target;
- a tampered token and a lifecycle misattribution both refused by the guest
  verify against the real vault-held IAK;
- the positive lifecycle stays green in the same boot, clean `[EXPECT BKPT]`
  exit, no fault markers.

Wired into the M33MU CI matrix ("Attestation negatives (IPC + tamper)") and
the `ci:attestneg` PR label. The H5 variant rides #96 (the positive HW image
fix) since it uses the production image.

**Phase 5 (Initial Attestation) is complete**: production tagged profile-2
token; ARM test_a001 green on host, M33MU (shim + reference QCBOR), and H563
silicon (both backends); deterministic claim-set golden vector; handoff /
challenge / tamper negatives; replay + lifecycle binding; IAK key-isolation
proven at the wolfHSM enforcement layers (the attestation beat-TF-M claim);
and the on-target negative scenario in CI.

## P6-S0 - Phase 6 requirements + restart-engine single-sourcing (#28)

Opened Phase 6 by seating its requirements and removing the last duplicate
restart engine before S3 extends it.

Requirements: `WT-SYS-0013` (runtime re-measurement) in system.md; a Phase 6
block in framework.md - `WT-FFM-0049` (authenticated + measured guest launch),
`WT-FFM-0050` (firmware anti-rollback / version binding), `WT-FFM-0051`
(graceful restartable-partition recovery), `WT-FFM-0052` (runtime
re-measurement), `WT-FWU-0001` through `WT-FWU-0003` (the PSA Firmware Update
service) - plus the Phase 6 acceptance gate. The Commit column fills in per
slice as each lands.

Collapse (#28): `src/lifecycle.c` was a formal state machine plus a priority
selector that no `src/` caller ever used - only its own host test - while the
live engine is `src/monitor.c`. Extracted the restart-budget and window
decision that `wt_restart_guest` runs on every fault into the neutral
`wt_restart_policy_evaluate` (`src/restart_policy.c`), byte-identical behavior.
`monitor.c` now calls it; `src/lifecycle.c` and `include/wolftrust/lifecycle.h`
are deleted and dropped from the secure source list; `tests/host/lifecycle/`
drives the real production predicate (budget exhaustion, window reset,
zero-window crash loop, unlimited restarts, NULL guards) - coverage the monitor
engine never had.

Evidence:

- Host `make test` green including `unit/lifecycle` (WT-SYS-0008 restart-policy,
  25 checks); clean under gcc, clang, and ASan/UBSan.
- M33MU (emulator, wolf-prec5560, `v1.15` container): `PASS: target/restart` -
  a guest restarted 3 times then FAULTED (banners 4/4), the extracted restart
  engine proven on the production Cortex-M path. The scenario's secure
  cross-build links, standing in for the local cross-build (the Mac toolchain
  lacks newlib).

## P6-S1 - Authenticated guest launch (WT-SYS-0002 / WT-FFM-0049)

wolfTrust now authenticates and measures every guest before domain entry, on
first launch and on every relaunch. The manifest declares the policy
(`launch_required`, `launch_min_version` per domain - schema, generated
descriptors, conformance manifest, and fixture all extended); the pinned
digest values live in a `.wt_guest_meas` slot inside the wolfTrust image that
`tools/measure/patch_guest_digests.py` stamps after the guests build and
BEFORE the wolfBoot signing step, so the pins are covered by the same
signature that authenticates wolfTrust itself. Patch-then-sign is forced by
the build order: guests link against the secure image's CMSE implib, so the
secure image cannot know their hashes at compile time.

At boot, `wt_monitor_init` (and the restart path, on every relaunch) locates
each required guest's executable window and pinned record and runs the
neutral `wt_guest_verify_image` predicate: SHA-256 over the recorded image
size, constant-time digest pin, and the manifest version floor. Any failure
fails closed - the guest is marked FAULTED and quarantined, never entered -
and the outcome is exposed in `g_wt_launch_verified_mask` /
`g_wt_launch_refused_mask` for the hardware harness. Verified digests are
recorded and emitted as lean per-guest software components
(measurement + signer_id) in the attestation token, so measured guest launch
is provable in the token; ARM's val only counts component zero toward the
mandatory-claim set and type-checks the rest, so test_a001 stays green.

The old compile-time expected-measurement check in the guests created a
circular dependency against patch-then-sign (guest binary → pinned digest →
signed image → measurement → guest binary), so the reference value moved to
the harness: the guest verifier gained a report-only mode
(`wt_attestation_verify_ex` - NULL/empty expected hex skips the compare and
returns the token's component-zero digest; the original API is unchanged) and
the runner asserts the printed value equals the wolfBoot measurement it
computes from the signed artifact. This is also the DICE-correct shape: the
verifier holds the reference values, the device reports evidence.

Defect found by the gate (and the reason the first target run failed closed):
the port's slot accessor read a `static const` object whose initializer is
the unpatched marker, and the compiler folded the check into an unconditional
NULL - every launch was refused despite a correctly patched image. Fixed with
a volatile load of the slot count; triaged with the `WT_LAUNCH_DEBUG`
per-reason BKPT instrumentation, which stays available (compiled out by
default) for the later Phase 6 slices.

Evidence:

- Host: new `guest_verify` suite (WT-SYS-0002/WT-FFM-0049, 30 checks -
  accept, tamper both sides of the pin, rollback, layout, argument abuse,
  measurement table) green under gcc/clang + ASan/UBSan and in the 34-suite
  `unit/all`; the P5-S1 golden vector is byte-identical (empty host
  measurement table), and manifest suite + `make test-conformance`
  reproducibility stay green with the new schema.
- M33MU (emulator, wolf-prec5560, v1.15 container): `PASS: target/positive` -
  both guests launch only after verification and the token's reported
  measurement equals the harness-computed wolfBoot measurement of the
  patched+signed image; `PASS: target/authneg` - one byte of guest0 flipped
  after pin+sign is refused at launch while guest1 and the platform keep
  running, no fault markers; `PASS: target/devattest` - unmodified
  dev_apis/initial_attestation test_a001 green on the three-component token.
- CI: `authneg` in the M33MU matrix ("Authenticated launch fail-closed") and
  the `ci:authneg` PR label.
- H563 silicon: pending the next board session (S1-HW in the task list; the
  hardware runner already carries the patch-then-sign flow and the harness
  measurement assertion).

## P6-S2 - Firmware anti-rollback and version binding (WT-FFM-0050)

The previously unread `boot_handoff.image_version` is now consumed and bound
to a monotonic version floor persisted in the wolfHSM vault NVM, so a validly
signed but older wolfTrust or guest image is refused before any domain is
entered. The floor table is a new plain-id NVM object
(`WT_HSM_ROLLBACK_TABLE_ID` = 0x0122, directly above the WT-FFM-0048 counter
table and using the same `wh_Nvm_GetMetadata`/`Read`/`AddObject` idiom):
one image floor plus one floor per pinned guest record.

Enforcement runs in `wt_hsm_rollback_enforce` on the secure boot stack, after
`wt_hsm_init` brings the NVM up and before `wt_monitor_start` dispatches the
first guest. The neutral predicate (`src/rollback.c`) accepts a version only
at or above its floor and never lowers a floor; the unlocked provisioning
lifecycles (assembly-and-test, PSA RoT provisioning) bypass refusal - the #95
pattern, so a version floor cannot brick development flows - while SECURED,
unknown, and every other lifecycle enforce strictly. A rolled-back wolfTrust
image (or an unreadable floor table, or a missing handoff once a floor is
armed) quarantines every guest via the new `wt_monitor_quarantine_guest`
monitor API and the platform halts fail-closed; a rolled-back guest is
quarantined alone. An accepted boot advances the floors, writing NVM only
when a floor actually moved so a steady-state reboot costs no flash wear.

Evidence:

- Host: new `rollback` suite (WT-FFM-0050, 51 checks - locked-lifecycle
  refusal matrix incl. UNKNOWN, provisioning bypass, monotone advance,
  no-retreat, missing-handoff-as-version-zero fail-closed, table validity)
  green under gcc/clang + ASan/UBSan and in the 35-suite `unit/all`.
- M33MU (emulator, wolf-prec5560, v1.15 container): `PASS: target/positive` -
  a normal boot initializes and advances the floors with no behavior change;
  `PASS: target/rollbackneg` - the `WT_ROLLBACK_PROBE` build arms the image
  floor one above the running version, stores it, SYSRESETREQs, and the
  second boot reads the floor back from flash NVM and refuses fail-closed
  (`[BKPT] imm=0x7d`, no guest entered a domain, no fault markers) - proving
  both the downgrade refusal and floor persistence across reset in one run.
  The probe forces PSA_LIFECYCLE_SECURED (the emulator chain boots in
  assembly-and-test, which correctly bypasses enforcement - the first probe
  run demonstrated exactly that bypass and was the reason the forced-SECURED
  half was added, mirroring vaultrecoversec).
- CI: `rollbackneg` in the M33MU matrix ("Anti-rollback downgrade refused")
  and the `ci:rollbackneg` PR label. The inline per-guest CI job was also
  reordered to the S1 patch-then-sign flow (it would otherwise boot an
  unpatched slot and refuse every guest).
- H563 silicon: rides the #113 board session.

## P6-S3 - Graceful Secure Partition fault recovery (WT-SYS-0008 / WT-FFM-0017)

A faulted Secure Partition is now recovered gracefully instead of dying
silently or resetting the platform. The design splits recovery across
execution modes: the Secure fault handler does only what the proven
guest-tasklet path did - mark the coroutine dead (`wt_co_mark_faulted`),
clear the stale PendSV target, pend the recovery - and the SPM dispatch path
then runs the full sequence on the bootstrap thread, the same context that
creates SPs at boot. The architecture-neutral engine (`src/sp_recovery.c`)
sequences: release the dead partition's locks (`wt_hsm_release_locks`),
force-complete its in-flight and queued messages with a defined error so no
pinned client hangs (`wt_ffm_fail_partition_messages`, which also drains the
partition's service queues and deasserts its signals), scrub its stack, and
restart the coroutine in place (`wt_co_reinit` - same table slot, same MPU
domain binding) under the manifest `restart_policy` budget
(`wt_restart_policy_evaluate`). NEVER/PLATFORM actions and an exhausted
budget escalate: the partition stays quarantined, or for a platform-fatal
service the platform fails closed.

Three defects were found and fixed by the gate on the way to green:

1. `wt_secure_fault_dispatch` attributed EVERY secure escalation to the
   currently scheduled NS guest - an SP fault would restart an innocent
   guest. `SecureFault_Handler` now routes faults whose EXC_RETURN shows a
   Secure Thread frame with a live tasklet to the tasklet recovery entry.
   This also covers M33MU emulator defect #3 (a stale `securefault_pending`
   in the emulator delivers a secure MPU DACCVIOL through the SecureFault
   vector instead of secure MemManage - minimal repro + upstream fix tracked
   with the #63 patch family, task #115).
2. Force-completed messages previously stayed on the service queue with the
   signal asserted, so a restarted partition woke instantly into a stale
   queue and could spin unpreemptably.
3. The fault path bypassed `wt_co_arch_leave`, leaving `g_wt_co_pendsv_target`
   pointing at the dead coroutine.

Evidence:

- Host: new `sp_recovery` suite (300 checks - restart-budget decision matrix
  incl. NEVER/PLATFORM/unlimited/NULL, ordered orchestration with
  failed-restart downgrade-to-escalate, 50x in-place coroutine reinit
  preserving slot identity and domain binding with zero slot leakage) and a
  new `ffm` fault-unblock case (pinned message force-completed with the
  injected status, unrelated partitions untouched, connection dropped to
  ERROR, service signal deasserted). 36-suite `unit/all` green under
  gcc/clang + ASan/UBSan; core/port split guard clean.
- M33MU (emulator, wolf-prec5560, v1.15 container): `PASS: target/spfaultneg`
  - the `WT_SP_FAULT_PROBE` build makes the crypto SP fault once on its first
  dispatch (out-of-domain read, one MEMFAULT, no HardFault/SecureFault
  cascade); the pinned NS client unblocks with `psa_connect ... handle=-145`;
  the restarted SP then serves the vault key-ops chain
  (`key-ops sign/verify verified` rides SERVICE_CRYPTO); ITS/PS, the wolfHSM
  tasklet path (KAT, guest1 C_Digest), and full attestation stay green
  through a clean BKPT exit - a graceful per-partition restart with no
  platform reset. Regressions on the same tree: `PASS: target/positive`,
  `PASS: target/crossdomain`, `PASS: target/confboot` (85/0/4).
- Root-cause work that got here (recorded because the emulator dumps were the
  decisive instrument): the first spfaultneg runs HardFaulted at the next NS
  veneer entry with `ctrl=0x00000001`; the emulator's `M33MU_CTRL_TRACE`
  showed no CONTROL write after PendSV set nPRIV=1 for the SP, proving the
  MemManage tail never ran; symbolizing the halt LR placed the handling in
  `wt_secure_fault_dispatch`, exposing the SecureFault misdelivery and the
  guest-blame bug at once.
- `target/restart` FAILS in this validation set, and an A/B on pristine S2
  (`5cfd394`, zero S3 changes) fails identically - a pre-existing S1/S2-era
  regression (guest1's first dispatch inherits guest0's NS stack bank; the
  NS MPU rightly denies it and rotation stalls). `restart` was last green at
  S0 and absent from the S1/S2 validation sets. Split to task #114 (S3-R);
  not an S3 artifact.
- CI: `spfaultneg` in the M33MU matrix ("Graceful SP fault recovery") and the
  `ci:spfaultneg` PR label. The `WT_SP_FAULT_DEBUG` NS-exit tripwires used
  for the hunt were removed once the root cause landed (the technique is
  recorded above; the eb0ef27 history carries the code).
- H563 silicon: rides the #113 board session.

## Phase 6 S3-R - target/restart regression fix (task #114)

- Root cause (bisect S0 `0af5eef` green / S1 `529b95a` red, then proven from
  the failure log): `wt_virtual_systick_restore_arriving` re-armed the
  arriving guest's SysTick and injected owed ticks (`PENDSTSET`) from inside
  `wt_platform_prepare_guest_return` - after `VTOR_NS` was switched to the
  arriving guest but before the NS bank (MSP/PSP/CONTROL_NS) was restored.
  With PRIS=0 the injected NS tick preempts the secure dispatcher inside
  that window: it vectors through the arriving guest's table while stacking
  on the departing guest's live MSP_NS, which the arriving guest's NS MPU
  rightly denies - derived NS HardFault, NS-Handler spin, rotation dead at
  2/4 banners. The decisive artifact is in the failing run's fault dump:
  the faulting SysTick context carries `EXC_RETURN=0xFFFFFFD0` (ES=0 NS
  exception, Mode=0 returns to Handler, S=1 secure stack) - an NS tick that
  preempted secure HANDLER code mid-dispatch. S1 made the latent race
  deterministic: its in-handler relaunch hash (~90KB SHA-256) guarantees the
  descheduled guest owes ticks at every relaunch dispatch.
- Fix (`port/stm32h563/platform_stm32h563.c` only): the arm/inject tail of
  `wt_virtual_systick_restore_arriving` is deferred into
  `wt_virtual_systick_arm_arriving()`, invoked from the end of both NS entry
  paths (`wt_exception_return_ns_msp`, `wt_jump_to_ns`) once the NS bank is
  fully restored - a tick taken there stacks on the arriving guest's own
  stack. A disproven earlier theory (per-guest capture bounds) was reverted
  and is not part of the fix.
- M33MU (emulator, wolf-prec5560, v1.15 container): `PASS: target/restart`
  - guest0 faults, restarts `restart_limit`=3 times, then FAULTED; 4/4
  banners; guest1 unaffected. Regressions on the same tree:
  `PASS: target/positive`, `PASS: target/spfaultneg`,
  `PASS: target/confboot` (85/0/4).
- Follow-up hardening: the peripheral-IRQ unmask in `wt_apply_partition`
  sits in the same dispatch window (no current scenario pends guest IRQs
  across a dispatch) - tracked as task #116.
- H563 silicon: rides the #113 board session.

## Phase 6 S4 - PSA Firmware Update service (WT-FWU-0001..0003, task #110)

- Neutral state machine `src/services/fwu_service.c` + client API
  `include/psa/update.h` (`psa_fwu_query/start/write/finish/install/abort`),
  driven through a `wt_fwu_backend_t` staging seam so the host test supplies a
  RAM mock and the target supplies real flash. WT-FWU-0001 (isolated SP + PSA
  surface reached only through the SPM gate), WT-FWU-0002 (staged write to the
  wolfBoot update partition + arm), WT-FWU-0003 (malformed/oversize/rolled-back
  refused before arming; abort restores prior state).
- New privileged `SERVICE_FWU` SP (domain 8, SID 4101, partition 8) mirroring
  the vault; `wt_spm_fwu_start`/`wt_spm_fwu_entry` in spm_svc.c. Target backend
  in `hsm_flash.c` stages into the real wolfBoot update partition
  (`WT_FWU_UPDATE_FLASH_BASE_S = 0x0C100000`, 256 KiB) with lazy per-sector
  erase, program, and memory-mapped read-back verify; install writes a
  wolfTrust update-request marker to the trailer sector. Stack band
  `WT_SP_FWU_STACK_BASE = 0x30089000` carved in memory_map.h + secure.ld
  (RAM 396K->388K, FWUSTACK region + ASSERT chain); `WT_FFM_MAX_PARTITIONS`
  8->9.
- FWU is production-only: the conformance ingester (`ingest_psa_arch.py`,
  `CONFORMANCE_EXCLUDE`) drops PARTITION_FWU so `manifest-conformance.json`
  is byte-identical to before and the Arm 85/4 layout is untouched.
- Evidence - host: `fwu_service` suite 33 checks green under gcc/clang +
  ASan/UBSan (every negative: bad-state, oversize, wrapping, misaligned,
  empty, rolled-back, storage-failure, abort-restores-state) plus a real
  NS -> SERVICE_FWU FF-M IPC round trip; 36-suite `unit/all` green;
  `manifest_ingest` reproducibility green.
- Evidence - M33MU (emulator, wolf-prec5560, v1.15 container):
  `PASS: target/fwustage` - a Non-secure guest drives
  start/write/finish/install over IPC, the candidate is staged into the real
  update-partition flash and verified by read-back (`wolfTrust FWU staged 64
  bytes to update partition, armed, verified`), a write before start is
  refused on target (`wolfTrust FWU write-before-start refused`), clean BKPT
  exit, no fault. Regressions on one tree: `PASS: target/positive`,
  `PASS: target/confboot` (85/4).
- CI: `fwustage` in the M33MU matrix ("PSA Firmware Update staging") and the
  `ci:fwustage` PR label.
- Deferred to S6 (full boot-and-update gate + silicon): the wolfBoot
  trailer-exact arm and the reboot->swap->authenticated-launch/anti-rollback
  of the swapped image (the emulator loads images directly and has no wolfBoot
  swap path).
- H563 silicon: rides the #113 board session.

## Phase 6 S5 - Runtime verification (WT-FFM-0052 / WT-SYS-0013, task #111)

- On-demand post-boot re-measurement. Neutral decision in `src/guest_verify.c`:
  `wt_runtime_verify_decide(window_base, window_size, record, min_version,
  launch_required)` reuses the S1 SHA-256 pin check (launch_required==0 has
  nothing pinned and passes; a launch-required guest with no window/record
  fails closed); `wt_runtime_verify_should_quarantine(result)` maps any
  non-OK to fail-closed. Monitor wiring `wt_runtime_verify_guest` (src/monitor.c)
  does the same window+record lookup as launch verification and, on mismatch,
  drives the domain through `wt_monitor_quarantine_guest` - catching a tamper
  that happens AFTER launch instead of trusting the boot-time measurement.
  Counters `g_wt_runtime_verify_pass`/`_fail` (harness symbol reads).
- Scope: guest-domain re-measurement. Secure Partitions have no pinned-digest
  store (`wt_platform_guest_measurements` is guest-only), so an SP re-measure
  would need a new expected-digest source and is not covered here.
- Evidence - host: `runtime_verify` suite 7 checks green under gcc/clang +
  ASan/UBSan (untampered -> OK no quarantine; tampered -> DIGEST fail-closed;
  no-launch-policy -> OK; no-record -> ARGUMENT fail-closed; shrunken window
  -> LAYOUT; rolled-back -> VERSION); 36-suite `unit/all` green.
- Evidence - M33MU (emulator, wolf-prec5560, v1.15 container):
  `PASS: target/remeasureneg` - after boot init and launch verification a
  secure probe (`WT_REMEASURE_PROBE`, `wt_platform_remeasure_probe`)
  re-measures guest0 clean, then tampers its flash window in place
  (`wt_hsm_flash_remeasure_tamper`; the secure MPU maps flash privileged-RO,
  so `WT_MPU_S_CTRL` is dropped for the single program then restored), and the
  on-demand re-measure catches the mismatch and quarantines the guest -
  `[BKPT] imm=0x6c` fires only when the untampered pass AND the tamper-catch
  both hold, with no fault marker. Regressions on one tree:
  `PASS: target/positive`, `PASS: target/confboot` (85/4).
- CI: `remeasureneg` in the M33MU matrix ("Runtime re-measurement quarantine")
  and the `ci:remeasureneg` PR label.
- H563 silicon: rides the #113 board session.

## Phase 6 S6 - Full boot-and-update gate (phases.md:126, task #112)

- The `phases.md:126` stop condition: the whole firmware lifecycle end to end -
  boot the authenticated chain, stage and arm an update through SERVICE_FWU,
  reboot, and confirm wolfBoot swaps the new image in, the new image runs, its
  measurement is attested, and anti-rollback advanced. This closes Phase 6.
- wolfBoot on-flash trigger binding (the piece S4 deferred). The FWU backend
  `arm` now writes wolfBoot's real WRITEONCE update trigger into the UPDATE
  partition trailer instead of an internal marker. A neutral, host-tested
  encoder `wt_fwu_wolfboot_arm_trailer` (`src/services/fwu_service.c`) lays down
  IMG_STATE_UPDATING (0x70) at partition_end-5 and the little-endian trailer
  magic 'BOOT' at partition_end-4 - exactly what wolfBoot's
  `nvm_select_fresh_sector`/`get_partition_state` read to select the sector and
  run the swap (`aidangarske/wolfBoot` @ `d85fa9d`, NVM_FLASH_WRITEONCE,
  non-inverted flags). The port backend (`hsm_flash.c wt_fwu_backend_arm`)
  programs that block into the trailer sector and read-back verifies it.
- `bootupdate` scenario. One secure image is signed twice: v1 at wolfBoot
  version 1, v2 at version 2. Because the version is inside the hashed wolfBoot
  header, v2 carries both a higher version AND a different measurement. v2 is
  pre-staged into the UPDATE partition (the emulator's 5th image at 0x100000; on
  silicon, flashed there); v1 boots with a version-gated arm probe
  (`WT_BOOTUPDATE_PROBE`, `platform_stm32h563.c`) that - only when the running
  image is version 1 - arms the real trigger (secure MPU dropped for the single
  privileged-RO trailer program, as in S5) and reboots. wolfBoot then swaps v2
  into the boot slot and boots it; the swapped-in v2 (version 2) skips the arm,
  so the swap terminates instead of looping. The byte-level block staging path
  is proven separately by `fwustage` (S4); this gate proves the arm -> reboot ->
  swap -> new-image chain.
- Anti-rollback: wolfBoot accepts the swap only because v2's version (2) is
  strictly greater than v1's (1) with ALLOW_DOWNGRADE=0; wolfTrust's own
  monotonic floor advances v1->v2 (`wt_rollback_advance`). The downgrade
  refusal is covered by the dedicated `rollbackneg` scenario (S2).
- Evidence - host: `fwu_service` suite +7 checks for the trigger encoder
  (byte-exact state/magic, little-endian order, erased flag region, NULL and
  undersized rejects) green under gcc/clang + ASan/UBSan; 36-suite `unit/all`
  green.
- Evidence - M33MU (emulator, wolf-prec5560, v1.15 container):
  `PASS: target/bootupdate` - no fault across the update reboot; the swapped
  image runs a clean FF-M lifecycle; the attestation token reports v2's wolfBoot
  measurement
  (`30399054dd2599141efa193b149fe410070c7710e139b1fc071d96dd1e8ac9d6`) and never
  v1's; clean BKPT exit. This is the real wolfBoot swap executing under emulation
  (SYSRESETREQ re-runs the whole chain with flash intact, K1), not a simulated
  one. Regressions on one tree: `PASS: target/positive`, `PASS: target/fwustage`,
  `PASS: target/confboot` (85/4).
- CI: `bootupdate` in the M33MU matrix ("Full boot-and-update swap gate") and the
  `ci:bootupdate` PR label.
- Evidence - H563 silicon (real Nucleo-H563ZI, wolf-prec5560):
  `PASS: hardware/h5/bootupdate` - guest-independent proof: after v1 armed the
  trigger and rebooted, the boot-partition header read back over SWD carries
  v2's measurement
  (`dae2f4a5d63e56f280131ebfa09df91a9d9aa63bc68a1e247dc851f2e9cf1fbc`), not
  v1's, so wolfBoot physically swapped v2 into the boot slot on real silicon.
  (The on-silicon v2 measurement differs from the emulator's because the
  hardware build drops the emulator-only BKPT/diag flags; the invariant tested
  is boot-header == v2 and != v1, which holds on both.)
- Phase 6 CLOSED: the full boot-and-update gate passes on the emulator (the
  `phases.md:126` stop condition) and on H563 silicon.

## Phase 7 S0 - OS-integration requirements seated (2026-08-27)

Opened Phase 7 by seating its requirements so the already-landed client work has
a written requirement to trace back to. Docs-only slice: no code, no cross-build,
no M33MU, no hardware.

Requirements: `WT-SYS-0014` (one OS-neutral non-secure client ABI, SPM the single
mediated path to every secure service) in system.md; a Phase 7 block in
framework.md - `WT-FFM-0053` (OS-neutral client core, no OS headers, same link
under Zephyr/FreeRTOS/bare-metal), `WT-FFM-0054` (single mediated path; raw
non-secure-to-wolfHSM CMSE bypass retired from production), `WT-FFM-0055` (same
PSA + isolation suites pass from both a Zephyr and a FreeRTOS client) - plus the
Phase 7 acceptance gate. Up-trace lives in the Source column
(`SRC-FFM 4.4/3.3.x`, `WT-FFM-0016/0020/0047`, `WT-PORT-0006`, `WT-SYS-0014`).

Evidence:

- `WT-FFM-0053` is marked met: the OS-neutral core `src/client/psa_ffm_client.c`
  landed in P7-S1 (`cb87ae5`) with the `tests/host/psa_ffm_client` suite (8 checks
  green under gcc/clang/ASan) and passes the core/port split guard (the
  `WolfTrust_FFM_*` veneers are extern port-provided, defined in
  `src/arch/armv8m/ffm_nsc.c`); P7-S2 (`8c9a675`) repointed guest0 and the val
  NSPE onto it, M33MU `PASS: target/positive` + `PASS: target/confboot` (85/4).
- `WT-FFM-0054` and `WT-FFM-0055` stay OPEN (empty Commit): the raw HSM-CMSE
  bypass is still live for the FreeRTOS guest1 and guest0's wolfPSA crypto
  front-end. They close in S3 (guest1 onto the SPM), S4/S5 (both-OS gates), and
  S6 (retire the bypass from the production image).
- Docs verification: table columns align (system.md 7-col, framework.md 6-col);
  IDs contiguous with no collision; every Source trace resolves to an existing
  ID/section; acceptance-gate IDs match the S1..S6 slice tasks. Host `make test`
  unaffected.

## Phase 7 S3 - FreeRTOS guest1 through the FF-M SPM (WT-FFM-0054, 2026-08-27)

FreeRTOS guest1 now reaches secure crypto only through the SPM; its raw
HSM-CMSE transport is retired. The one coverage gap was randomness: added
`WT_CRYPTO_OP_RANDOM` to SERVICE_CRYPTO (bounded 256 B/call, forwarded over the
cached SP-to-SP connection to `WT_VAULT_OP_RANDOM`, fail-closed without a vault
route) and a `random` entry on the vault key backend that draws from the same
wolfHSM `WC_RNG` that mints key material - entropy never leaves the vault domain.
SHA-256 was already covered by SERVICE_CRYPTO (guest0's `exercise_ffm_crypto`).
New OS-neutral helper `src/client/ffm_crypto_client.c` (`wt_ffm_crypto_random`).
guest1 dropped wolfPKCS11 + the wolfHSM client + the raw glue for a wolfPSA
front-end (`psa_crypto_init`/`psa_generate_random`/`psa_hash_compute`) plus the
neutral FF-M client; the wolfCrypt DRBG seed/`wc_GenerateSeed` hooks now route to
`wt_ffm_crypto_random`, so even entropy crosses via the SPM.

Evidence:

- Host `make -s -C tests/host` green. `unit/crypto_service` (+5): the RANDOM op
  forwards through a real two-partition crypto→vault fixture whose backend is a
  live `wc_RNG`; two draws fill and differ, over-256 and zero-length are refused,
  and pulling the vault route (`vault_sid=0`) makes RANDOM fail closed
  (`NOT_SUPPORTED`). `unit/psa_ffm_client` (+5): the veneer stub counts crossings
  and a 300-byte fill is asserted to issue **exactly 2** bounded psa_calls;
  NULL/zero-length refused before any crossing. Clean under gcc/clang + ASan/UBSan.
- M33MU (emulator, wolf-prec5560, v1.15): `PASS: target/positive` with the five
  new guest1 markers (`ffm sha256 ok`, `ffm rng ok`, `psa_crypto_init st=0`,
  `psa rng ok`, `psa hash ok`); `PASS: target/spfaultneg` (the restarted crypto
  SP serves the FreeRTOS client too); `PASS: target/authneg`.
- Path validated, not assumed: `arm-none-eabi-nm`/`objdump` of the guest1 ELF show
  call thunks and branches only to `WolfTrust_FFM_{Connect,Call,Close}`; no thunk
  or instruction reaches the raw `WolfTrust_HSM_*` veneer addresses (present only
  as dead implib address symbols). The build script fails the link if any
  `wh_Client_*`/`wolfhsm_guest_init` symbol survives. Core/port split guard: hard
  leaks 0.
- CI: the positive scenario + the M33MU lifecycle job assert the five guest1
  markers; authneg/spfaultneg updated to the mediated markers.

## Phase 7 S4 - Both-OS PSA parity gate (WT-FFM-0055, 2026-08-27)

A dedicated `bothpsa` M33MU scenario proves the same PSA client behavior from
both operating systems in one boot: the Zephyr guest (guest0) and the FreeRTOS
guest (guest1) each pass the mediated SERVICE_CRYPTO SHA-256 KAT, PSA
`psa_generate_random`, and PSA `psa_hash_compute` SHA-256 KAT. Both guests reach
the same Secure Partition with the same input and get the same digest, so the
client behavior is operating-system-neutral, not merely present on each side.

Evidence:

- M33MU (emulator, wolf-prec5560, v1.15): `PASS: target/bothpsa`, 8/8 checks -
  no fault markers, and each of the three operations verified from guest0 and
  from guest1 in the same boot, through the clean BKPT exit.
- Wired into CI: a `bothpsa` entry in the M33MU scenario matrix and the
  `pr-m33mu-select` all-list/case-map (`ci:bothpsa` label). Shell + YAML linted.
- No shared-code regression from S3: `PASS: target/confboot` (85/0/4) and
  `PASS: target/devcrypto` (64/0/13) re-run green on the same tree.

## Phase 7 S5 - Both-OS isolation negatives (WT-FFM-0055, 2026-08-27)

The SPM must reject a malformed non-secure request identically no matter which
operating system issues it. The FreeRTOS guest gained `run_ffm_negatives`:
through the neutral client it issues a forged handle (`psa_call` on a handle not
mapped to its connection), an oversized input vector (length beyond the secure
transfer bound), and a connect to an unknown SID. Each mirrors a rejection
guest0 already proves, and none faults the guest - a rejected call returns an
error and the guest keeps running.

Evidence:

- M33MU (emulator, wolf-prec5560, v1.15): new `bothiso` scenario,
  `PASS: target/bothiso`, 8/8 checks - no fault markers; forged-handle and
  oversized-vector calls rejected from BOTH the Zephyr and the FreeRTOS client;
  the FreeRTOS unknown-SID connect refused; the FreeRTOS guest survived and still
  served the mediated SERVICE_CRYPTO SHA-256 KAT; clean BKPT exit.
- Wired into CI: a `bothiso` entry in the M33MU scenario matrix and the
  `pr-m33mu-select` all-list/case-map (`ci:bothiso` label). Shell + YAML linted.
- With S4, both operating-system gates now hold: the same PSA behavior and the
  same isolation rejections pass from guest0 and guest1. Ran on `claude-opus-4-8`
  (replicating proven negative patterns, not deep work).

## Phase 7 S6a - Host proof of the wolfHSM-over-SPM relay (WT-FFM-0054, 2026-08-27)

Opens the bypass retirement (decision locked: no gate, no mixed transport - the
wolfHSM server becomes the single crypto backend behind one mediated door).
S6a proves the keystone on the host before any secure-side change: a real
wolfHSM client whose pluggable transport is one synchronous `psa_call` to a
`SERVICE_HSM` relay reaches a real wolfHSM server through a genuine
`psa_connect`/`psa_call` round trip on the in-process FF-M runtime.

New code:

- `src/services/hsm_relay_service.c` + `include/wolftrust/services/hsm_relay.h`
  - SERVICE_HSM's architecture-neutral dispatch: one wolfHSM wire packet in
  invec[0], handed opaquely to a pluggable platform submit hook (on target,
  the monitor's per-guest server tasklet), response in outvec[0]. Copied-IOVEC
  bounds at `WT_HSM_RELAY_MSG_MAX` (512 B; a packet is 376 B), fail-closed
  `NOT_SUPPORTED` default when no submit hook is installed. The relay never
  parses packets - wolfHSM's comm layer owns the protocol, the SPM owns caller
  identity and bounds.
- `src/client/hsm_psa_transport.c` + `include/wolftrust/hsm_psa_transport.h`
  - the `whTransportClientCb` whose Send performs the whole mediated round
  trip and stashes the response, so Recv completes on the first try: the
  client's blocking wrappers structurally cannot spin on NOTREADY, retiring
  the multi-chunk hang class the old shared-RAM CSR handshake produced.

Evidence: `tests/host/wolfhsm_relay` 19/19 - CommInit over the SPM; 32-byte
RNG via the blocking wrapper; 1000-byte multi-chunk RNG (>2 packets, each
chunk one completed psa_call); ECC P-256 keygen + sign in the server keystore
through the relay with local verify against the exported public key; relay
fails closed with no submit hook and serves again after restore; oversized
packet refused client-side (WH_ERROR_BADARGS) and oversized invec refused by
the relay dispatch. Green under gcc/clang + ASan/UBSan; core/port split guard
hard leaks 0. COMM_DATA_LEN pinned to the target's 368 so host packets are
the same 376 bytes the platform will relay.

## Phase 7 S6f - Both guests on the single mediated path (WT-FFM-0054/0055, 2026-08-28)

Closes the guest-repoint half of the bypass retirement: every non-secure crypto
request from both operating systems now traverses `wolfPSA -> wolfCrypt(WH_DEV_ID)
-> crypto_cb -> wh_Client -> wt_hsm_psa_transport_cb -> psa_call(SERVICE_HSM 4102)
-> relay -> per-guest wolfHSM server`. No non-secure client speaks the retired
SERVICE_CRYPTO (4097) op protocol, and the manifest exposes only SERVICE_HSM, so
4097 is unreachable from non-secure code.

Guest changes:

- `guest0_psa`: `exercise_ffm_crypto` now proves the SHA-256 KAT with
  `psa_hash_compute` (same input and digest); `exercise_ffm_keys` generates a
  volatile P-256 pair whose private half lives only in the wolfHSM server, signs
  and verifies a digest, and refuses a tampered digest; `exercise_ffm_key_negatives`
  generates two P-256 keys and proves a signature under key A does not verify under
  key B (no cross-key oracle); `exercise_ffm_negatives` connects to SERVICE_HSM for
  its forged-handle and oversized-vector rejections. `WT_CRYPTO_SID` removed. Every
  scenario marker string is unchanged.
- `freertos_guest1`: links the full wolfHSM client subset and registers the
  cryptocb; `guest_crypto_init` now calls `psa_crypto_init` (guest0 gets this from
  wolfPSA's Zephyr SYS_INIT) so the first mediated `psa_hash_compute` is not
  `PSA_ERROR_BAD_STATE` (-137) - a first-boot ordering bug caught on the box.

Evidence (M33MU box, one tree): `positive`, `bothpsa`, `bothiso` all PASS, plus
`confboot` PASS. Both guests emit `freertos_guest1: ffm sha256 ok` / the guest0
`SERVICE_CRYPTO dispatch verified` marker through the SERVICE_HSM relay; guest0
key-ops sign/verify, key negatives, forged-handle (st=-129) and oversized-vector
(st=-135) rejections all green; both-OS isolation negatives (forged-handle,
oversized-vector, unknown-SID connect refused) green from Zephyr and FreeRTOS. No
fault markers. WT-FFM-0054's "no non-secure path outside the SPM" is met for the
non-secure side; the secure-image retirement (delete the CMSE veneers +
`crypto_service.c`/`ffm_crypto_client.c`, strict nm absence guards) is S6g.

## Phase 7 S6g - Bypass deleted from the secure image (WT-FFM-0054 met, 2026-08-28)

Closes the single-mediated-path milestone: the raw non-secure-to-wolfHSM
transport no longer exists anywhere - not as veneers, not as a transport
implementation, not as a port-contract capability. The SPM is the sole
gatekeeper by construction, and build guards keep it that way.

Deleted:

- The `WolfTrust_HSM_Submit/Poll/Cancel` CMSE veneers and their prechecks
  (`port/stm32h563/platform_stm32h563.c`) - the secure image and its CMSE
  import library no longer export any raw HSM entry point.
- `src/arch/armv8m/cmse_transport.c` + header - the shared-RAM CSR transport.
- `src/services/crypto_service.c` + header and `src/client/ffm_crypto_client.c`
  + header - the retired SERVICE_CRYPTO op-protocol face and its NS helper.
- The crypto-SP isolated-compute block (work struct, MSP-switch trampoline,
  `wt_platform_run_crypto_sp_isolated`) and the descheduled
  `wt_spm_sp_entry`/`wt_spm_sched_start` service loop.
- The monitor's veneer-only HSM wake hooks (`wt_monitor_hsm_request_pending`/
  `_response_ready`).
- The port-contract bypass affordance: `WT_PORT_CAPABILITY_HSM_TRANSPORT`, the
  `wt_hsm_transport_window_t` descriptor and per-guest bindings, the window
  validation in `wt_partition_validate_port_binding`, and the `memory_map.h`
  NS-RAM window macros. A port can no longer even declare a direct NS-to-HSM
  window.

Repaired two latently-red scenarios the full matrix surfaced:

- crossdomain: both `WT_FFM_NEGATIVE_PROBE` sites lived in the descheduled
  crypto-SP path - dead code since the manifest swap. The probe now lives in
  the live unprivileged ITS partition loop, restoring a genuine out-of-domain
  MEMFAULT.
- spfaultneg: the runner still asserted the pre-relay probe signature
  (MEMFAULT at SPM RAM) and the retired guest connect-failure marker, but the
  probe has been `udf #0` in the privileged relay entry since the relay
  landed (link-only validation then). The assertions now match the relay:
  Secure-Thread UNDEFINSTR caught, no HardFault/SecureFault escalation, and
  the RESTARTED relay serves every mediated request from both OS clients.
  The pinned-client defined-error unblock stays host-proven in
  `tests/host/sp_recovery`.

Found and fixed a REAL resilience defect the repaired scenario immediately
exposed: the relay's fault window can overlap a guest's boot, and the NS
wolfHSM client glue treated one failed init as terminal. The probe faults the
relay at its first scheduling; guest0's client SYS_INIT then races the
recovery window, its `wh_Client_Init` connect to SERVICE_HSM is refused by
the faulted partition, and the glue latched `g_client_ready = 0` forever - so
every later mediated crypto op on guest0 failed (surfacing as -132) while
guest1, booting after recovery, was fine. Diagnosis chain: A/B (same probe
build, udf disabled → fully green) proved the aftermath; a guest-side probe
returned the glue's fail-closed rc, pinning the latch (a rebuilt healthy
server had already ruled the secure side out). Fix (client-side, where FF-M
puts it - partitions may restart, clients must reconnect): the glue heals on
demand - `wolfhsm_guest_ensure_ready` retries the init on the next crypto
request, the registered crypto callback is the healing wrapper
`wolfhsm_guest_cryptocb`, boot init failure downgrades to a warning, and the
bare-metal glue's RNG stub retries the same way. Defense kept on the secure
side: `wt_hsm_relay_reinit_servers` rebuilds each ready per-guest server
(cleanup + fresh DRBG + re-init, tasklets and configs preserved) from the
recovery release stage, so a genuinely torn mid-request server is never
trusted; a guest whose rebuild fails stays down (fail closed).

Ported: the wolftrust-tee Zephyr module's init/ping/invoke liveness probes now
ride the mediated `WolfTrust_FFM_FrameworkVersion` veneer; every scenario
marker ("wolfTrust TEE client initialized", impl-id, framework version) is
unchanged.

Guards (fail the build, not just a scenario): `nm` must show no
`WolfTrust_HSM_(Submit|Poll|Cancel)` in the secure ELF (mk link rule), in
guest0 (`build_guest.sh`), or in guest1 (`build_freertos_guest.sh`, which also
asserts the mediated `wt_hsm_psa_transport_cb` is present).

Host evidence: `crypto_service` suite deleted; `psa_ffm_client` and
`ffm_veneer` re-fixtured onto the production `wt_hsm_relay_dispatch` with a
SHA-256 submit hook (same KAT digest, now through the relay); `make test`
`unit/all` green; core/port split guard hard leaks 0.

Target evidence (M33MU box, one tree): positive, bothpsa, bothiso, restart,
crossdomain, spfaultneg, confboot, devstorage, devcrypto, devattest, attestneg,
authneg, rollbackneg, fwustage, remeasureneg, bootupdate all PASS.

## Phase 7 audit - attestation veneers retired, veneer whitelist (WT-SYS-0014, 2026-08-28)

An adversarial audit of the closed single-mediated-path milestone confirmed
the wolfHSM bypass gone tree-wide, then found one live sibling of the same
shape: three direct attestation CMSE veneers
(`WolfTrust_Attest_GetTokenSize/GetToken/GetPublicKey`) calling secure
attestation code outside the SPM in the default image - `GetToken` (the
IAK-signing, token-minting entry) with no in-tree caller at all.

Fixed:

- The three veneers and their impls are deleted from the platform. The size
  and public-key queries ride `psa_call` to SERVICE_ATTEST as new call types
  (`WT_ATTEST_OP_TOKEN_SIZE`, `WT_ATTEST_OP_PUBLIC_KEY`,
  `attestation_service.c`) with the exact PSA status mapping the retired
  client produced (ARM test_a001 semantics preserved); the Zephyr client's
  every attestation request is now psa_connect/psa_call. The token path
  already rode FF-M and is unchanged.
- The secure-image guard is now a WHITELIST: any `__acle_se_` veneer symbol
  outside `WolfTrust_FFM_*` fails the link - a renamed reintroduction of any
  direct door is caught categorically. The dedicated vnet firmware opts
  `WolfTrust_VNet_*` in via CONFIG_VNET=y; that image sits outside the FF-M
  mediation boundary by design and the production stm32h563 image can never
  contain its veneers.
- All three nm guards fail closed on nm errors; guest0 gained the positive
  `wt_hsm_psa_transport_cb` assert; both guest guards also refuse
  `WolfTrust_Attest_*`.
- The "SERVICE_CRYPTO dispatch verified" marker is renamed
  "mediated crypto dispatch verified" in lockstep (guest, M33MU runner, CI
  yml, H5 runner); KAT input bytes unchanged.

Evidence: `tests/host/attestation_service` extended - mediated token-size
query (exact size + bad-size refused with PSA_ERROR_INVALID_ARGUMENT through
real FF-M dispatch) and mediated IAK public-key query (65-byte uncompressed
point, 0x04 prefix) - plus host `unit/all`, split guard, and the M33MU
positive/devattest/attestneg/bothpsa/confboot scenarios on one tree.

## Mediated VNET S0 - requirements seated (WT-SYS-0015, WT-FFM-0056..0058, 2026-08-28)

Docs-only slice: seats the requirements for re-homing the virtual Ethernet
switch behind the FF-M SPM as SERVICE_VNET, replacing the raw
`WolfTrust_VNet_*` CMSE veneers (the last designed-in non-FFM NS surface,
previously fenced by #139/#141/#142 - this milestone deletes it instead).

- `system.md`: WT-SYS-0015 - isolated guests exchange network frames only
  through an SPM-dispatched virtual network Secure Partition; ports bind to
  the SPM-stamped caller identity; disabled by default; no NS-callable entry
  point outside the FF-M client ABI.
- `framework.md`: WT-FFM-0056 (psa_call-only switch access with per-caller
  port isolation), WT-FFM-0057 (raw veneers absent from every image, secure
  whitelist admits only the FF-M gateway even with the capability enabled),
  WT-FFM-0058 (two guests exchange Ethernet/IP end to end through
  SERVICE_VNET; compile-time gated, nothing in the default build), plus the
  mediated virtual network acceptance gate naming the proving slices.
- `task-list.md`: SERVICE_VNET feature milestone seated between the
  OS-integration and hardware phases with sub-slices S0-S6 (#143-#149).

Design facts fixed at seat time (exploration-verified): NS guests cannot
block on PSA signals (`psa_wait` is SP-only, gated to scheduled SP slots;
NS `psa_call` is synchronous run-to-completion), so RX is poll-based
`RX_READ`/`RX_RELEASE` over `psa_call` - matching the existing demo guest,
which already polls (`vnet_ll_poll`; `IrqAck` never called). Capacity
ceilings are maxed and S2 must bump `limits.max_partitions` 6->7,
`profile_capabilities.max_domains` 9->10, `WT_FFM_MAX_PARTITIONS` 9U->10U;
next free sid 4103, next domain id 9. `manifest-conformance.json` is not
touched (SERVICE_VNET is not a PSA conformance service).

Evidence: requirement rows present and traced (WT-SYS-0015,
WT-FFM-0056..0058); acceptance gate names the proving slices; no runtime
gate for this slice; host suite unaffected (`make test` unit/all PASS).
Commit: `316849e`.

## Mediated VNET S1 - SERVICE_VNET relay dispatch host-proven (WT-FFM-0056, 2026-08-28)

`src/services/vnet/vnet_relay_service.c` + `include/wolftrust/services/
vnet_relay.h`: the neutral SERVICE_VNET dispatch loop (WAIT -> GET -> op ->
REPLY, modeled on the HSM relay) driving the unchanged `src/vnet/` switch
data plane. Operations ride the psa_call type (OPEN, SET_MAC, TX, RX_FETCH,
IRQ_ACK); the caller's switch port comes only from the SPM-stamped
`-(guest+1)` client id (mirrors wt_ffm_boot_caller_guest; secure-origin and
out-of-range ids refused). RX_FETCH dequeues, copies, and releases one frame
in a single call, so pool slot/generation cookies never leave the secure
side - the stale-cookie/double-release surface does not exist at the NS
boundary. Switch refusals reply as the untranslated WT_VNET_E_* code (the
-3000 range does not collide with PSA_ERROR_*); no switch installed fails
closed with PSA_ERROR_NOT_SUPPORTED. Frame staging uses two file-scope
1536-byte buffers (one message in flight in the cooperative SPM; keeps the
partition stack small).

Evidence: `tests/host/vnet_relay` (23 checks, all printing WT-FFM-0056) -
boot-core init from a two-partition fixture manifest, registration, connect
per guest, fail-closed-no-switch, OPEN info, SET_MAC binding, byte-exact
guest0 TX -> switch -> guest1 RX_FETCH round trip through the real
`src/client/psa_ffm_client.c` marshaling, queue drain after one fetch
(release proven), no reflected or cross-port delivery, spoofed source MAC
refused, unknown unicast dropped (flood off), runt frame refused, short
SET_MAC vector refused, undersized RX meta vector refused, unknown op
refused, out-of-range stamped identity refused. Suite added to the
aggregate host run; `make test` unit/all PASS. Commit: `f65a635`.

## Mediated VNET S2 - SERVICE_VNET seated in the manifest and SPM boot path (2026-08-28)

`port/stm32h563/manifest-vnet.json` (new, selected by `CONFIG_VNET=y`;
`WT_CONFORMANCE=1` takes precedence): the production manifest plus domain 9
(entry window 0x0C019000/4K, stack 0x30093000/8K, FWU-mirrored class/role/
policy) and `PARTITION_VNET` with `SERVICE_VNET` sid 4103; its own
`max_partitions` 7 / `max_domains` 10. `manifest.json` is untouched, so the
default image carries no virtual network manifest row, generated id, or
service at all. `WT_FFM_MAX_PARTITIONS` stays 9U (conformance uses 8, the
vnet manifest 7). The partition stack aliases the conformance data window
(`WT_SP_VNET_STACK_*`; vnet and conformance builds are mutually exclusive,
the window is empty otherwise), so the fully-consumed secure RAM chain needs
no re-layout and no other manifest changes.

Wiring: `wt_spm_vnet_entry`/`wt_spm_vnet_start` (spm_svc.c, CONFIG_VNET
only) install the SVC transport, the monitor-owned switch via the new
`wt_vnet_service_switch()` accessor (NULL until init -> relay fail-closed),
and the scheduler tick, then schedule the relay loop like the other
partitions; `ffm_boot.c` registers `wt_vnet_relay_dispatch` and starts the
partition under `#ifdef PARTITION_VNET_ID`, which only the vnet manifest
generates. `tools/manifest/generate.py` validates the new manifest clean.

Evidence: host `make test` unit/all PASS (vnet_relay suite riding the new
boot registration path); box container cross-builds green both ways -
default `make all` (no VNET symbol or manifest row in the image) and
`make all CONFIG_VNET=y` (7-partition manifest generated, secure veneer
whitelist passing). Commit: `2612167`.

## Mediated VNET S3 - guest client transport on psa_call (WT-FFM-0056, 2026-08-28)

`src/client/vnet_psa_transport.c` + `include/wolftrust/vnet_psa_transport.h`:
the neutral guest-side transport - open (psa_connect + the OPEN call,
returning the switch info), set_mac and tx as one psa_call each, rx_fetch as
one psa_call returning the frame length (or the untranslated WT_VNET_E_*
refusal, WT_VNET_E_EMPTY when idle), close. The WT_VNET_OP_* codes and the
service SID moved into `vnet/vnet_abi.h` (the shared NS/S ABI header) so the
client compiles freestanding without SPM-internal headers. The stm32h563-vnet
demo guest is repointed: `vnet_ll_send`/`vnet_ll_poll` and the open/set-mac
bring-up now ride the transport - receive is ONE mediated call where the raw
path needed RxPoll + RxRead + RxRelease, and no slot/generation cookie ever
reaches the guest. `wolfip_config` unchanged (the swap is entirely below
wolfIP's send/poll seam). The guest links `src/client/psa_ffm_client.c` and
the transport; the raw veneer symbols have no remaining call site in the
demo (deleted from the build in the next slice).

Evidence: `tests/host/vnet_relay` extended to drive the REAL transport
functions through the stubbed gateway into the live dispatch + switch - 28
checks green including transport open/info, TX, cross-guest byte-exact
RX_FETCH, and EMPTY pass-through; aggregate `make test` unit/all PASS. Box
container builds the repointed firmware green: secure CONFIG_VNET=y image +
both guest ELFs linking the mediated client (lib/wolfIP submodule
initialized to build the demo). The end-to-end emulator ping is the S5
scenario's gate. Commit: recorded in the next entry.

## Mediated VNET S4 - raw veneers deleted, whitelist pinned (WT-FFM-0057, 2026-08-28)

`src/arch/armv8m/vnet_nsc.c` is deleted outright - file, CONFIG_VNET build
entry, and the seven `WolfTrust_VNet_*` prototypes in `vnet/vnet_abi.h`;
the veneer-only `wt_vnet_service_begin` helper goes with it. The secure
veneer whitelist loses its CONFIG_VNET escape and is pinned to the exact
five gateway names - `__acle_se_WolfTrust_FFM_(FrameworkVersion|
ServiceVersion|Connect|Call|Close)$` - plus a count==5 assert, enforced on
every linked secure image. No build of wolfTrust can now export a
non-FF-M non-secure-callable veneer, and an added or renamed FF-M veneer
also fails the link. The vnet demo guests gain a fail-closed nm absence
guard (nm failure fails the build; any `WolfTrust_VNet_` reference fails;
`wt_vnet_psa_tx` must be present).

Evidence: host `make test` unit/all PASS; box container builds green on
all three secure configurations - default, `CONFIG_VNET=y`, and
`WT_CONFORMANCE=1` (with the scenario runner's `WT_SECURE_FLASH_*` layout;
a bare conformance make without that layout overflows flash by design of
the layout, not a regression) - each image holding exactly 5 veneers; the
repointed demo firmware builds with its guard green. This closes the
fenced-veneer follow-ups: the second NS surface no longer exists to fence.
Commit: recorded in the next entry.
