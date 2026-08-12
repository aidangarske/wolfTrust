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

- Host validation: [workflow run 31059194072](https://github.com/aidangarske/wolfTrust/actions/runs/31059194072) — passed.
- M33MU lifecycle matrix: [workflow run 31059193766](https://github.com/aidangarske/wolfTrust/actions/runs/31059193766) — passed.
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

Commits: `69dc8eb` (Phase D — isolated crypto SP compute),
`f15fe11` (Phase E — gated cross-domain probe).

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
  SPM-private RAM and faults —
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
  now asserts both fields flow from the generated manifest — a distinct
  `restart_limit` (7) and a `STOPPED` guest domain bind through to the runtime
  config, and the real manifest's `READY` restores a runnable guest.
- M33MU positive: full wolfBoot -> wolfTrust -> guest lifecycle green,
  `[EXPECT BKPT] Success`, exit 0, no fault markers — NS guests still boot
  runnable now that their run state is sourced from the manifest.

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Item 7 Initial Attestation through FF-M IPC

Commits: `29ab959` (server-side `SERVICE_ATTEST` dispatch), `0de6c52` (client
routes the token through the FF-M veneers).

- Host validation: `make test` green including `unit/attestation_service` — a
  real `psa_connect`/`psa_call` round trip carries a challenge in and a token
  out through `wt_attestation_service_dispatch` (backend stubbed to isolate the
  IPC routing; the token generator is M33MU-proven).
- M33MU positive: the guest's Initial Attestation now travels the FF-M IPC path
  (`SERVICE_ATTEST` connect/call/close) and still verifies —
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
  and stops reappearing — proving `wt_restart_guest` honors the manifest
  `restart_limit` (domain id 1) on target. This is the restart-policy coverage
  the happy-path lifecycle run never exercised.
- The surviving FreeRTOS guest keeps running throughout (wolfPKCS11 init, slot,
  session, SHA-256, heartbeats 0-4) while guest0 cycles, so the monitor
  gracefully restarts a Non-secure guest fault from the SecureFault handler and
  the scheduler continues — the guest-side half of graceful recovery (SP-side
  graceful recovery remains task #26).

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Item 8 detect-or-skip target-scenario harness (Slices 2-3)

Single repo-resident runner `tests/target/run_m33mu_scenario.sh
<positive|restart|crossdomain>` factors the shared wolfBoot -> wolfTrust ->
guests build with per-scenario deltas (secure `WT_FFM_NEGATIVE_PROBE`, guest
`WT_GUEST_FAULT_PROBE`, boot flags, assertions). `make test-target` auto-detects
an M33MU binary (or `WT_TARGET_SCENARIOS=1`) and runs restart + crossdomain, or
prints an explicit `SKIP` and passes — never a silent pass. It is a standalone
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

- Forged handle (`handle + 0x1000`, unmapped for this caller) —
  `wolfTrust FF-M forged-handle call rejected st=-129` (PSA_ERROR_PROGRAMMER_ERROR
  from `wt_ffm_connection_from_handle`).
- Oversized input vector (2048 > `WT_FFM_TRANSFER_BYTES` 1024, refused at
  `wt_ipc_validate_vectors` before any copy) —
  `wolfTrust FF-M oversized-vector call rejected st=-135`
  (PSA_ERROR_INVALID_ARGUMENT).

The guest still reaches `[EXPECT BKPT] Success`, so the SPM rejected both without
a fault or stale data — the target-side proof of the handle-integrity and
bounded-vector checks host-tested in `tests/host/ffm` (WT-FFM-0021/0032). Both
markers are asserted in the positive M33MU gate (`run_m33mu_scenario.sh
positive`, the box `run_m33mu.sh`, and the CI `wolfboot-wolftrust-m33mu` job) so
CI and the local gate stay identical. The positive `psa_connect`/`psa_call`
round trip and the cross-domain negative were already covered (SERVICE_CRYPTO
dispatch and the item-8 crossdomain scenario).

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Item 10 conformance expansion (Slice 1 — host version-policy tests)

Added the Arm FF-M version-policy tests `i010`, `i011`, and `i026` to
`tests/host/psa_ff_upstream/` (run locally on the host via `make
test-conformance`, no emulator needed). FF-M resolves an unspecified manifest
service to version 1 with `STRICT` policy, so the harness models
`SERVER_UNSPECIFIED_VERSION` as a `STRICT` service at version 1 — not the
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

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 — 16 host
  checks PASS (i010/i011/i026 included), `i067` SKIP (SP heap), ending
  `PASS: conformance/all`.
- `make test`: EXIT 0 — full host suite green including `unit/ffm`
  (`WT-FFM-0032 bounded vector rejection` on the new PROGRAMMER-ERROR mapping).

This is host conformance evidence for the version-policy behavior; the full Arm
suite under M33MU (NS app + 3 Secure test partitions) and the TF-M baseline
remain the later item-10 slices.

## Item 10 conformance expansion (Slice 2 start — negative call type i090)

Wired Arm FF-M test `i090` (`psa_call_with_neg_type`) into
`make test-conformance`: a `psa_call` with a negative message type must return
`PSA_ERROR_PROGRAMMER_ERROR`. This required the second half of the
PROGRAMMER-ERROR-family fix begun for i026 — `wt_ffm_call` returned
`PSA_ERROR_INVALID_ARGUMENT` for `type < 0`; per FF-M a negative type is a
PROGRAMMER ERROR, so `wt_ffm_call` now returns `PSA_ERROR_PROGRAMMER_ERROR`
(the `runtime == NULL`/`caller == 0` internal-argument guards still return
`INVALID_ARGUMENT`). The wolfTrust host test `WT-FFM-0036` gained a matching
negative-type assertion.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 — `i090` PASS
  alongside the Slice-1 set, ending `PASS: conformance/all`.
- `make test`: EXIT 0 — full host suite green including `unit/ffm`
  (`WT-FFM-0036 invalid arguments and empty wait` now covers the negative type).

The remaining server-dispatch tests (`i002`, `i003`, `i027`, `i063`) are
host-viable but need `test_dispatch()` to replicate per-test server behavior
(task 10b); `i048`-`i053` need real MPU isolation (M33MU) and `i058` needs a
Secure-Partition client (compiled out under `-DNONSECURE_TEST_BUILD`).

## Item 10 conformance expansion (Slice 2 — per-service dispatch, i003 data plane)

Added a per-test server dispatch to `tests/host/psa_ff_upstream/main.c`: because
the upstream tests reuse the same SIDs with contradictory server behavior, a
`g_active_test` selector routes `test_dispatch()` to the matching per-test
server instead of the generic reply-success. The harness `val` vtable gained
`ipc_connect`/`ipc_close`. Wired Arm FF-M test `i003` (invec/outvec data plane),
whose server `dispatch_i003()` replicates the upstream server faithfully:

- `zero_length_invec` / `zero_length_outvec`: read the one non-empty invec and
  write it to the one non-empty outvec; zero-length and NULL vectors are
  skipped, and `psa_outvec.len` reflects the bytes written.
- `call_read_and_skip`: the full `psa_read`/`psa_skip` workout — full reads,
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

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 — all six `i003`
  checks PASS alongside the prior set, ending `PASS: conformance/all`.
- `make test`: EXIT 0 — full host suite green.

Remaining server-dispatch tests for the router: `i002` (connection lifecycle),
`i063` (signal-mask filtering).

## Item 10 conformance expansion (Slice 2 — connection drop i027)

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

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 — `i027` PASS,
  ending `PASS: conformance/all`.
- `make test`: EXIT 0 — full host suite green including the new
  `WT-FFM-0022 dropped connection close`.

## Item 10 conformance expansion (Slice 2 — signal-mask refusal i063)

Wired Arm FF-M test `i063` (`psa_wait_signal_mask`): the RoT service refuses
both connects (`SERVER_UNSPECIFIED_VERSION`, `SERVER_RELAX_VERSION`), and the
client confirms it receives `PSA_ERROR_CONNECTION_REFUSED` for each — the
client-visible half of the test. The upstream rule under test (a Secure
Partition using a masked `psa_wait` to ignore an unrelated irritator signal)
needs a real multi-signal scheduler and is deferred to the M33MU slice; the host
dispatch has no concurrent irritator to filter. No production change.

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 — `i063` PASS,
  ending `PASS: conformance/all`. `make test`: EXIT 0.

## Item 10 conformance expansion (Slice 2 — connection lifecycle i002, all 9 checks)

Wired Arm FF-M test `i002` (connection lifecycle) end to end via the per-test
dispatch (`dispatch_i002`, keyed on `g_i002_check`):

- `connection_busy_and_reject`: the two connects reply `CONNECTION_BUSY` then
  `CONNECTION_REFUSED` (per-connect sequence counter).
- `accept_and_close_connect`, `connect_with_allowed_version_policy` (UNSPEC@1,
  STRICT@2, RELAX@1, RELAX@2), `psa_call_with_allowed_type_values`
  (`{PSA_IPC_CALL,1,2,INT16_MAX}`): all accepted.
- `psa_call_with_allowed_status_code`: the server replies each of
  `{PSA_SUCCESS,1,2,INT32_MAX,-1,-2,INT32_MIN+128}` in turn and `psa_call`
  returns it verbatim — confirming negative non-PROGRAMMER_ERROR statuses pass
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

- `make test-conformance BUILD_DIR=/tmp/wolftrust-conf`: EXIT 0 — all nine
  `i002` checks PASS, ending `PASS: conformance/all`. `make test`: EXIT 0.

## Item 10 P1a — manifest-bound partition dispatch registry (WT-FFM-0014)

Replaced the per-PID `if` chain in `src/ffm_boot.c` with a manifest-bound
dispatch registry: `wt_ffm_partition_runtime_t` gained a `dispatch`/
`dispatch_context` pair, `wt_ffm_register_partition` binds a service loop to a
partition, and `wt_ffm_dispatch_message` routes each message to the owning
partition's registered handler, using the port dispatch op only as a fail-closed
fallback. Crypto + attest register through `wt_ffm_boot_init`.

Evidence (host, EXIT 0):
- `make test`: new `PASS: WT-FFM-0014 partition dispatch routing` — a registered
  loop intercepts connect/disconnect for `TEST_PARTITION_ID` (dispatch count 2),
  the generic port op is bypassed (count 0), argument/lookup validation returns
  `ARGUMENT`/`POLICY`; plus `SERVICE_CRYPTO`/`SERVICE_ATTEST` KAT round trips
  still green through real FF-M dispatch.

Not yet proven (split out, not claimed): P1t schedulable execution context and
P1r production `ffm_boot` registration regression are target-only and deferred
to the next M33MU box gate (the Mac `arm-none-eabi` toolchain lacks libc headers,
so `src/ffm.c`/`src/ffm_boot.c` cannot be target-compiled locally).

## Phase gate rule

Every implementation phase must repeat host tests and the complete M33MU
matrix on its exact phase commit. When target hardware is available, the same
phase must additionally record a physical-board smoke result covering the
phase behavior.
