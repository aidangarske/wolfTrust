# m33mu papercuts

Hardware-only defects found while bringing up the STM32H563 TrustZone firmware test.
Treat these as emulator fidelity gaps until each has a reduced reproducer.

## Fixed in this pass

- ARMv8-M secure-to-nonsecure entry validation now rejects null `BXNS/BLXNS`
  targets before switching state. `m33mu` records a SecureFault with
  `SFSR=INVEP|AUVIOL|SFARVALID` and `SFAR=target`, matching the hardware
  `SFSR=0x49` class observed when `__gnu_cmse_nonsecure_call` executed
  `blxns r4` with `r4 == 0`.
- STM32H563 SAU PPB register layout now includes `SAU_TYPE` at `0xE000EDD4`;
  `RNR/RBAR/RLAR` live at the hardware addresses `0xE000EDD8/0xE000EDDC/
  0xE000EDE0`. This removes the emulator's previous tolerance for firmware
  that wrote the SAU registers one word too low.
- wolfTrust's STM32H563 partition metadata now derives guest flash bases and
  MPU windows from `memory_map.h`, and `memory_map.h` carries the current
  guest layout (`0x08018000/0x08028000`). This closes the local layout drift
  that emulator workflows were not making visible.
- STM32H563 GTZC SRAM MPCBB attribution: firmware marked all SRAM1 blocks
  non-secure, including secure `.bss` at `0x30020000`. Real hardware then lost
  secure scheduler state and faulted in `wt_monitor_init`. `m33mu` now denies
  secure accesses through the secure SRAM alias when MPCBB marks the block
  non-secure, while preserving secure data access through the non-secure alias.
- STM32H563 secure CPU reads from the guest non-secure flash alias
  (`0x08018000`, `0x08028000`) returned zeros during secure dispatch even
  though `STM32_Programmer_CLI` verified valid vectors in flash. The firmware
  now reads guest vectors through the secure flash alias (`0x0c018000`,
  `0x0c028000`) and still branches to the non-secure reset address stored in
  the vector. `m33mu` now models this STM32H5 behavior with a target flag:
  secure reads through the non-secure flash alias return zero, while
  non-secure reads and secure reads through the secure alias still see the
  flash backing.
- Stale guest partition entry points (`0x08018101`, `0x08028101`) did not match
  the linked guest reset handlers (`0x08018131`, `0x08028131`). Hardware GDB
  showed `blxns` successfully entered non-secure state at the stale address,
  before faulting. `m33mu` now emits a low-noise TrustZone warning when a
  `BXNS/BLXNS` target is close to, but not equal to, `VTOR_NS[1]`; it does not
  fault this case because branching to an arbitrary non-secure function pointer
  is architecturally valid.

## Still Open

- STM32H563 secure and non-secure SRAM aliases need shared physical backing.
  Hardware showed guest RAM clearing through the `0x20000000` non-secure alias
  corrupting secure scheduler state when secure `.bss` overlapped the same
  physical SRAM1 window through the `0x30020000` secure alias. wolfTrust now
  places secure RAM above the guest windows, but m33mu should model this alias
  overlap so the defect is visible in emulation.
- The HSM ECC sign path overflowed a 32 KiB secure coroutine stack on hardware:
  the watchpoint hit in the TFM/wolfCrypt path below the coroutine stack base
  during `wc_ecc_sign_hash`. wolfTrust now uses 64 KiB coroutine stacks for the
  HSM engine; m33mu should get a reduced stack-canary test if this path still
  passes silently under emulation.
- ARMv8-M `BXNS/BLXNS` should reject secure-to-nonsecure targets that still
  carry the Thumb bit. Hardware raised `SFSR=INVEP` when the first-dispatch
  shim branched to the guest reset vector value `0x080182b1`; the CMSE
  non-secure function pointer form must clear bit 0 before `BXNS`.
- ARMv8-M exception return into non-secure Thread mode needs stricter
  `EXC_RETURN` fidelity. Hardware rejected the synthetic first-entry restore
  for guest 1 with `CFSR=0x00040000` (`UFSR.INVPC`) when the initial context
  used `EXC_RETURN=0xffffffb8`; setting `ES` gives the hardware-valid
  `0xffffffb9`. The installed `m33mu` behaves the other way around: it allowed
  the stale `0xffffffb8` path before this fix, and with the hardware-correct
  `0xffffffb9` it restores guest 1 with the wrong/secure stack state and stops
  at `pc=0x080282b6` on a null load. Reduced reproducer should cover the
  ARMv8-M `ES` bit for Secure-exception-to-Non-secure-thread returns.
- STM32H563 peripheral security attribution is split between SAU and
  GTZC/TZSC. Hardware entered non-secure code after the `BXNS` fix, then
  faulted on the first guest store to `USART3_BASE` until TZSC marked USART2
  and USART3 non-secure. `m33mu` currently accepts the access when SAU marks
  the APB window non-secure, even if the target-specific TZSC bit remains
  secure.
- STM32H563 secure and non-secure RCC aliases need separate treatment for
  non-secure peripheral use. Enabling USART2/USART3 only through secure RCC
  left hardware faulting on the first non-secure USART access; `m33mu` did not
  model the split clock-gate view.
- HSM-mode scheduling still needs a reduced test: with `WT_SHARED_UART=1`,
  the non-HSM firmware test prints both `g0` and `g1` on USART2 under installed
  `m33mu`, proving basic secure scheduling and UART routing. The HSM engine path
  still prints only `g0` on the same USART2 path, so the remaining issue is
  narrowed to HSM/coroutine interaction rather than UART routing. Do not
  classify this as an m33mu fidelity bug without a hardware comparison.
- The HSM engine path originally entered libgcc's `__gnu_cmse_nonsecure_call` helper
  for the first non-secure reset call, which emitted `vlstm sp`. The installed
  emulator treated that instruction as an undefined instruction even though
  the STM32H563 FPU path is enabled. Firmware now uses an integer-only
  first-dispatch `BXNS` shim, but the missing/rough FP lazy-stack instruction
  coverage is still worth a reduced m33mu test.
