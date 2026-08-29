# RM0481 encoding cross-check (STM32H5)

Every register value, option-byte code, and product-state constant that
[`stm32h5-secure-manager-guide.md`](stm32h5-secure-manager-guide.md) and the
`tests/target/*.sh` provisioning tooling assert is cross-checked here against
its normative source before any external-facing claim. Values were first
*observed* on the NUCLEO-H563ZI; this ledger is the paper trail that they match
the ST STM32H5 reference manual (**RM0481**) and the Arm Cortex-M33 architecture.

Legend: **RM0481** = ST reference manual RM0481; **Arm M33 UG** = Arm Cortex-M33
Devices Generic User Guide; **HW** = confirmed live on the board (discover /
option-byte read-back). ✓ = value verified.

## 1. Product-state ladder - `FLASH_OPTSR`, `PRODUCT_STATE[7:0]`

STM32H5 is a one-way product-state ladder (RM0481, "Product state" /
"Life cycle"). The 8-bit `PRODUCT_STATE` code is programmed in the FLASH option
bytes and read back by Debug Authentication `discover`.

| State | Code | Source of truth | ✓ |
| --- | --- | --- | --- |
| Open | `0xED` | RM0481 product-state table; HW `discover` = `ST_LIFECYCLE_...` at baseline | ✓ |
| Provisioning | `0x17` | RM0481; **directly confirmed** in ST AN "RDP-like product state" note; HW `ST_LIFECYCLE_PROVISIONING` | ✓ |
| TZ-Closed | `0xC6` | RM0481; HW `discover` = `ST_LIFECYCLE_TZ_CLOSED` | ✓ |
| Closed | `0x72` | RM0481; **directly confirmed** in the same ST note; HW read-back | ✓ |
| Locked | `0x5C` | RM0481 - final, irreversible; **never programmed on the dev board** | ✓ |

Intermediate `iRoT-provisioned` (`0x2E`) exists in RM0481 but is not used by the
wolfTrust flow, which advances Open → Provisioning → TZ-Closed → Closed and
regresses via a DA certificate Full Regression. The one-way property and the
"Locked = no regression" rule are RM0481-normative and are why
`provisioning_ctrl.sh advance` refuses `0x5C` outright.

## 2. TrustZone + boot option bytes - `FLASH_OPTSR2`

| Field | Code | Meaning (RM0481) | ✓ |
| --- | --- | --- | --- |
| `TZEN` | `0xB4` | TrustZone **enabled** (`0xC3` = disabled) | ✓ |
| `BOOT_UBE` | `0xB4` | OEM-iRoT boot path (`0xC3` = ST-iRoT / `SECBOOTADD`) | ✓ |

`TZEN=0xB4` = enabled is corroborated externally (SEGGER STM32H5 lifecycle note
reads `TZEN 0xC3` for the *disabled* case). `TZEN=0xB4` selecting the OEM-iRoT
certificate DA pairing is per ST AN6008.

## 3. Secure flash watermarks - `FLASH_SECWM1R1` / `FLASH_SECWM2R1`

STM32H563 = 2 MB dual-bank, **8 KiB** sectors, 128 sectors/bank. `SECWMx_STRT`
/ `SECWMx_END` are sector indices; a sector is secure iff its index is within
`[STRT, END]`. Secure-alias writes to a sector outside the window are silently
dropped (RM0481 secure-watermark semantics - the source of gotcha §7.1).

| Field | Code | Sector math | ✓ |
| --- | --- | --- | --- |
| `SECWM1_STRT` | `0x00` | bank-1 sector 0 → `0x08000000` | ✓ |
| `SECWM1_END` | `0x4F` | bank-1 sector 79 → secure through `0x0809FFFF`; guest0 at `0x080A0000` = sector `0x50`, just past | ✓ |
| `SECWM2_STRT` | `0x00` | bank-2 sector 0 | ✓ |
| `SECWM2_END` | `0x7F` | bank-2 sector 127 (last) → all of bank 2 secure | ✓ |

`SECWM1_END=0x4F`: 80 sectors × 8 KiB = 640 KiB = `0xA0000`, so
`0x08000000..0x080A0000` is secure - the whole wolfBoot + wolfTrust boot
partition. `0x3F` would have ended at `0x08080000` (128 KiB short), truncating
the ~140 KiB signed image → wolfBoot integrity-reject (guide §7.1).

## 4. Secure reset authority - `SCB->AIRCR` (`0xE000ED0C`)

The #83 fix. All bit positions are Armv8-M architectural (Arm M33 UG,
"Application Interrupt and Reset Control Register"), banked to the Secure alias
of AIRCR when TrustZone is implemented.

| Bit | Name | Value used | Meaning (Arm M33 UG) | ✓ |
| --- | --- | --- | --- | --- |
| `[31:16]` | `VECTKEY` | `0x05FA` | write key; writes without it are ignored | ✓ |
| `14` | `PRIS` | preserved | prioritize Secure exceptions | ✓ |
| `13` | `BFHFNMINS` | preserved | BusFault/HardFault/NMI target security | ✓ |
| `[10:8]` | `PRIGROUP` | preserved | priority grouping | ✓ |
| `3` | `SYSRESETREQS` | **set to 1** | `SYSRESETREQ` usable from **Secure only**; a Non-secure `SYSRESETREQ` no longer resets the SoC | ✓ |
| `2` | `SYSRESETREQ` | - | request a system reset | ✓ |

`SYSRESETREQS=1` is the exact architectural mechanism of the #83 fix: the Arm
M33 UG states set = "SYSRESETREQ functionality is only available to Secure
state." `WT_SCB_AIRCR_CFG_MASK` in `stm32h563_regs.h` preserves bits 3/13/14 and
`PRIGROUP[10:8]` across the write so only the intended field changes.

## 5. Result

All values asserted by the guide and the provisioning tooling match RM0481 (ST
product-state / FLASH option bytes / secure watermarks) and the Arm Cortex-M33
architecture (AIRCR). The guide's earlier "as-observed on hardware, not yet
paper-verified" caveat is retired. Sources: RM0481 (STM32H563/573 reference
manual); Arm Cortex-M33 Devices Generic User Guide (AIRCR); ST AN6008 (OEM-iRoT
DA pairing); ST "RDP-like product state" application note (0x17 / 0x72).
