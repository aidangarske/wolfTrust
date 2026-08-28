# Architecture

wolfTrust is a secure runtime for Armv8-M systems with TrustZone-M. It takes
the place normally occupied by TF-M: wolfBoot authenticates the secure image,
wolfTrust hosts the FF-M Secure Partition Manager (SPM) and services, and
Non-secure operating systems use PSA and FF-M client APIs.

The STM32H563 port is the reference implementation. Zephyr and FreeRTOS are
the reference Non-secure guests.

## Boot chain

1. wolfBoot authenticates the signed wolfTrust image. Its handoff carries the
   image measurement, version, and device lifecycle.
2. wolfTrust validates the generated manifest, starts the SPM and service
   dispatchers, and initializes wolfHSM-backed state.
3. Before entering a guest, wolfTrust hashes its executable window and checks
   the result and version against the record pinned into the signed wolfTrust
   image.
4. A verified Zephyr or FreeRTOS guest enters its Non-secure domain and uses
   the shared, operating-system-neutral FF-M client core.

The authenticated-chain assembly is visible in
[`tests/target/run_m33mu_scenario.sh`](../tests/target/run_m33mu_scenario.sh).
Guest launch checks live in
[`src/guest_verify.c`](../src/guest_verify.c) and
[`src/monitor.c`](../src/monitor.c).

## One service path

For the normal service build, every request from a Non-secure guest crosses
the same FF-M gateway. The gateway derives the caller from the active guest;
the guest does not supply its own identity.

```text
Non-secure guest (Zephyr or FreeRTOS)
        |
        | PSA client call
        v
WolfTrust_FFM_* CMSE gateway
        |
        | caller identity + checked, copied vectors
        v
Secure Partition Manager
        |
        | manifest policy, handles, queues, dispatch
        v
partition dispatcher
        |
        +--> wolfHSM server / key store / entropy
        +--> vault / sealed NVM
        +--> flash / wolfBoot update partition
```

The CMSE veneers are in
[`src/arch/armv8m/ffm_nsc.c`](../src/arch/armv8m/ffm_nsc.c). The SPM runtime
and service registration are in
[`src/ffm.c`](../src/ffm.c) and
[`src/ffm_boot.c`](../src/ffm_boot.c).

The default secure build exports only the `WolfTrust_FFM_*` veneers. A linked
symbol check rejects any other Non-secure-callable veneer. The Zephyr and
FreeRTOS guest builds also reject the retired direct HSM and attestation
veneers and require the PSA-mediated HSM transport.

Virtual networking is an optional, separate data path. Enabling
`CONFIG_VNET=y` deliberately adds `WolfTrust_VNet_*` veneers to the build
whitelist. See the [VNET integration notes](vnet/integration_notes.md).

## Services

[`port/stm32h563/manifest.json`](../port/stm32h563/manifest.json) is the
authoritative service list for the reference port.

| Service | Job | Non-secure access |
| --- | --- | --- |
| `SERVICE_ATTEST` | EAT claims in a wolfCOSE `COSE_Sign1`; the IAK operation stays in wolfHSM | Yes |
| `SERVICE_HSM` | Opaque relay for guest wolfHSM packets and secure crypto | Yes |
| `SERVICE_ITS` | Internal Trusted Storage, namespaced by caller | Yes |
| `SERVICE_PS` | AES-GCM sealed storage with rollback binding | Yes |
| `SERVICE_FWU` | Stages and arms an image for wolfBoot's update flow | Yes |
| `SERVICE_VAULT` | Key, object, counter, and random backend for Secure callers | No |

The reference guest crypto path is:

```text
PSA Crypto -> wolfPSA -> wolfCrypt crypto callback -> wolfHSM client
           -> one psa_call per wolfHSM packet -> SERVICE_HSM -> wolfHSM server
```

The SPM-stamped client identity selects a per-guest wolfHSM server. Each
server has its own key namespace while sharing the secure NVM backend.

## Current partition execution

The manifest describes six Secure Partitions and selects FF-M isolation
profile 3. That is the policy target, not proof that every partition has the
same hardware boundary. On the current STM32H563 path:

- ITS and PS run as unprivileged scheduled loops with secure-MPU domains
- HSM, vault, and FWU run as scheduled privileged loops because their current
  backends access monitor, NVM, or flash state
- attestation is registered as a partition dispatcher but is currently called
  inline rather than started as a scheduled partition loop

That distinction matters. TrustZone enforces the Secure/Non-secure boundary,
and the secure MPU confines the unprivileged partition loops. The current
source does not support a blanket claim that every service loop has identical
hardware isolation from all other Secure code. See the
[security model](security-model.md) for the exact claim boundaries.

## Port boundary

The neutral core owns manifests, FF-M IPC, lifecycle, services, and policy.
`src/arch/armv8m/` owns the Armv8-M gateway and context mechanics.
`port/stm32h563/` owns SAU, GTZC, MPU, interrupt, flash, entropy, and board
details.

See the [port contract](port-contract.md) and
[adding a port](adding-a-port.md) for the longer version.
