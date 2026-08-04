# Clean-room development policy

## Purpose

wolfTrust provides an independent implementation of published PSA and
Firmware Framework for M interfaces. Product code must be traceable to an
approved public specification or an original wolfTrust requirement. It must
not be derived from Trusted Firmware-M implementation source.

This policy applies to the Secure Partition Manager, Secure services,
isolation layer, client transport, manifest tooling, platform ports, and
generated files.

## Roles

The clean-room process separates three roles:

1. Requirements reviewers may read approved specifications, public client
   examples, conformance tests, and observable compatibility results. They
   write requirements without reproducing implementation details.
2. Implementers receive only approved specifications, wolfTrust requirements,
   public API headers, and wolfSSL project sources. They must not inspect TF-M
   Secure implementation source.
3. Compatibility auditors run external test suites, compare observable
   behavior, and perform provenance and source-similarity checks. They report
   test inputs, expected results, and failures without providing reference
   implementation code to implementers.

One person or automated agent must not act as an implementer after reviewing
prohibited source. A new implementation context must be created when exposure
has occurred.

## Approved inputs

Implementers may use:

- Published GlobalPlatform and Arm PSA API specifications.
- The published PSA Firmware Framework and FF-M extension specifications.
- Published Arm architecture manuals and CMSIS interface definitions.
- Vendor data sheets, reference manuals, errata, and device headers.
- Public PSA reference headers whose licenses permit redistribution.
- Public Non-secure example applications that only consume specified APIs.
- Public Secure Partition examples limited to specified API usage and
  documented manifest input.
- wolfTrust requirements carrying an approved provenance record.
- wolfSSL, wolfBoot, wolfPSA, wolfHSM, wolfCOSE, wolfPKCS11, wolfHAL, and other
  explicitly approved project sources.

Public TF-M examples and tests are compatibility consumers. Their presence on
this list does not permit reading the TF-M implementation that satisfies them.

## Prohibited inputs

Implementers must not inspect or adapt:

- TF-M Secure Partition Manager implementation source.
- TF-M scheduler, IPC backend, isolation HAL, or context-switching source.
- TF-M Crypto, storage, attestation, firmware-update, or Platform service
  implementation source.
- TF-M platform initialization, linker templates, generated implementation
  source, or internal build logic.
- Patches, reviews, summaries, or generated output that reproduce prohibited
  implementation details.

No TF-M source, binary, generated client, build artifact, or Secure service is
shipped in a wolfTrust product package.

## Requirement provenance

Every normative requirement is recorded in the requirements register with:

- A stable requirement identifier.
- Observable behavior and failure behavior.
- Security rationale.
- Public source title, version, section, and URL, or `WOLFTRUST-ORIGINAL`.
- API and conformance tests that verify the requirement.
- Implementing commit and reviewing auditor.

Requirements derived from compatibility testing describe only public inputs,
outputs, status codes, and state transitions. They must not describe how TF-M
implements that behavior.

## Implementation controls

- New implementation work begins in a context with no prohibited source
  history.
- Commits remain scoped to one requirement group and include corresponding
  tests.
- Public constants and structures are imported only from approved reference
  headers, with their original license retained when required.
- Generated sources include the generator version and input digest.
- Security-sensitive failures are fail-closed and are covered by negative
  tests.
- Reviewers reject unexplained source similarity, copied comments, copied
  control flow, or undocumented constants.

## Release evidence

A release claiming independent implementation requires:

- A complete requirements and provenance register.
- Passing PSA and FF-M conformance results.
- A dependency and license inventory.
- A source-similarity audit performed outside the implementation lane.
- Security review with no unresolved HIGH or MEDIUM findings.
- Confirmation that release artifacts contain no TF-M implementation or build
  products.
