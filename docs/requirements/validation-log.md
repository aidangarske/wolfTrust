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

## Item 10 P3a-3 — Arm conformance partitions compiled into the secure image (M33MU)

Arm's unmodified `ff/partition/server_partition.c` and `client_partition.c`, plus
the i001/i003 test bodies, now compile into the secure image (behind
`WT_CONFORMANCE=1`) against wolfTrust's own `include/psa/*.h`, the generated
`psa_manifest/` headers, and a new STM32H563 `pal_config.h`. `wt_ffm_boot_start_sched`
schedules `server_main`/`client_main` as unprivileged coroutines via
`wt_spm_sched_add` on their P2-carved manifest stacks (0x3009A000 / 0x3009E000).

The service-side PSA API is now target-native: `src/arch/armv8m/spm_sp_api.c`
marshals every `psa_*` service call into a `wt_spm_call_t` and traps to the
privileged gate via SVC (`wt_spm_sp_call`), replacing the direct
`src/ffm_api.c` bindings in the target build — those dereferenced SPM state an
unprivileged partition cannot reach (the P1t-2b fault class, now structurally
impossible). The SVC entry stamps `call->partition_id` from the scheduled slot,
so a partition cannot impersonate another; a latent conflation was fixed by
giving `psa_notify` its own `notify_partition` field distinct from the acting
partition id. SP-as-client IPC (`psa_connect`/`psa_call` from a partition) has
no gate op yet and refuses closed rather than faking success — tracked with P3b.

- Host `make test`: EXIT 0 (`spm_gate` 62 checks, `unit/all`) — the
  `notify_partition` split keeps the gate NOTIFY path green.
- Target dry-compile: all six upstream sources + every touched wolfTrust source
  compile clean under `arm-none-eabi-gcc` (`-Wall -Wextra`, `-mcmse`).
- M33MU `confboot` gate PASS (2026-08-12): the conformance image (both Arm SPs
  scheduled) boots the full positive lifecycle green — `PASS: target/confboot`,
  exit 0, `[EXPECT BKPT] Success`, no fault markers.
- M33MU regression on the same tree PASS: `positive` (production image unhurt by
  the psa_* API move) and `crossdomain` (`[MEMFAULT] addr=0x30028000` still
  denied). `restart` is NS-guest-fault behavior, orthogonal to the SP psa_*
  change, last proven on the P1 keystone tree.
- New `confboot` scenario wired into `run_m33mu_scenario.sh`, `make test-target`,
  and the CI matrix. The unmodified Arm partitions are the true TF-M drop-in.

## Item 10 P3a-4a — first unmodified Arm conformance test green (i001, M33MU)

`PASS: target/confboot` with `Result=Passed`, `TOTAL PASSED : 1`,
`TOTAL FAILED : 0` (2026-08-13): Arm PSA Arch Test Suite v1.8 test_i001 runs
end-to-end through wolfTrust's production SPM — NS val framework in the Zephyr
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
  (`spm_svc.c`) — a client partition blocks on its message while the serving
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
  commit — see the commit that carries this entry.

## Item 10 P3a-4b — multi-vector psa_call green (i003, M33MU)

`PASS: target/confboot` with `TOTAL TESTS : 2`, `TOTAL PASSED : 2`,
`TOTAL FAILED : 0` (2026-08-13): test_i003 (Testing IOVECS, all 6 checks —
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
  discriminator (dead handle fails ARGUMENT, not BUFFER) — spm_gate 67 checks.
- `psa_set_rhandle` on a DISCONNECT message returned STATE and panicked the
  server; FF-M requires success with no observable effect (i003 checkpoint
  206). Now a no-op success; the host ffm test had asserted the wrong
  behavior and was corrected to the spec.

Host `make test` green on the same tree; guest VERBOSITY raised to 3 and the
NS `wtconf:` traces gated behind `WT_CONF_TRACE` (default off). Production
regression (positive + crossdomain) rerun on the same tree before commit.

## Item 10 P3b — NS val NVM through the DRIVER partition (M33MU)

`PASS: target/confboot` with `TOTAL TESTS : 2`, `TOTAL PASSED : 2`,
`TOTAL FAILED : 0` (2026-08-13): the NS val framework's `pal_nvm_read/write`
now cross the boundary as IPC clients of the DRIVER partition's NVMEM service
(`nvmem_param_t` invec + data vec to SID 0xFC03, `nonsecure_clients` already
granted by the generated manifest), so NS and SPE val share the single
driver-served store — required for every P3c test whose bookkeeping crosses
NS<->S. The private NS RAM store is deleted. val reads/writes NVM throughout
both tests, so the green run exercises the path constantly.

Scope splits recorded in the task list: SPE `pal_print` -> real UART requires
the driver domain's manifest MMIO grant (P4's ingestion mechanism, moved
there); watchdog remains a no-op on M33MU (no WDG model; P6).

Host `make test` green on the same tree. The slice changes only the
conformance guest (`WT_RUN_CONFORMANCE`), so the production positive and
crossdomain binaries are identical to those proven green on `48b63e6`; no
rerun was performed.

## Item 10 P3c-2 Phases A-C — doorbell + signal-mask scheduler completeness (host)

Host `make test` green across three committed slices; no target run (box
`wolf-prec5560` offline since 2026-08-13, so Phases D-F are blocked).

- Phase A (`8f01063`): `psa_wait` signal-mask filtering locked against
  regression. `tests/host/ffm` `test_wait_signal_mask` and `tests/host/spm_gate`
  prove an out-of-mask asserted signal keeps a wait blocking and that
  `psa_wait` returns only `asserted & mask`.
- Phase B (`9c8781c`): i058 Check-1 doorbell state machine through the gate
  (`test_gate_doorbell_state_machine`), plus the real POLL/BLOCK fix — the
  `psa_wait` `timeout` is threaded through `wt_spm_call_t` and
  `wt_spm_call_would_block` so `PSA_POLL` returns instead of blocking (i058's
  final poll would otherwise hang). spm_gate 90 checks.
- Phase C (`52d3f67`): doorbell-driven origination with masked starvation, the
  portable core of i063 (`test_doorbell_origination`). A doorbell-woken client
  originates an outbound connect through the SP-as-client gate; it stays starved
  on a masked server signal across the server's masked waits and is delivered
  only on an explicit wait, completing `CONNECTION_REFUSED`. The existing gate +
  runtime already carry this — no scheduler change was required — so the
  outstanding target faults are choreography/epilogue bugs, not a missing
  capability. spm_gate 126 checks.

The coroutine choreography these prove out (`wt_spm_sched_dispatch`) is
Armv8-M-only and remains to be validated on M33MU (Phases D-F).

## Item 10 P3c-2 Phases D-F — six-test conformance green (M33MU)

`PASS: target/confboot` with `TOTAL TESTS : 6`, `TOTAL PASSED : 6`,
`TOTAL FAILED : 0`, `[EXPECT BKPT] Success`, exit 0 (2026-08-14): the full
non-IRQ/non-heap subset — i001, i003, **i058 (PSA_DOORBELL)**, **i063 (psa_wait
signal mask)**, i071, i088 — all `Result=Passed` through the unmodified Arm
suite on the real SPM. Tree = `3bac964` + `96fee67` (+ `1d37648`).

Root cause of the i058/i063 hangs, found via WT_CONFORMANCE-gated hang
tripwires (register-dump diag traps for silent stalls): the SP-side SVC
transport re-issued any gate call that returned NOT_READY, so a `PSA_POLL`
wait miss — which reports NOT_READY but must return, not suspend — spun the
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
`r6=0x00010001` (one replied-but-unharvested NS EXECUTE message) — matching
the POLL-spin prediction exactly.

Host `make test` green on the same tree. Production regression rerun on the
same tree: `PASS: target/positive` (full lifecycle green, exit 0) and
`PASS: target/crossdomain` (cross-domain read of 0x30028000 denied by the SP
domain).

## P4/P5 K1 — reset feasibility probe (GO, 2026-08-14)

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
  reads the SWAP state back intact — over the exact FLASH controller MMIO
  (`0x40022000`, NSKEYR/SECKEYR/NSCR/SECCR, `cpu/stm32h5_mmio.c`) that
  wolfTrust's `port/stm32h563/hsm_flash.c` already drives. wolfHSM NVM writes
  already succeed within a boot on M33MU, so the write path is live; the
  dualbank test adds the cross-reset persistence guarantee.
- **`.noinit` RAM survives too.** The same test uses a `.noinit reset_marker`
  to distinguish first vs. second boot — so a warm reset re-inits the CPU core
  only, leaving RAM and flash intact. Useful as a boot-count detector in K3/K4.

**Verdict: GO.** No hardware gating for the reboot bucket. `wt_platform_system_reset`
(AIRCR.SYSRESETREQ) is implemented in K3 so its first target run exercises the
production SPM panic→reset path rather than a disposable probe; K2 backs the
NVMEM service with reserved secure flash; K4 proves the full resume loop on i047.

## P4/P5 K4 — i047 needs per-SP MMIO isolation, not just the reboot keystone (2026-08-14)

The reboot-continuity keystone (K1–K3) is mechanically complete. Trying to prove
it end-to-end with i047 uncovered that i047 is fundamentally an MMIO-isolation
test, so it cannot pass on the current shared-CONFDATA domain model.

Runs on the box (`k4-trace2.log`, WT_CONF_TRACE on): the full schedule executes
and exits clean — `TOTAL TESTS 7, PASSED 6, FAILED 1`, `[EXPECT BKPT] Success`,
exit 0. The one failure is i047. (An earlier caveat: the plain run's log looked
like it "rebooted" — that was stdout buffering; the emulator's block-buffered
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
INSIDE it. So the SERVER can legally reach the DRIVER's MMIO address — the gate
buffer check passes, `must_panic` never fires, the server returns normally, the
client's `psa_connect(0xFB04)` gets `-130` instead of never returning, and val
marks i047 FAILED. To pass, the DRIVER's MMIO region must be OUTSIDE the SERVER
partition's domain (true per-SP MMIO isolation — task #33 / P4 bucket b). Every
available panic test hits this same wall (i047/i055/i057 use the DRIVER MMIO at
L3; i064–066 need `psa_eoi`), so the keystone can only be demonstrated once
per-SP MMIO isolation lands (or the driver MMIO is relocated outside all SP
domains).

**Secondary:** a timing-dependent heisenbug — the plain (untraced) build faults
in the guest `psa_call` invec copy, but the traced build completes; correlated
with unbounded FF-M handle growth (176 connects over the run, handle values
climbing +128 each with no reuse). Likely a fixed-size table indexed off the
handle overflowing once i047's extra connects push the count up. Needs the
handle allocation bounded/reused.

**Status:** K1–K3 done. K4 (and P4.1's panic tests) are gated on per-SP MMIO
isolation. i047 build wiring is in; the confboot schedule asserts 7 so the gate
is RED until the isolation work lands. Deep target work — Fable-class.

## P4/P5 K4 second pass — MMIO carve landed; blocked on an M33MU emulator defect (2026-08-14)

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
`CFSR=0x00010082` (UNDEFINSTR + stale DACCVIOL) at `pal_nvm_write+0x2c` — a
plain `add r3, sp, #8` that cannot fault on silicon. `--record` instruction
traces show: (a) the NS thread entered the FF-M veneer on its PSP
(`r7=0x20007bc4` frame) and resumed after the secure round trip with the MSP
selected (`sp=0x20005388`, `ctrl_ns=0`); (b) the final recorded events are a
SECURE SVC handler (IPSR=0x0B, secure PC `0x0c0636xx`) executing with the **NS
stack pointer bank selected** (`sp = msp_ns + 0x14 = 0x200053c8`) while the
registers still hold the scrubbed pre-BXNS veneer state
(`r1=r2=r3=r12=lr=0x080808ca`) — i.e. an SVC taken from secure thread mode at
the BXNS boundary stacked onto the NS bank. wolfTrust cannot select the SP bank
on exception entry; that is the CPU model. Reproduces identically on the pinned
emulator (`c84792f7`) AND current master (`f96ab8e`). A WT_CONF_TRACE build
passes because printk's UART stalls between the veneer return and the next
secure entry change the emulator's event interleaving — consistent with an
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

## M33MU emulator defect register

Defects in the pinned M33MU emulator that block conformance work. These are
CPU-model bugs, not wolfTrust logic; each carries a deterministic reproducer.
Emulator: `github.com/danielinux/m33mu`, pinned `M33MU_REF`
`c84792f7f9e9ce24cf94ffc492c36231de1854c2` (tests/target/run_m33mu_scenario.sh).

### M33MU-1: secure SVC at the BXNS boundary stacks onto the NS SP bank (OPEN)

- **Symptom:** with i047 in the confboot schedule, the untraced image
  deterministically stops at virtual cycle ~14,244,2xx: the NS guest reports
  `CFSR=0x00010082` (UNDEFINSTR + stale DACCVIOL) at `pal_nvm_write+0x2c`
  (`add r3, sp, #8` — cannot fault on silicon), `exc_ret=0xFFFFFFB8`,
  `ctrl_ns=0` while the thread's frame registers still point at its PSP stack.
- **Mechanism (from `--record` traces):** the NS thread enters an FF-M NSC
  veneer on PSP_NS; after the secure round trip the MSP is selected. The final
  recorded events show a SECURE SVC handler (IPSR=0x0B, PC `0x0c0636xx`)
  executing with the NS stack pointer bank selected (`sp = msp_ns + 0x14`)
  while registers hold the scrubbed pre-BXNS veneer state
  (`r1=r2=r3=r12=lr=0x080808ca`) — an SVC taken from secure thread mode
  adjacent to the BXNS transition stacked onto the wrong security state's SP.
  wolfTrust cannot influence exception-entry bank selection.
- **Reproduce:** tree @ `6bb8493` with i047 restored to the schedule (drop the
  `panic_test` marker via the mk sed) and confboot asserting 7. Build the
  confboot images (`WT_CONFORMANCE=1`), then:
  `m33mu wolfboot.bin wolftrust_v1_signed.bin:0x60000 zephyr.bin:0x80000
  freertos_guest1.bin:0xA0000 --uart-stdout --expect-bkpt 0x7f
  --quit-on-faults --timeout 90 --record --record-quiet --record-dump 120`.
  Byte-identical fault across 4 runs; reproduces on pinned `c84792f7` and
  master `f96ab8e`. A `WT_CONF_TRACE` guest build passes — printk UART stalls
  change the emulator's event interleaving (ordering sensitivity).
- **Evidence:** box `wolf-prec5560`: `k4-carve.log`, `k4-nsbank.log`,
  `k4-rec.log`, `k4-bigtrace.log` (traces), `k4-master.log` (master repro).
- **Impact:** blocks K4 (i047) and P4.1 (i055/i057 + panic set). Tracked:
  task #63.
- **Parked state:** i047 removed from the schedule (mk sed reverted; confboot
  asserts 6 again) so the gate is green while the keystone stays in-tree.
  Verified on this tree: host `make test` green and `PASS: target/positive`
  (k4-positive.log, carve + NS-bank fix included). The confboot-at-6 rerun on
  the parked schedule is PENDING — the box dropped offline mid-wrap; run
  `tests/target/run_m33mu_scenario.sh confboot` when it returns.

### M33MU-2 (candidate, unconfirmed): peripheral-IRQ NVIC delivery unproven

- P4.2/i021 needs a USART peripheral NVIC line delivered to the NS guest;
  SysTick works, peripheral IRQ delivery has never been exercised. To be
  probed when P4.2 starts (own feasibility gate; task #59).

## Phase gate rule

Every implementation phase must repeat host tests and the complete M33MU
matrix on its exact phase commit. When target hardware is available, the same
phase must additionally record a physical-board smoke result covering the
phase behavior.
