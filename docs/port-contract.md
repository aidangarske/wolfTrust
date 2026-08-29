# wolfTrust Port Contract (WT-PORT)

The interface a new target must implement to run wolfTrust. The architecture-
neutral **core** (`src/`, `include/wolftrust/`) contains policy, IPC, lifecycle,
and services and must not name Armv8-M, CMSE, MPU, GTZC, SAU, NVIC, or any SoC
register. Everything target-specific lives behind this contract in the **port**
(`src/arch/<arch>/`, `port/<soc>/`). Formal requirements: `WT-PORT-0001`..`0008`
in `docs/requirements/portability.md`. This document is the implementation-level
companion the requirements table points at.

## Two port layers

| Layer | Supplies | STM32H563 impl |
| --- | --- | --- |
| **Architecture port** (`src/arch/<arch>/`) | context save/restore, secure transport (SVC), CMSE veneers, MPU program/restore, mode queries | `src/arch/armv8m/` |
| **MCU-family port** (`port/<soc>/`) | platform bring-up, clock/timer, memory-window + MPU envelope, fault/panic/reset, flash/NVM, entropy, IRQ routing, board map | `port/stm32h563/` |

A new **architecture** (e.g. AArch64) replaces the architecture port only. A new
**MCU in the same family** (e.g. STM32C5) reuses the architecture port and most
"common STM32" glue, replacing only the MCU-family port and board description
(`WT-PORT-0007`: a C5 port must need zero core/service edits).

## Surface A - architecture-port entry points (`include/wolftrust/platform.h`)

32 `wt_platform_*` symbols the core calls. Grouped by concern:

- **Bring-up / tick:** `init`, `start_secure_timer`.
- **Guest switch / context:** `prepare_guest_return`, `capture_guest_context`,
  `restore_guest_context`, `svc_guest_return`, `return_to_secure_thread`,
  `restore_ns_bank`, `active_guest_id`.
- **Memory / isolation (WT-FFM-0011):** `program_memory_windows`,
  `program_ns_mpu`, `program_secure_partition_domain`,
  `program_sp_thread_domain`, `restore_spm_domain`, `run_crypto_sp_isolated`,
  `zero_guest_memory`.
- **IRQ delivery:** `mask_all_guest_irqs`, `apply_irq_mask`,
  `quarantine_pending_irqs`, `secure_irq_enable`, `secure_irq_disable`,
  `configure_ns_irq`, `set_ns_irq_pending`.
- **Mode queries:** `in_handler_mode`, `ns_thread_mode_trap`.
- **Fault / panic / reset:** `log_fault`, `read_fault_address`,
  `all_guests_faulted`, `panic`, `system_reset`.
- **Engine bookkeeping (`WT_ENGINE_HSM`):** `secure_service_active`,
  `note_hsm_wait_skip`.

**Transport hook (the clean pattern to copy):** `include/wolftrust/spm_gate.h`
declares a neutral `wt_spm_transport_fn` typedef; the core default is
`wt_spm_transport_direct`, and the Armv8-M port supplies `wt_spm_svc_transport`
(`src/arch/armv8m/spm_svc.c`) with no ARM naming in the core header.

## Surface B - MCU-family port (currently undeclared; MP4 closes this)

Per `WT-PORT-0003` the MCU-family port supplies flash/entropy/identity/lifecycle/
counters/reset. Today two of these exist but have **no header under
`include/wolftrust/`** - the core couples to them by compile-time include path:

- **Flash/NVM:** `port/stm32h563/hsm_flash.h` (`g_wt_hsm_flash_cb`,
  `wt_hsm_flash_context()`, `wt_hsm_flash_config()`); core file
  `src/services/wolfhsm/wt_hsm.c` includes it via bare `#include "hsm_flash.h"`,
  resolved only because the secure build injects `-I$(PORT_DIR)`.
- **Entropy:** `wolftrust_rng_generate_block()` (wolfCrypt
  `CUSTOM_RAND_GENERATE_BLOCK` target), implemented in
  `port/stm32h563/rng_entropy.c`.

**MP4 action:** promote these to a declared contract header
(`include/wolftrust/port_nvm.h` / `port_entropy.h`) so the coupling is an
interface, not an include-path accident.

## Current split status - enforced

The architecture port and `port/stm32h563/` are correctly scoped, and MP4
resolved the former core→arch leaks: the CMSE veneers were extracted to the arch
port (`src/arch/armv8m/ffm_nsc.c`, `vnet_nsc.c`), the HSM fault-notify now goes
through a platform callback (dropping the arch `cmse_transport.h` include from
core), the `boot_handoff` `dmb`/`dsb` became platform hooks, and the guest
context is held by pointer so `platform.h`/`monitor.h` need no arch layout. The
guard (`tools/check-core-port-split.sh`) now reports **hard leaks = 0, soft
(register-name) hits = 0**, enforced in CI - a new Armv8-M SoC port needs zero
core edits.

Residual (cosmetic, not a leak the guard flags): `include/wolftrust/types.h`
still names `WT_MAX_MPU_REGIONS` / `wt_mpu_region_t` where `WT-PORT-0001` prefers
architecture-neutral `WT_MAX_MEM_REGIONS` / `wt_memory_region_t`; and the
`src/services/wolfhsm/runner/` board glue (IVT, `secure.ld`, libc stubs) is a
candidate to relocate wholesale under `port/stm32h563/`. Neither blocks a port.

The **CMSE veneers** were the real question and are now settled the way
`SERVICE_CRYPTO` was: the portable service logic stays in the core file and only
the veneer/CMSE shim (the `.gnu.sgstubs` section on Armv8-M) lives in the arch
port, so a non-Armv8-M target supplies its own gateway without touching core.

## Porting plan to other targets (DOCS-ONLY - no implementation this milestone)

The order of effort per new target, mapped onto the two surfaces above. Native
immutable-RoT primitives per vendor are catalogued in
`docs/competitive-edge-vs-secure-manager.md` ("Cross-vendor RoT landscape").

**Same architecture (Armv8-M), same vendor - lowest effort.**
- **STM32C5** (next concrete target; `SRC-STM32C5` tracked): reuse `src/arch/
  armv8m/` and common STM32 glue; new MCU-family port (clock, flash, entropy,
  memory map) + board. Lock lifecycle identical (RDP/HDP/WRP, product state, DA
  - same MP3 workflow via `provisioning_ctrl.sh`). See `portability.md:30`.
- **STM32U5 / L5:** same as C5. Lock primitives: RDP + HDP + WRP + TZEN.
- **STM32H7 (H7Sx TZ parts):** Armv8-M; MCU-family port + board; confirm the
  RoT/OB model against its RM before claims.

**Same architecture, other vendors - MCU-family port + lock-primitive mapping.**
Each keeps the Armv8-M architecture port; the MCU-family port maps the target's
native RoT onto the WT-PORT flash/identity/lifecycle/reset surface:
- **NXP LPC55S6x** - CMPA/CFPA + ROTKH + Debug Auth (closest to STM32H5 DA).
- **NXP i.MX RT5xx/RT6xx** - OTP fuses + ROTKH + Debug Auth.
- **Nordic nRF5340 / nRF54L15 / nRF91** - NSIB + UICR/KMU + APPROTECT.
- **Renesas RA6M4/M5** - DLM + SKMT; RA8 - masked FSBL + SFP.
- **Microchip SAM L11 / PIC32CM LS** - UROW/BOCOR + DAL + BOOTKEY (CEHL = one-way lock).

**Different architecture (e.g. AArch64) - replace the architecture port only.**
The core, services, manifest, IPC, and PSA contracts are unchanged
(`WT-PORT-0006`); a new `src/arch/<arch>/` supplies context, transport, isolation
program/restore, and the secure-call gateway.

**What every target reuses unchanged:** the generated manifest + validation, the
SPM/FF-M IPC runtime, the PSA service surface, the lifecycle/restart engine, the
conformance suite, and (for STM32) the MP3 lock workflow.

## Boot integration - how a different loader launches wolfTrust

wolfTrust is boot-agnostic: a signed flat image in secure flash. Vendors differ
in *who verifies and launches* it (ST H5: RSS bootrom → wolfBoot; NXP LPC55:
ROM + ROTKH; Nordic: NSIB/MCUboot; Renesas: boot ROM + DLM), and the loader
touches wolfTrust at exactly three parameterized seams - none of them core code:

1. **Image header/signing** - `WT_SECURE_FLASH_ORIGIN` /
   `WT_SECURE_IMAGE_HEADER_SIZE` are build parameters; a vendor image format
   means a different header size + signing tool in the port's mk file.
2. **Measured-boot handoff** - the neutral `wt_boot_handoff_consume` reads the
   measurement the first stage leaves at a configured address and FAILS CLOSED
   when absent (attestation then reports an unmeasured boot). A different
   loader either produces the same handoff or the port supplies an alternative
   measurement source.
3. **Lock lifecycle tooling** - per-vendor provisioning scripts (the
   `provisioning_ctrl.sh` pattern), mapped in the vendor table above.

**Preferred strategy: wolfBoot as the universal first stage.** wolfBoot already
ports across these vendors, so the component that loads wolfTrust stays
wolfBoot everywhere - the vendor's ROM trust anchor verifies wolfBoot, wolfBoot
verifies and measures wolfTrust - normalizing the handoff format so
`boot_handoff.c` never changes. Only where a vendor ROM must load the image
directly does the port sign in the vendor format and rely on seam 2's fallback.
