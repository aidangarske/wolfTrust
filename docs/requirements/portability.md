# Portability requirements

## Port model

wolfTrust separates architecture, MCU family, board, operating-system client,
and Secure service code.

| Layer | Responsibility | Expected reuse |
| --- | --- | --- |
| Common core | Domains, capabilities, manifests, scheduling policy, IPC state, lifecycle, verification policy | All architectures |
| Architecture | Context format, privilege transition, exception entry, memory enforcement, interrupt delivery | All devices with the same architecture |
| MCU family | Security controller, flash, entropy, identity, lifecycle, counters, reset | All boards using that MCU family |
| Board description | Memory layout, image slots, external storage, assigned peripherals and interrupts | One board or compatible board group |
| OS client | Thread safety, task identity, startup, and build integration | All boards using that OS |
| Secure service | Crypto, storage, attestation, firmware update, and Platform behavior | All supported devices |

## Requirements

| ID | Behavior | Failure | Rationale | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- | --- |
| WT-PORT-0001 | Common core code contains no CMSE, MPU-count, STM32-register, or 32-bit context assumptions. | Architecture contract or 64-bit host build failure blocks integration. | Preserve a future AArch64 frontend. | WOLFTRUST-ORIGINAL | Host 64-bit and Armv8-M builds | |
| WT-PORT-0002 | An architecture port supplies context switching, privilege control, memory enforcement, exception handling, and interrupt delivery. | Missing mandatory operations fail at link or generation time. | Keep hardware execution mechanics outside common policy. | WOLFTRUST-ORIGINAL | Architecture contract tests | |
| WT-PORT-0003 | An MCU-family port supplies security attribution, flash, entropy, identity, lifecycle, counters, and reset. | A selected service fails to build when its required capability is absent. | Keep vendor-specific hardware outside services. | WOLFTRUST-ORIGINAL | Platform capability tests | |
| WT-PORT-0004 | A board description declares all executable, RAM, MMIO, interrupt, storage, and image-slot resources. | Overlap, omission, unsupported sharing, or alignment errors fail before signing. | Generate a complete static security policy. | WOLFTRUST-ORIGINAL | Board-description validator | |
| WT-PORT-0005 | Board data is converted into a static generated policy covered by the wolfTrust signature. | Runtime mutation or digest mismatch prevents boot. | Avoid unaudited runtime discovery of security boundaries. | WOLFTRUST-ORIGINAL | Policy digest and tamper tests | |
| WT-PORT-0006 | Zephyr, FreeRTOS, and bare-metal clients expose the same PSA calls and transport semantics. | An adapter-specific behavior difference fails shared client tests. | Keep application behavior OS-independent. | WOLFTRUST-ORIGINAL | Shared client conformance suite | |
| WT-PORT-0007 | The C5 port requires no common-core or Secure-service modifications after H563 support is complete. | Any such change must be classified as a core defect before the port can pass. | Prove the port boundary rather than adding a second special case. | WOLFTRUST-ORIGINAL | H563 and C5 diff audit | |
| WT-PORT-0008 | Unsupported boards may build lower named profiles only when their actual enforcement capabilities match the profile. | Capability mismatch fails the build and cannot be suppressed by a board description. | Keep portability claims honest. | WOLFTRUST-ORIGINAL | Negative profile-selection tests | |

## H5 to C5 replacement flow

1. Remove TF-M sources, generated clients, and the TF-M Secure image.
2. Retain standards-compliant application `psa_*` calls.
3. Select the wolfTrust PSA client and the board description.
4. Generate the linker layout, attribution policy, manifests, and image slots.
5. Build wolfBoot, wolfTrust, and the existing application.
6. Sign wolfTrust and external application images with wolfBoot tooling.
7. Flash wolfBoot, wolfTrust, and the application slots.
8. Run generated isolation self-tests and PSA conformance tests.

The C5 reuses the Armv8-M architecture implementation and common STM32
support. Its MCU-family layer supplies C5-specific attribution, flash,
identity, entropy, lifecycle, counter, and reset behavior. Its board
description supplies the NUCLEO-C5A3ZG resource layout.
