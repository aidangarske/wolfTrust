# Introducing **wolfTrust**

*An early look at a **wolfSSL**-native alternative to **Trusted Firmware-M**.*

**wolfTrust is a trusted firmware platform under development at wolfSSL that unifies the wolfSSL security stack, from secure boot and cryptography through key management, storage, and attestation, into one deterministic, standards-based secure runtime for Arm Cortex-M devices.** It follows the Arm Firmware Framework for M (FF-M) programming model and standard PSA interfaces so applications keep their `psa_*` source calls after rebuilding with the wolfTrust client library, building its isolation, IPC, lifecycle, and secure services natively on wolfCrypt and wolfHSM in place of the Secure-world runtime commonly supplied by Trusted Firmware-M (TF-M) and its configured cryptographic backend.

## A Complete wolfSSL Security Stack

wolfTrust integrates the standalone security layers embedded teams already use into one cohesive runtime:

- **wolfBoot** performs the BL2 secure-boot role and authenticates wolfTrust.
- **wolfTrust** supplies the Secure Partition Manager (SPM), isolation, IPC, lifecycle, and secure services.
- **wolfCrypt** provides the optimized cryptographic implementation.
- **wolfPSA** implements the Arm PSA Crypto API over wolfCrypt, preserving standard `psa_*` entry points.
- **wolfHSM** provides protected key and storage operations through a controlled vault boundary.
- **wolfCOSE** supports signed attestation tokens.
- **Zephyr, FreeRTOS, and bare-metal** applications act as Non-secure clients through familiar PSA interfaces.

wolfTrust is an independent implementation of the FF-M model with source-compatible PSA client interfaces as a central design goal: a standards-compliant application keeps its `psa_*` calls while replacing the trusted-firmware provider.

## Why **wolfTrust**?

- **Optimized embedded cryptography.** wolfCrypt offers architecture-specific optimizations and hardware-acceleration integrations; see [wolfSSL's benchmark guidance](https://www.wolfssl.com/docs/benchmarks/) for configuration-specific measurements.
- **A route to a validated wolfCrypt deployment.** Certificates #4718 and #5041 validate specific wolfCrypt modules and operating environments, not wolfTrust itself; a FIPS deployment must use the validated module, approved mode, and an applicable operating environment.
- **A post-quantum roadmap.** wolfCrypt offers ML-KEM, ML-DSA, SLH-DSA, LMS/HSS, XMSS/XMSS-MT, and experimental Falcon in separately configured builds; the current STM32H563 secure profile enables ECC P-256, AES, SHA-256, HMAC/HKDF, and HashDRBG and does not enable these PQC algorithms. CAVP #A8437 validates wolfCrypt Post Quantum v7.0.0 in its listed operating environment and does not validate Falcon.
- **One vendor behind the stack**, with commercial licensing, long-term maintenance, and 24x7 support from wolfSSL engineers.
- **Protected generated keys.** Keys generated in the Secure vault are stored non-exportable and their private-key operations execute there; a client importing private material still supplies it from caller memory before it is placed in protected storage.
- **A path to dedicated HSM hardware.** The same wolfHSM client-server model can place keys and sensitive cryptography on a separate HSM core such as Infineon AURIX TC3xx or TC4x, beyond TrustZone-only isolation.
- **Secure on-chip networking between isolated guests.** An optional wolfIP virtual network lets mutually isolated Non-secure guests exchange TCP/IP traffic guest-to-guest, entirely through the SPM.
- **Zero dynamic allocations.** The Secure image eliminates the heap (`WOLFSSL_NO_MALLOC`) for deterministic execution and a reduced attack surface.
- **Designed for embedded portability.** Architecture-neutral policy, service, IPC, and lifecycle code are separated from platform-specific isolation mechanisms.

## Beyond **TF-M**

The first focus is the Cortex-M trusted firmware role served today by TF-M, with the STM32H563 as the reference target. Planned work will also explore an AArch64 port and a wolfSSL-based replacement boundary for roles commonly supplied by Trusted Firmware-A (TF-A); an A-profile port would need its own architecture layer and validation. Reusing the manifest, service, IPC, and PSA contracts across profiles is a design goal rather than a current implementation, a path toward one wolfSSL-based trust architecture across microcontrollers and application processors.

We are currently welcoming early evaluations. If you have any questions about the above architecture, or if you would like to get started on integrating wolfTrust into your own embedded environment, please contact us at facts@wolfssl.com, or call us at +1 425 245 8247.
