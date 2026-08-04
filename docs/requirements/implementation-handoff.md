# Clean implementation handoff

## Context boundary

Start implementation in a new workspace session that has not received TF-M
Secure implementation source or summaries of its internal design. Supply only:

- `docs/clean-room-development.md`
- `docs/requirements/`
- Approved public specifications from `sources.md`
- The wolfTrust repository and approved wolfSSL project dependencies

Do not supply prior conversation transcripts or external TF-M review notes.

## First implementation slice

Create an architecture-neutral domain and resource model with host tests.

The slice must:

1. Define domain IDs, domain classes, Root of Trust roles, security state,
   privilege state, memory resources, interrupt resources, and restart policy.
2. Validate IDs, address overflow, empty regions, write-plus-execute mappings,
   entry points, stack bounds, resource overlap, shared resources, interrupt
   ownership, and profile capability limits.
3. Keep architecture context types outside the common domain descriptor.
4. Preserve source compatibility for the current H563 guest monitor through
   temporary guest aliases.
5. Add a separate host-domain test job, compiler-matrix coverage, sanitizer
   coverage, Valgrind coverage, and the existing Cortex-M33 cross-build.
6. Make no behavioral change to the current wolfBoot to wolfTrust to guest
   chain.

After this slice passes, implement the manifest intermediate representation
and generator from the approved FF-M requirements. The SPM and IPC code begins
only after those requirements have complete source-section references.
