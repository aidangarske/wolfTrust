# Arm PSA FF conformance adapter

This adapter compiles selected upstream Arm PSA Firmware Framework tests
without modifying their source. The exact repository revision is stored in
`tests/upstream/psa-arch-tests.rev`.

Run the current conformance gate from the repository root:

```sh
make test-conformance
```

The current host gate executes upstream tests `i001` and `i004` through
`i008`. These cover framework and service versions, invalid service IDs,
strict and relaxed version policies, Secure-only access policy, and a
successful Secure connection lifecycle.

This focused host gate is not the complete Arm architecture suite. The full
suite requires its Non-secure application and three Secure test partitions to
run through wolfTrust's production Armv8-M SPM and isolation path. Phase 3 is
not conformant until those binaries execute under M33MU and every applicable
test result is recorded.

wolfTrust's independent implementation tests live in `tests/host/ffm`. They
cover properties outside the selected upstream cases, including forged and
stale handles, caller ownership, bounded pools, message scrubbing, vector
limits, and output-pointer revalidation.
