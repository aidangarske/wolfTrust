# Approved source register

Only sources recorded here may be supplied to the clean-room implementation
lane. Pin an exact published version and archive digest before using a source
for normative requirements.

| ID | Source | Intended use | Status |
| --- | --- | --- | --- |
| SRC-PSA-CRYPTO | PSA Crypto API 1.5 | Crypto client and service behavior | Approved, digest pending |
| SRC-PSA-STORAGE | PSA Secure Storage API 1.0 | ITS and Protected Storage behavior | Approved, digest pending |
| SRC-PSA-ATTEST | PSA Attestation API 2.0 | Attestation client and service behavior | Approved, digest pending |
| SRC-PSA-ATTEST-1 | PSA Attestation API 1.0 | Compatibility behavior | Approved, digest pending |
| SRC-PSA-FWU | PSA Firmware Update API 1.0 | Firmware update behavior | Approved, digest pending |
| SRC-PSA-STATUS | PSA Status Code API 1.0 | Common result codes | Approved, digest pending |
| SRC-FFM | Arm DEN 0063, PSA Firmware Framework for M 1.0, Release issue 0 | Client, service, IPC, and isolation behavior | Approved and pinned below |
| SRC-FFM-EXT | Arm AES 0039, Firmware Framework for M 1.1 Extension, Beta issue 0 | Extended framework behavior | Approved and pinned below |
| SRC-ARMV8M | Armv8-M architecture manuals | Context, privilege, exception, MPU, and TrustZone behavior | Approved, version and digest pending |
| SRC-STM32H5 | STM32H563 reference manual and errata | H563 platform port | Approved, version and digest pending |
| SRC-STM32C5 | STM32C5A3 reference manual and errata | C5 platform port | Approved, version and digest pending |
| SRC-PSA-TESTS | Published PSA API test suites | External conformance oracle | Compatibility lane only |
| SRC-TFM-NS-EXAMPLES | Public TF-M Non-secure examples | Source-level client compatibility | Compatibility lane only |
| SRC-TFM-TESTS | Public TF-M test applications | External compatibility oracle | Compatibility lane only |

The canonical PSA API publication index is
<https://arm-software.github.io/psa-api/>. Exact document URLs, versions,
licenses, and SHA-256 archive digests must replace the pending fields before
the corresponding implementation phase starts.

## Pinned framework sources

### SRC-FFM

- Title: Arm Platform Security Architecture Firmware Framework for M
- Document: DEN 0063
- Version: 1.0, Release issue 0
- Publication date: 19 June 2019
- URL: <https://documentation-service.arm.com/static/64a2ed35df6cd61d528c4132>
- SHA-256: `9db18f66ad9d89d33098386319fe15db61b4bdd84b8a1b0e3569ccf6f887ee80`
- Rights: Arm Non-Confidential document. Use is governed by the notices in
  the published PDF. No specification text is incorporated into product
  source.

### SRC-FFM-EXT

- Title: Arm Firmware Framework for M 1.1 Extension
- Document: AES 0039
- Version: 1.1 Extension, Beta issue 0
- Publication date: 18 January 2023
- URL: <https://documentation-service.arm.com/documentation/aes0039/latest>
- SHA-256: `010971c92d20adc70477eb0a8d9c5130e26acdc9133c39d5c23fed18723d3b55`
- Rights: Arm Non-Confidential document. Use is governed by the notices in
  the published PDF. No specification text is incorporated into product
  source.

The 1.0 Release specification is the normative baseline for the first IPC
implementation. The 1.1 Extension is a Beta publication and is used only for
the explicitly recorded extension requirements. A later published issue must
be reviewed and repinned before it changes release behavior.
