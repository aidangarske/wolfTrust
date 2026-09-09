# Architecture

wolfTrust is the Secure Partition Manager between an authenticated bootloader
and one or more Non-secure guests. The STM32H563 reference configuration runs
two guests on one Cortex-M33 and exposes Secure services only through FF-M IPC.

## Software stack

| Layer | Role |
| --- | --- |
| wolfBoot | Performs the BL2 secure-boot role, authenticates wolfTrust, passes the measured-boot handoff, and swaps authenticated update images. |
| wolfTrust | Configures isolation, validates the manifest and guest images, schedules guests and Secure Partitions, implements FF-M IPC, and manages lifecycle and recovery. |
| wolfPSA | Implements PSA Crypto entry points over wolfCrypt for the Non-secure reference guests. |
| wolfCrypt | Supplies cryptographic implementations used by wolfPSA and Secure services. |
| wolfHSM | Owns protected key operations and the persistent NVM backend. |
| wolfCOSE | Encodes and signs COSE_Sign1 attestation tokens. |
| wolfIP | Supplies guest networking used by the optional Secure virtual Ethernet switch. |

## Boot flow

1. wolfBoot verifies the wolfTrust image and transfers a measured-boot record
   containing the Secure image measurement, lifecycle, and active image
   version.
2. wolfTrust configures the target's TrustZone, GTZC, Secure MPU, interrupt,
   and memory policies; validates and binds the generated manifest; registers
   the FF-M services; seeds each guest context; and performs an initial
   signed-record, image-size, manifest-version, digest, and optional WRP check.
3. wolfTrust consumes the measured-boot handoff, initializes wolfHSM and the
   persistent vault backends, and checks the persistent Secure-image and guest
   version floors.
4. wolfTrust starts the scheduled Secure Partitions.
5. Immediately before an accepted guest's first dispatch, the monitor repeats
   its signed-record, image-size, manifest-version, digest, and optional WRP
   checks, then installs that guest's active GTZC, Non-secure MPU, and interrupt
   policy.
6. The monitor begins time-sliced Non-secure execution.

The image assembly and signing order matters: guest records are patched into
`wolftrust.bin` before wolfBoot signs it. See
`tools/measure/patch_guest_digests.py` and the target runners.

## Isolation layers

The reference port combines several mechanisms:

- The Armv8-M SAU/IDAU boundary separates Secure and Non-secure address space.
- STM32 GTZC MPCBB attribution prevents an inactive guest from accessing the
  active guest's RAM through a Non-secure MPU bypass.
- The monitor replaces the Non-secure MPU and allowed interrupt set on each
  guest switch. Reference guests currently run privileged Non-secure code and
  can reprogram those two controls, so they are scheduling policy rather than
  adversarial boundaries.
- CMSE checks require every request pointer to be Non-secure and inside the
  active guest's declared readable or writable window.
- The Secure MPU gives each Secure Partition thread shared read/execute image
  text, read-only constants, its private stack and data, and only its declared
  shared resources.

All shipped service loops are scheduled as unprivileged Secure coroutines.
Operations requiring wider Secure access, including flash programming, entropy,
NVM locking, and platform reset, trap through privileged SVC handlers that
verify the calling partition and operation.

The single-image design shares executable text among Secure Partitions. The
MPU isolates writable state, not code identity; this is an explicit difference
from separately linked partition images.

Both guest images share one Non-secure flash attribution window, so a privileged
guest can read peer flash. WRP plus `WT_GUEST_FLASH_WRP=1` protects guest-flash
integrity but not confidentiality. Peripheral and Non-secure NVIC attribution
are deployment responsibilities; see [[Threat Model]].

## FF-M request path

A Non-secure client calls a standard PSA API or the generic FF-M client API:

```text
guest API
  -> WolfTrust_FFM_Connect / Call / Close
  -> CMSE and active-guest window validation
  -> SPM-owned connection and copied vectors
  -> scheduled Secure Partition
  -> SVC-gated backend when privileged hardware access is required
  -> copied reply
```

The gateway never accepts a caller ID from the guest. It derives guest
`N` as PSA client ID `-(N + 1)`. Handles carry ownership and
generation state, and the SPM rejects stale, cross-owner, or invalid
transitions.

The link step runs `arm-none-eabi-nm` and requires exactly five
Non-secure-callable symbols:

- `WolfTrust_FFM_FrameworkVersion`
- `WolfTrust_FFM_ServiceVersion`
- `WolfTrust_FFM_Connect`
- `WolfTrust_FFM_Call`
- `WolfTrust_FFM_Close`

Any additional `__acle_se_*` symbol, or a missing expected symbol,
fails the build.

## Secure Partition scheduling

Connection-based services use `psa_wait`, `psa_get`,
`psa_read`, `psa_write`, and `psa_reply`. Each
service has a statically allocated coroutine stack and blocks until its signal
or interrupt is ready. The dispatcher continues running ready partitions until
the system is quiescent, which permits partition-to-partition IPC without
executing one partition on another partition's stack.

A Secure Partition fault first releases held synchronization state and then
completes its pinned client call with a communication failure. If policy selects
a restart, wolfTrust scrubs declared private writable memory before rearming the
partition. A forbidden or exhausted restart, or a failed rearm, escalates to
the port's fail-closed path.

## Guests and lifecycle

The monitor time-slices the configured Non-secure guests. A guest fault masks
its interrupts and applies the restart policy. If restart is allowed, wolfTrust
clears restart-marked RAM and restores a clean context after a bounded delay; if
the budget is exhausted, the guest remains faulted. Runtime remeasurement can
also quarantine a guest without entering it.

The PSA lifecycle value comes from the authenticated boot handoff. Scheduled
Secure Partitions can read it through `psa_rot_lifecycle_state()`; there is no
Non-secure client adapter or CMSE veneer for that function. The value also controls
whether persistent storage may self-recover from an unknown or corrupt NVM
pool: assembly-and-test and provisioning may reformat, while every other
lifecycle value fails closed.

See [[Security Model]], [[Services]], and [[Threat Model]] for the security
properties built on this design.
