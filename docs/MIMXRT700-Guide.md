# MIMXRT700 Guide

This guide covers the NXP MIMXRT700-EVK (MIMXRT798S, compute Cortex-M33) as a
second Armv8-M reference port. Unlike the STM32H563, this part has no internal
program flash: it executes in place from an external octal SPI NOR on XSPI0, and
the boot chain, Secure image, and guests all live in that NOR. Changing the OTP,
debug-authentication policy, or product lifecycle can permanently lock the part.
Read the current state first and keep a development board recoverable.

## Status

- **Validated:** the wolfBoot first-stage loader on this silicon (signed boot
  with ECC256 and ML-DSA-87, update swap and rollback, boot-region protection,
  and the TrustZone measured handoff into a Secure payload), and the wolfTrust
  Secure image cross-build (`make TARGET=mimxrt700 secure-image`) with the port
  split, veneer, and manifest checks green.
- **In bring-up:** the wolfTrust Non-secure guest `positive` scenario on the
  EVK. The bare-metal `guest0` links and its Non-secure to Secure call path
  resolves; the on-silicon launch is the current step.

Record emulator, cross-build, and physical-board evidence separately. There is
no MIMXRT700 emulator, so all target evidence for this port is real silicon.

## What a MIMXRT700 port comprises

A full port spans two repositories. The first-stage loader changes live in
wolfBoot; the Secure runtime changes live in wolfTrust.

### First-stage loader (wolfBoot)

| Addition | Purpose |
| --- | --- |
| `hal/imx_rt7xx.{c,h,ld}` | LPUART0 debug console, and an XSPI0 octal-NOR driver (program, erase, read-modify-write). The part has no ROM flash API, so the flash path runs from RAM (`RAM_CODE`) through a deadline-bounded transaction layer, invalidating the XSPI cache after every write. |
| `config/examples/imx-rt700.config` | ECC256, TrustZone disabled: the plain-boot smoke configuration. |
| `config/examples/imx-rt700-tz.config` | TrustZone enabled with the generic Secure-application handoff (`WOLFBOOT_SECURE_APP`): wolfBoot writes the measured-boot record to Secure SRAM and stays in Secure state across the jump to the Secure runtime. |
| `config/examples/imx-rt700-mldsa.config` | ML-DSA-87 image signatures for a CNSA 2.0 boot chain. |
| Boot-region protection | Before handoff, wolfBoot programs and locks the XSPI Secure Flash Protection descriptors so the bootloader region is read-only to the application, and refuses to continue if the protection cannot be read back. |

The loader satisfies the [Porting](Porting.md) bootloader contract: it
authenticates the Secure image, provides `wt_boot_handoff_t` (SHA-256
measurement, lifecycle, image version) at the agreed Secure-RAM address,
reserves the Secure image header, and provides an update partition compatible
with the firmware-update backend.

### Secure runtime (wolfTrust)

The port reuses `src/arch/armv8m/` and `src/arch/common/` unchanged and adds
only `port/mimxrt700/` and one build fragment:

| File | Responsibility |
| --- | --- |
| `memory_map.h` | The bit-28 Secure-alias map: XSPI0 NOR windows, Secure and guest RAM, the boot-handoff address, and the per-partition RAM bands. |
| `mimxrt798_regs.h` | Register bases for CLKCTL, SYSCON, IOPCTL, LPUART0, XSPI0, TRNG, the AHBSC fabric controllers, and their GLIKEY unlock state machines. |
| `platform_mimxrt700.c` | Every `wt_platform_*` operation: clocks, the SAU table, the Secure MPU whitelist, the fabric memory windows, the boot-handoff region, fault logging, panic, and reset. |
| `partitions.c` | The guest and capability tables, the profile capability bitmap, and the pinned guest-measurement slot. |
| `hsm_flash.c/.h` | The `port_nvm.h` octal-NOR backend for the wolfHSM store (page program, 4 KiB erase, cache invalidate, foreign-media detection). |
| `rng_entropy.c` | The `CUSTOM_RAND_GENERATE_BLOCK` entropy source over the on-die TRNG, preserving the unprivileged-to-privileged trap. |
| `secure.ld` | The port's own Secure linker script (the runtime never shares a linker script across ports). |
| `manifest.json` | The service partitions (attestation, HSM, vault, ITS, PS, FWU) and their resources. |
| `mk/target-mimxrt700.mk` | `WT_CPU`, the flash and RAM defaults, the linker `--defsym` set, and the source lists. |

The only edit outside the port allow-list is a neutral seam: an `#ifndef` guard
around `WOLFHSM_CFG_FLASH_UNIT_SIZE` so the port can set the NOR write unit.

## Reference flash and RAM layout

XSPI0 octal NOR is aliased at `0x28000000` (Non-secure) and `0x38000000`
(Secure); compute-domain SRAM is `0x20000000` / `0x30000000`; peripherals are
`0x40000000` / `0x50000000`. Bit 28 selects the Secure alias.

| Image or region | Non-secure | Secure alias |
| --- | ---: | ---: |
| wolfBoot (FCB at flash + 0, boot header at + `0x4000`) | `0x28000000` | `0x38000000` |
| wolfTrust Secure image (`0x40000`) | `0x28040000` | `0x38040000` |
| Guest 0 (`0x80000`) | `0x28080000` | `0x38080000` |
| Guest 1 (`0x40000`) | `0x28100000` | `0x38100000` |
| wolfBoot update partition (`0x40000`) | `0x28180000` | `0x38180000` |
| wolfHSM NVM store | `0x281E0000` | `0x381E0000` |
| Conformance NVM store | `0x281E8000` | `0x381E8000` |

| RAM region | Address |
| --- | ---: |
| Guest 0 RAM (`0x40000`) | `0x20100000` |
| Guest 1 RAM (`0x40000`) | `0x20140000` |
| Boot-handoff record | `0x30180000` |
| Secure runtime RAM | `0x30188000` |

These constants come from `port/mimxrt700/memory_map.h` and
`mk/target-mimxrt700.mk`. Use the hardware runner for image assembly so the
build and flash addresses stay paired.

## TrustZone and fabric perimeter

The MIMXRT700 has no option-byte Secure watermark. The Secure boundary is set at
run time by three mechanisms the port programs before any guest launches:

- **IDAU/SAU:** the bit-28 alias makes each address inherently Secure or
  Non-secure; the SAU table in `platform_mimxrt700.c` opens the Non-secure
  windows (the guest flash and RAM, the shared console) and leaves everything
  else Secure.
- **Secure MPU:** a per-partition whitelist confines each Secure Partition to
  its own RAM band.
- **AHBSC fabric:** the AHB Secure Controller denies every other bus master
  (the sense M33, the DSPs, the NPU, and DMA) from the Secure SRAM and
  peripheral bands. Each controller instance is unlocked through its GLIKEY
  code-word sequence.

Only the compute Cortex-M33 (cpu0) is trusted; every other core and master is
fenced. A port declares what it actually enforces through the capability bitmap
in `partitions.c`, and the manifest validator refuses a domain that requires a
capability the port does not provide.

## Required tools

- MIMXRT700-EVK with its on-board MCU-Link (CMSIS-DAP) and USB serial
- `arm-none-eabi-gcc` with newlib headers, and `arm-none-eabi-{nm,objcopy,size}`
- NXP SPSDK (`nxpimage` for FCB and bootable-image assembly)
- pyOCD with MIMXRT798S pack support (flash and SWD inspection)
- Python 3
- wolfBoot key tools and a signing key for the Secure payload
- a hardware runner host that owns the probe, with a controllable reset line to
  the EVK, and a serial console (default `/dev/ttyACM0`)

## Build, flash, and verify

The hardware runner (`tests/target/run_rt700_hardware.sh`) drives image
assembly and flashing so the addresses stay paired. The `romsmoke` scenario
proves the BootROM XIP path; the `positive` scenario is the wolfTrust chain.

The full chain build and flash performs:

1. build wolfBoot for `imx-rt700-tz.config` and wrap it with the EVK FCB and a
   boot header (`nxpimage`);
2. build the wolfTrust Secure image and its CMSE import library
   (`make TARGET=mimxrt700 secure-image`);
3. build `guest0`, linked against the Secure image's CMSE import library so the
   `WolfTrust_FFM_*` veneers resolve;
4. patch the guest measurement records into the unsigned `wolftrust.bin`
   (`tools/measure/patch_guest_digests.py`);
5. sign `wolftrust.bin` with the wolfBoot key tools;
6. flash wolfBoot at `0x28000000`, the signed Secure image at `0x28040000`, and
   `guest0` at `0x28080000` over XSPI0 with verification;
7. reset the board through the runner's reset line, capture UART, and check the
   expected markers.

Because the Secure image pins each guest's SHA-256 before it is signed, an
unpatched or corrupted guest fails launch closed: the wolfBoot signature covers
the pinned digests, and the Secure port refuses a guest whose image does not
match its record.

### Bring-up markers

`guest0` records progress in an SWD-readable mailbox at the base of its
Non-secure RAM (`0x20100000`) and echoes it on LPUART0:

| Mailbox word | Address | Pass value |
| --- | ---: | ---: |
| signature | `0x20100000` | `0x47543030` |
| step | `0x20100004` | `0x00000005` |
| `psa_framework_version()` | `0x20100008` | `0x00000100` |
| `psa_connect(SERVICE_HSM)` handle | `0x20100010` | > 0 |
| status | `0x20100014` | `0x600D600D` |

A `status` of `0x600D600D` proves the wolfBoot to wolfTrust to Non-secure-guest
chain booted and that the Secure runtime serviced a Non-secure PSA client
through the veneers. `0xBAD00000` records a failed check at the `step` reached.

## Recovery rules

- If the BootROM does not run the image, confirm the FCB is present and the
  boot header offset matches the runner (`0x4000`).
- If wolfBoot rejects wolfTrust, confirm the guest measurement records were
  patched before signing and that the signing key matches the configured
  keystore.
- If wolfTrust refuses a guest, compare the built guest address and size with
  the manifest and inspect the signed measurement record.
- If the guest mailbox never leaves `0x00000000`, halt over SWD and sample the
  program counter: an identical value each time is a spin, not progress. Confirm
  the Secure image launched the Non-secure guest before assuming a veneer fault.
- Never reuse another NXP part's FCB, clock, or pin table without checking its
  reference manual and NOR geometry.

See [Porting](Porting.md) for the generic port contract, [Testing](Testing.md)
for scenario selection, and [Security Model](Security-Model.md) for the policy
enforced after boot.
