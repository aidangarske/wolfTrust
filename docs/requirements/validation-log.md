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

P1r CLOSED (M33MU emulator, wolf-prec5560, 2026-08-12): local gate on the tree
at `0183818` (P1a registry + P2a ingester, pre-P1b) — `PASS: local M33MU gate`,
exit 0, `[EXPECT BKPT] Success`, attestation COSE_Sign1 verified, SHA-256 KAT,
AES-CTR, FreeRTOS PKCS11 digest all green, no fault markers. The production
dispatch-registry path serves SERVICE_CRYPTO and SERVICE_ATTEST unchanged.

## Item 10 P2a — Arm manifest ingestion, psa_manifest identity headers

`tools/manifest/ingest_psa_arch.py` ingests the unmodified upstream
`server/driver/client_partition_psa.json` and emits `psa_manifest/pid.h`,
`sid.h`, and per-partition signal headers through `generate.py`'s existing
emitters, replacing the hand-faked `-D` defines path for upstream `val`/PAL
includes. FF-M defaulting applied: missing `version` → 1, missing
`version_policy` → STRICT; service and IRQ signals assigned per partition from
0x10 upward.

Evidence (host, EXIT 0): `make test-conformance` now runs
`tests/host/manifest_ingest/run.py` against the real fetched manifests —
asserts all 12 SIDs (0xFA01, 0xFB01-07, 0xFC01-04), version defaults
(`SERVER_UNSPECIFIED_VERSION_VERSION`=1, `SERVER_STRICT_VERSION_VERSION`=2),
and driver signals including `DRIVER_UART_INTR_SIG_SIGNAL`=0x100 — then the
full 24-test host subset, ending `PASS: conformance/all`.

## Item 10 P1b + P2 carve — M33MU positive gate (run 3)

Gate history on this slice, all M33MU emulator on wolf-prec5560 (2026-08-12):
- Run 2 FAILED (build): `platform_stm32h563.c`'s new `psa_manifest/pid.h`
  include had no rule dependency on the manifest generation stamp; the target
  compile raced the generator. Fixed with an explicit
  `sec_platform_stm32h563.o` rule mirroring `sec_ffm_boot.o`. The P1b logic
  never executed in that run.
- Run 3 PASSED on `14b08b9`: `PASS: local M33MU gate`, exit 0,
  `[EXPECT BKPT] Success`, no fault markers — with `wt_platform_run_crypto_sp_isolated`
  resolving the crypto SP MPU domain from the bound manifest
  (`wt_ffm_resolve_secure_domain(PARTITION_CRYPTO_ID)`, stack from the
  manifest's writable resource, fail-closed) AND the 5-slot secure stack carve
  (SPSTACKS 0x30096000/40K, production crypto/attest stacks moved to
  0x30096000/0x30098000).

P1b CLOSED — negative gate (WT_FFM_NEGATIVE_PROBE=1) on the same tree PASSED
(M33MU, 2026-08-12): `[MEMFAULT] pc=0x0c060f34 addr=0x30028000`, the crypto SP's
read of SPM-private RAM from inside the manifest-resolved domain faults, exit 1,
`PASS: negative M33MU gate`. `sp=0x30097ff0` and `r4=0x30096000` confirm the SP
executed on the new manifest-resolved stack carve (crypto slot 0x30096000). Both
halves of the resolver wiring (positive run 3 + this negative) hold on one tree.

## Item 10 P1t-1 — SPM-call gate (host)

The single privileged choke point `wt_spm_gate` (`src/spm_gate.c`,
`include/wolftrust/spm_gate.h`) that every SP-side `psa_*` will funnel through
on target once the SVC handler unmarshals registers into `wt_spm_call_t`. It
routes wait/get/set_rhandle/read/skip/write/reply/notify/clear to the existing
`wt_ffm_*` runtime, bounds every SP-supplied pointer against the caller's
resolved protection domain via `wt_secure_domain_contains` before the
privileged SPM dereferences it, and `wt_spm_call_would_block` flags an empty
`psa_wait` (runtime returns `WT_FFM_ERROR_NOT_READY`) as "suspend this SP".

Host evidence (2026-08-12), `tests/host/spm_gate` — 62 checks, green under
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

Not in the secure build yet — the target SVC gate, unprivileged drop, per-SP
MPU on switch-in, and real coroutine suspend/resume land together in P1t-2
under one M33MU gate. Valgrind not run locally (macOS host); it is the CI
Valgrind workflow's responsibility.

## Item 10 P1t-2a — gate live in the production image (M33MU)

The production crypto service dispatch (`src/services/crypto_service.c`) now
routes `psa_wait`/`psa_get`/`psa_read`/`psa_write`/`psa_reply` through
`wt_spm_gate` instead of calling `wt_ffm_*` inline, and `src/spm_gate.c` is in
the secure build source list. Still privileged and synchronous — no isolation
change; this slice proves the gate on the real production dispatch path before
the P1t-2b SVC/unprivileged work builds on it.

- Host (2026-08-12): `tests/host/crypto_service`
  (`PASS: SERVICE_CRYPTO SHA-256 KAT through real FF-M dispatch`) green under
  `cc`, `gcc`, `clang`, and ASan+UBSan with the gate-routed dispatch; full
  `make test` `PASS: unit/all`.
- M33MU positive gate (2026-08-12, wolf-prec5560): `PASS: local M33MU gate`,
  exit 0 — `wolfTrust FF-M SERVICE_CRYPTO dispatch verified`,
  `psa_hash_compute(SHA-256) KAT verified`, `psa_initial_attestation st=0`,
  `[EXPECT BKPT] Success`, no fault markers. The KAT result therefore
  transited `wt_spm_gate` inside the production secure image.
- Negative gate not re-run: `WT_FFM_NEGATIVE_PROBE` exercises
  `wt_crypto_sp_body`/`wt_platform_run_crypto_sp_isolated` (platform compute
  layer), which this slice does not touch.

## Item 10 P1t-2b — coroutine-backed unprivileged SP via SVC gate (M33MU)

The crypto Secure Partition now runs as a scheduled coroutine, unprivileged on
its own PSP stack inside its manifest MPU domain, reaching the SPM only through
`svc #1`. The service loop is the same architecture-neutral code the host tests
prove (`src/services/crypto_service.c`); only the transport differs — each
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
  stack) and `xpsr=0x01000000` (Thread mode) — the unprivileged SP's read of
  SPM-private RAM (WT_RAM_S_BASE) is denied by the MPU, proving genuine
  unprivileged isolation, then recovers gracefully via the tasklet fault path.
- Runner flake fixed (recurred twice): wolfBoot host keytools `-j` link race
  (sp_ModExp_*/sp_Rsa* linked before their objects). `run_m33mu.sh`,
  `run_m33mu_negative.sh`, and the CI yml now build `keytools` serially first.

## Item 10 P3a-1 — schedule Secure Partitions from a coroutine-keyed table (M33MU)

The single hardcoded crypto SP in `src/arch/armv8m/spm_svc.c` is now a slot
table (`g_spm_sp[WT_FFM_MAX_PARTITIONS]`). The privileged SVC `#1` dispatcher
resolves the caller from `wt_co_current()` and validates the call pointer
against that slot's own MPU thread table before running `wt_spm_gate`, so N
partitions share one transport, each confined to its manifest domain. Slot setup
is factored into `wt_spm_sched_add(runtime, pid, entry, arg)`; `wt_spm_sched_start`
calls it for the crypto SP (table entry 0). A slot is published (`count++`) only
after `wt_ffm_register_partition` succeeds, so an SP is never visible half-built.
This is the seam P3a-3 adds Arm's `server_main`/`client_main` through with no
scheduler-core change. Pure foundation — no Arm SP yet, crypto behavior unchanged.

- Target syntax check: `arm-none-eabi-gcc -fsyntax-only -std=c99 -Wall -Wextra
  -mcpu=cortex-m33` on `spm_svc.c` — exit 0, no warnings.
- M33MU positive gate PASS (2026-08-12): `PASS: local M33MU gate`, exit 0,
  `wolfTrust FF-M SERVICE_CRYPTO dispatch verified`,
  `psa_hash_compute(SHA-256) KAT verified`, forged-handle st=-129,
  oversized-vector st=-135, `psa_initial_attestation st=0`,
  `[EXPECT BKPT] Success`, no fault markers — the crypto SP still transits
  veneer -> svc #1 -> gate -> coroutine through the generalized slot table.
- M33MU negative gate (`WT_FFM_NEGATIVE_PROBE=1`) PASS: `[MEMFAULT]
  addr=0x30028000` with `sp=0x30097fe0` (SP on its PSP domain stack) and
  `xpsr=0x01000000` (Thread mode) — the per-slot MPU table still denies the
  unprivileged SP's read of SPM-private RAM, then recovers via the fault path.

## Item 10 P3a-2 — NS->S psa_* version veneers (M33MU)

The Non-secure guest now reaches the SPM's framework-version query through a real
PSA client veneer, not the TEE-driver `tee_invoke_func` path. New secure
`cmse_nonsecure_entry` veneers `WolfTrust_FFM_FrameworkVersion()` and
`WolfTrust_FFM_ServiceVersion(sid)` (`src/ffm_boot.c`, auto-exported into the CMSE
import library) alias `wt_ffm_framework_version`/`wt_ffm_service_version`. NS
`psa_framework_version()`/`psa_version()` wrappers over those veneers live in the
Zephyr `wolftrust-tee` module (`wolftrust_tee_driver.c`) — the exact symbols the
upstream Arm val NSPE links. The connect/call/close aliases, which need
`psa/client.h`'s `psa_invec`/`psa_outvec` types, arrive with val in P3a-3.

- M33MU positive gate PASS (2026-08-12): the guest calls `psa_framework_version()`
  and logs `wolfTrust FF-M psa_framework_version=0x0100` (== `PSA_FRAMEWORK_VERSION`)
  — asserted by the `positive` scenario — then `PASS: target/positive`, exit 0,
  no fault markers. Proves NS shim -> CMSE veneer -> `wt_ffm_framework_version`
  end to end.

## Phase gate rule

Every implementation phase must repeat host tests and the complete M33MU
matrix on its exact phase commit. When target hardware is available, the same
phase must additionally record a physical-board smoke result covering the
phase behavior.
