# Compatibility requirements

## Product boundary

The released product contains wolfBoot, wolfTrust, selected wolfSSL project
dependencies, and the product application. It contains no TF-M runtime or
generated build product.

An existing standards-compliant application replaces its TF-M provider with
the wolfTrust client library and retains its `psa_*` calls. Build and board
configuration changes are permitted. Application security behavior must not
be rewritten.

## Initial release baseline

| Area | Required baseline |
| --- | --- |
| Framework | FF-M 1.0 IPC plus the selected FF-M 1.1 features recorded in `framework.md`, with Level 3 isolation |
| Crypto | PSA Crypto API 1.5 |
| Storage | PSA Secure Storage API 1.0 |
| Attestation | PSA Attestation API 2.0 with 1.0 compatibility |
| Firmware update | PSA Firmware Update API 1.0 |
| Status | PSA Status Code API 1.0 |
| Boot | wolfBoot authenticates wolfTrust; wolfTrust authenticates external domains |
| Reference platform | STM32H563 with M33MU and hardware |
| Portability proof | NUCLEO-C5A3ZG without common-core or service changes |

## Security profiles

- Level 3 isolates each Secure Partition from every other partition and keeps
  the privileged SPM protected from all partitions and Non-secure domains.
- Level 2 separates PSA Root of Trust and Application Root of Trust domains.
- Level 1 separates Secure and Non-secure processing environments.
- Service-only builds provide no unsupported isolation claim.

A build fails when the selected profile exceeds declared architecture or
platform enforcement capabilities.

## Framework compatibility policy

The first production profile implements the complete FF-M 1.0 IPC programming
model. It also exposes FF-M 1.1 framework and isolation discovery, explicit
IPC manifest selection, and stateless services after their individual gates
pass. SFN partitions remain a later compatibility feature, but the manifest
intermediate representation reserves the model distinction now so adding SFN
does not change the common SPM boundary.

Memory-mapped IOVEC support is disabled in the initial Level 3 profile.
Services use SPM-mediated `psa_read`, `psa_skip`, and `psa_write` transfers so
client memory is never directly mapped into a service partition. A future
port may enable memory-mapped IOVEC only with an architecture-specific proof
that the selected isolation profile remains enforced.
