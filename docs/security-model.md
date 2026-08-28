# Security model

This page describes what the current STM32H563 source enforces. It does not
treat a manifest setting or a successful build as proof by itself.

## TrustZone boundary

Armv8-M TrustZone-M is the outer boundary. wolfTrust runs Secure; Zephyr and
FreeRTOS run Non-secure. The H563 port programs SAU and GTZC attribution, and
the compiler-generated CMSE gateway checks that guest buffers are both
Non-secure and inside the active guest's declared window.

The secure MPU adds narrower domains for unprivileged scheduled partitions.
Today that hardware confinement applies to the ITS and PS service loops. HSM,
vault, and FWU are scheduled but privileged because their backends currently
need broad Secure access. Attestation is dispatched inline. Those loops rely
on SPM policy and code structure, not the same per-partition MPU boundary.
The manifest's isolation-profile value records the intended Level 3-style
policy; it does not erase these current enforcement differences.

Source:
[`port/stm32h563/platform_stm32h563.c`](../port/stm32h563/platform_stm32h563.c)
and
[`src/arch/armv8m/spm_svc.c`](../src/arch/armv8m/spm_svc.c).

## One mediated service door

The normal service image exposes five `WolfTrust_FFM_*` veneers: framework
version, service version, connect, call, and close. The gateway stamps caller
identity from the active guest and validates copied input and output vectors
before the SPM uses them.

This boundary has two build-time backstops:

- the secure link runs `nm` and rejects Non-secure-callable symbols outside
  the FF-M whitelist
- both reference guest builds reject retired direct HSM and attestation
  veneers and require `wt_hsm_psa_transport_cb`

These checks are in
[`mk/secure-armv8m-stm32h563.mk`](../mk/secure-armv8m-stm32h563.mk),
[`build_guest.sh`](../tests/firmware/zephyr-stm32h5/scripts/build_guest.sh),
and
[`build_freertos_guest.sh`](../tests/firmware/zephyr-stm32h5/scripts/build_freertos_guest.sh).

The optional VNET build is a stated exception: it adds dedicated VNET veneers
to that whitelist. They are outside the FF-M service path.

## Per-guest keys and crypto

The reference guests enter crypto through the PSA Crypto API. wolfPSA routes
wolfCrypt operations to a crypto callback, the wolfHSM client packs a wire
request, and the transport sends exactly one `psa_call` to `SERVICE_HSM` per
packet.

The CMSE gateway stamps guest identity. The HSM relay maps that identity to one
wolfHSM server per guest, and the server uses `guest + 1` as its key namespace.
A guest-supplied wolfHSM client ID does not choose the namespace. Private keys
can be stored as non-exportable objects in the shared secure NVM backend.

Source:
[`src/client/hsm_psa_transport.c`](../src/client/hsm_psa_transport.c) and
[`src/services/wolfhsm/wt_hsm.c`](../src/services/wolfhsm/wt_hsm.c).

## Storage

ITS and PS forward storage operations to the Secure-only vault. The storage
partition identity and the SPM-stamped end-client identity form the object
namespace, so the same UID can refer to different objects for different
guests.

PS adds AES-256-GCM sealing. A device-local key is generated and kept in the
wolfHSM NVM store. Each sealed write advances a persistent counter before the
ciphertext is stored; that counter supplies the nonce and is also bound to the
live object slot. Old ciphertext or a modified tag fails closed. Write-once
objects cannot be replaced or removed.

Source:
[`src/services/wolfhsm/wt_hsm_vault.c`](../src/services/wolfhsm/wt_hsm_vault.c)
and
[`src/services/wolfhsm/wt_hsm_seal.c`](../src/services/wolfhsm/wt_hsm_seal.c).

## Authenticated guest launch

The image assembly tools place each guest's SHA-256 digest, size, and version
in a record covered by the wolfTrust signature. Before a guest enters its
domain, wolfTrust checks that record against the executable window and the
manifest's minimum version.

Version floors are persisted in wolfHSM-backed NVM and enforced in locked
lifecycles. A failed guest check quarantines that guest. An unreadable or
failed wolfTrust image floor quarantines all guests. The same hash check can be
requested after boot; a mismatch then quarantines the running guest.

Source: [`src/guest_verify.c`](../src/guest_verify.c),
[`src/rollback.c`](../src/rollback.c), and
[`src/monitor.c`](../src/monitor.c).

## Fault recovery

Restart action, budget, and window come from the manifest. For a recoverable
scheduled-partition fault, the SPM releases HSM locks, completes pinned calls
with a communication error, scrubs the partition stack, and reinitializes its
coroutine in place. Exhausted or platform-fatal policy escalates instead.

Connections involved in a fault enter an error state and cannot be reused.
The restarted service accepts new connections; the reference HSM client also
retries initialization if it first connects during a recovery window. The
current code does not promise transparent reconnection of every existing
client connection.

Source: [`src/sp_recovery.c`](../src/sp_recovery.c) and
[`src/arch/armv8m/spm_svc.c`](../src/arch/armv8m/spm_svc.c).

## Where the evidence comes from

| Claim | Enforcement | Main evidence |
| --- | --- | --- |
| Secure versus Non-secure | TrustZone, SAU, GTZC, CMSE | M33MU negatives and H563 silicon runs |
| FF-M service entry path | CMSE veneers and SPM identity | link/guest symbol guards and target calls |
| Unprivileged partition boundary | secure MPU | cross-domain target scenario |
| Key namespace | SPM identity and per-guest wolfHSM server | host key-isolation suite and target crypto negatives |
| Sealed PS | AES-GCM, counter table, owner namespace | host sealed-storage suite and target storage scenarios |
| Guest launch and rollback | pinned hash, version floor, quarantine | host checks and target negative scenarios |
| Restart behavior | manifest policy and recovery state machine | host orchestration suite and M33MU fault scenario |

See [testing](testing.md) for the commands and for the emulator/silicon split.
