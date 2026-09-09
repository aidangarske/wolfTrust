# wolfTrust

wolfTrust is a Secure Partition Manager (SPM) for Armv8-M TrustZone systems.
In the reference STM32H563 chain, wolfBoot authenticates wolfTrust. TrustZone
isolates wolfTrust and its Secure services from Non-secure guests, while STM32
GTZC attribution isolates each guest's RAM. wolfTrust mediates interprocess
communication (IPC) and service access through the Arm Platform Security
Architecture (PSA) Firmware Framework for M (FF-M), including cryptography,
storage, attestation, firmware update, and optional virtual networking.

## Key Features

| Feature | Description |
| --- | --- |
| Authenticated chain | wolfBoot authenticates the Secure image. Signature-covered guest records bind each guest ID to its image size, version, and SHA-256 digest before launch. |
| One Non-secure gateway | Exactly five `WolfTrust_FFM_*` veneers expose FF-M framework version, service version, connect, call, and close operations. |
| Caller-bound IPC | The gateway derives the PSA client identity from the active guest, copies vector descriptors, checks every range, and enforces manifest access policy. |
| Hardware-backed isolation | TrustZone attribution and the Secure MPU protect wolfTrust and Secure Partition writable state. STM32 GTZC MPCBB attribution isolates guest RAM. Reference guests run privileged Non-secure code, so the Non-secure MPU and interrupt masks are scheduling policy rather than adversarial boundaries; peer flash remains readable, and WRP plus `WT_GUEST_FLASH_WRP=1` protects its integrity. |
| PSA cryptography | Zephyr and FreeRTOS reference guests call wolfPSA's PSA Crypto API; operations are mediated to wolfCrypt and per-guest wolfHSM namespaces. |
| Secure services | Connection-based services provide attestation, hardware security module (HSM) access, Internal Trusted Storage (ITS), Protected Storage, firmware update, and an optional wolfIP virtual Ethernet switch. |
| Fault containment | A guest fault either restarts the guest within policy limits or leaves it quarantined. A Secure Partition fault releases synchronization state before failing affected calls. Restart paths scrub declared private writable memory before rearming; forbidden, exhausted, or failed recovery escalates to the port's fail-closed path. |
| Static Secure memory | The Secure image is built with `WOLFSSL_NO_MALLOC` and `NO_WOLFSSL_MEMORY`; service buffers, stacks, and state are statically allocated. |

## Documentation

| Page | Contents |
| --- | --- |
| [[Getting Started]] | Prerequisites, checkout, first builds, emulator use, and hardware entry points |
| [[Architecture]] | Boot flow, isolation layers, FF-M IPC, services, and scheduling |
| [[Security Model]] | Trust boundaries and enforced security properties |
| [[Threat Model]] | Protected assets, attacker capabilities, controls, and residual risks |
| [[API Reference]] | PSA client, service, storage, update, lifecycle, attestation, and gateway APIs |
| [[Services]] | Behavior and access policy for each Secure service |
| [[TF-M Compatibility]] | Supported interfaces, intentional differences, and migration guidance |
| [[Macros]] | Supported build and manifest configuration |
| [[Porting]] | Architecture and target port contracts |
| [[Building]] | Build targets, outputs, and cross-build options |
| [[Testing]] | Host, M33MU, and STM32H563 validation |
| [[Project Structure]] | Repository layout |
| [[STM32H5 Guide]] | STM32H563 provisioning, flashing, WRP, and recovery safety |
