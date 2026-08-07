# Arm PSA FF conformance adapter

This adapter compiles selected upstream Arm PSA Firmware Framework tests
without modifying their source. The exact repository revision is stored in
`tests/upstream/psa-arch-tests.rev`.

Run the current conformance gate from the repository root:

```sh
make test-conformance
```

The current host gate executes upstream tests `i001`, `i004` through `i008`,
`i012`, `i024`, `i025`, `i067`, `i071`, and `i088`. These cover framework and
service versions, invalid service IDs, strict and relaxed version policies,
Secure-only access policy, a successful Secure connection lifecycle, closing
and calling with an invalid or null handle, memory manipulation, and RoT
lifecycle state. `i067` reports SKIP: it requires SP heap allocation support,
which wolfTrust does not advertise.

Every other `ff/ipc` test in the pinned suite was evaluated and is currently
blocked on one of: real reboot continuity (`set_boot_flag`/boot-signature
across a reset), real multi-partition memory isolation, real interrupt
delivery, or (for `i002/i003/i010/i011/i026/i048-i053/i058/i063/i090`) an
`UNSPECIFIED` service version policy wolfTrust's `WT_SERVICE_VERSION_*` enum
does not yet implement, or (for `i027`) server-side per-service dispatch logic
our generic `test_dispatch()` does not yet provide. See task-list.md Phase 3
item 10.

This focused host gate is not the complete Arm architecture suite. The full
suite requires its Non-secure application and three Secure test partitions to
run through wolfTrust's production Armv8-M SPM and isolation path. Phase 3 is
not conformant until those binaries execute under M33MU and every applicable
test result is recorded.

wolfTrust's independent implementation tests live in `tests/host/ffm`. They
cover properties outside the selected upstream cases, including forged and
stale handles, caller ownership, bounded pools, message scrubbing, vector
limits, and output-pointer revalidation.
