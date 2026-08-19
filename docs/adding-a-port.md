# Adding a wolfTrust port

How to bring wolfTrust to a new SoC. The goal of the port design: a new target
is **one new `port/<soc>/` folder that fills in a fixed contract — the core is
never edited**. See `docs/port-contract.md` for the full interface and
`docs/requirements/portability.md` for the `WT-PORT-000x` requirements.

## What you reuse vs. what you write

| Reused unchanged (core) | You write (the port) |
| --- | --- |
| SPM, FF-M IPC runtime, PSA service surface | `port/<soc>/` MCU-family layer |
| lifecycle / restart engine, manifest + validation | board description + memory map |
| conformance suite, host tests | a per-SoC manifest + build file |
| the Armv8-M architecture port (if the SoC is Armv8-M) | flash / entropy / clock / MPU program |

If the SoC is a **different architecture** (e.g. AArch64) you also write a new
`src/arch/<arch>/`; if it is Armv8-M you reuse `src/arch/armv8m/` as-is.

## Steps

1. **Copy the skeleton.** Start from `port/stm32h563/` (or the reference stub
   `src/platform_stub.c`, which implements the host-buildable subset) into
   `port/<soc>/`.
2. **Implement the architecture-port surface** — the `wt_platform_*` functions
   in `include/wolftrust/platform.h` (context switch, memory/MPU program,
   fault/panic/reset, IRQ routing, mode queries). The list and grouping is in
   `docs/port-contract.md` §"Surface A". A function you cannot yet support
   returns a not-implemented error — never an always-success stub.
3. **Implement the MCU-family surface** — flash/NVM (`port_nvm` contract) and
   entropy (`port_entropy` contract), plus clock and identity. See
   `docs/port-contract.md` §"Surface B".
4. **Describe the board** — `board.h` (peripheral bases, console UART) and
   `memory_map.h` (secure/NS flash + RAM windows, per-partition RAM).
5. **Author the manifest** — a `port/<soc>/manifest.json` for the SoC's
   partitions/memory, then regenerate; the generated manifest is the sole SPM
   input (no hand-edited runtime tables).
6. **Add the build wiring** — `mk/secure-<arch>-<soc>.mk` mirroring
   `mk/secure-armv8m-stm32h563.mk` (toolchain, `-I$(PORT_DIR)`, image layout).
7. **Wire the lock workflow** (if the SoC has an immutable-RoT lifecycle) — map
   its native primitives onto a `provisioning_ctrl.sh`-style control script.
   Per-vendor primitives are catalogued in
   `docs/competitive-edge-vs-secure-manager.md`.

## Prove it (same gates as H5)

Run in order; do not claim a port works on a compile alone:

1. `make test` (host) — the core + your host-buildable port stub.
2. The Cortex-M (or target-arch) cross-build.
3. The emulator lifecycle matrix if one exists for the target (H5 uses the
   M33MU 85/4 gate).
4. Physical-board smoke when hardware is available (recorded in a separate
   hardware ledger, never implied by emulator runs).

## Target readiness

Concrete next targets and their effort are mapped in `docs/port-contract.md`
§"Porting plan": STM32C5 (reuses the Armv8-M port; a real second-target flow is
already sketched in `docs/requirements/portability.md`), then STM32U5/L5/H7,
then other Armv8-M vendors (NXP/Nordic/Renesas/Microchip) by mapping their
native RoT onto the MCU-family surface.
