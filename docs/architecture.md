# Architecture Notes

The clean-room policy and normative requirements for the TF-M replacement
work are maintained in [clean-room-development.md](clean-room-development.md)
and [requirements/](requirements/README.md). The architecture described below
is the currently implemented static Non-secure guest monitor and is the base
from which the independently specified Secure Partition Manager is developed.

## Model

WolfTrust is a Secure monitor for ARMv8-M systems such as Cortex-M33. It is
designed as a static partitioner:

- every guest is Non-secure,
- every guest owns a fixed memory envelope,
- every guest owns a fixed IRQ set,
- every guest receives a fixed slot in a cyclic schedule.

The monitor is responsible for dispatch, fault containment, and restoring each
guest's NS execution context. Guests are resumed from the exact point where they
were preempted.

The generated Secure Partition Manager manifest is validated before monitor
startup. The STM32H563 port then binds each static hardware guest envelope to a
manifest secure-partition domain and fails closed on missing or incompatible
identity, security state, privilege state, restart policy, or memory envelope.
The generated flash/RAM resources populate the H5 memory windows and MPU
regions. Device and NSC windows remain platform policy. Vector-table read
aliases and wolfHSM transport windows are explicit port capabilities. The port
declares both required and provided capabilities, rejects missing or unknown
capabilities, and verifies that each transport window is contained in a
manifest-authorized writable, non-executable memory resource before scheduling.

Alongside guest scheduling the Secure side hosts a wolfHSM server, exposed to
guests through ARMv8-M Non-secure Callable (NSC) veneers. The crypto/keystore
service shares the Secure exception model with the monitor but runs in its own
secure tasklet context entered through Secure PendSV, so HSM work never
executes on the SysTick handler's stack and never blocks guest preemption.

## Why this is a separation kernel, not a generic hypervisor

ARMv8-M TrustZone gives a Secure/Non-secure split, not full hardware
virtualization between multiple peer guests. For strong separation between
multiple Non-secure guests, the monitor must combine:

- Secure-side memory attribution (`SAU` plus implementation firewalls),
- per-dispatch NS MPU reprogramming,
- interrupt ownership enforcement,
- static configuration with no guest control over partition metadata.

This works well for RTOS-style partitions. It is not intended to run richer
MMU-based OSes.

## Repository layout

- `include/wolftrust/` — public Secure-side headers (`monitor.h`, `partition.h`,
  `platform.h`, `types.h`, plus `arch/armv8m/`, `sched/`, `services/`, `sync/`).
- `src/monitor.c` — generic scheduler core, fault path, NSC yield hook.
- `src/spm.c` — manifest validation and fail-closed SPM bootstrap state.
- `src/arch/armv8m/` — ARMv8-M specific code: CMSE helpers (`cmse.c`),
  CMSE-validated wolfHSM transport (`cmse_transport.c`), Secure PendSV tasklet
  switch path (`coroutine_armv8m.c`).
- `src/sched/coroutine.c` — Secure-side tasklet runqueue used by the
  wolfHSM service.
- `src/sync/mutex.c` — sleep mutex on top of the coroutine scheduler. Used to
  serialise shared NVM access between per-guest HSM coroutines.
- `src/services/wolfhsm/` — per-guest wolfHSM server context, NVM lock callback
  table, and the runner glue (`runner/` — IVT, runtime libc stubs, linker script,
  local wolfHSM settings).
- `port/stm32h563/` — STM32H563 platform port: SAU/GTZC setup, SysTick,
  partition table, internal-flash NVM backend, RNG entropy source, register
  definitions.
- `lib/wolfhal`, `lib/wolfHSM`, `lib/wolfSSL` — vendored dependencies. wolfCrypt
  is built into the Secure image with `NO_WOLFSSL_MEMORY` + `WOLFSSL_NO_MALLOC`
  and uses sp_math (`sp_int.c` + `sp_cortexm.c`) plus the Thumb-2 AES/SHA-256
  assembly; there is no heap in the Secure domain.
- `tests/firmware/stm32h563/` — Cortex-M33 firmware test (Secure + two NS
  guests), launched under `m33mu`.
- `tests/host/wolfhsm_loopback/` — POSIX test that exercises the wolfHSM
  client/server stack end-to-end without the SoC.

## Switch path

The production switch path is:

1. Secure scheduler timer expires.
2. Secure handler masks guest-visible IRQ delivery.
3. Current NS context is captured.
4. Non-owned pending IRQs are quarantined.
5. Next guest memory windows and NS MPU envelope are installed.
6. Next guest IRQ mask is installed.
7. Next guest context is restored.
8. Secure exception return transfers execution into NS.

The code in `src/monitor.c` implements this sequence; the STM32H563 port binds
`wt_platform_prepare_guest_return()` and `wt_platform_restore_guest_context()`
to the SoC's exception return path.

`wt_monitor_on_secure_timer` deliberately skips guest save/dispatch when the
frame belongs to a Secure-side HSM tasklet (`wt_co_current() != NULL`): the
hardware exception frame will unwind naturally, and guest scheduling resumes on
the next tick after the tasklet blocks back to bootstrap. Mixing handler-mode
tasklet switching with NS context capture would corrupt either side.

## Guest ABI

The minimum guest contract is:

- fixed boot entry point and initial NS stacks,
- fixed IRQ ownership,
- guest tick source supplied either by dedicated timer hardware or by
  monitor-injected periodic IRQ behavior,
- optional NSC entry for wolfHSM service calls and for voluntary yield.

Guests may still use their own `PendSV` or RTOS context switch logic inside
their partition. The monitor does not rely on guest `PendSV` to perform guest to
guest switches.

## wolfHSM service

The Secure side hosts one wolfHSM server context per guest. Each guest is a
wolfHSM client; requests cross the boundary through a 512-byte NS-RAM window
(256 B request slot + 256 B response slot, `whTransportMemCsr` layout) that
sits inside the guest's own RAM envelope at a fixed offset. The secure side
validates every access against the guest's declared NS window AND through
`cmse_check_address_range`, so the server never trusts a guest pointer.

Per-guest server contexts share one wolfHSM NVM context, which is serialised
by a sleep mutex (`wt_mutex_t`) plugged into wolfHSM's `whLockCb` vtable.
On STM32H563 the NVM backend (`port/stm32h563/hsm_flash.c`) writes two
mirrored 8 KiB sectors at the end of internal flash bank 2.

Each per-guest server runs inside a Secure tasklet. Tasklets are entered from
NSC submit/poll veneers through `SVC -> Secure PendSV -> exception return`,
then block back to bootstrap when wolfHSM returns `WH_ERROR_NOTREADY` or when
the NVM mutex path sleeps. SysTick can preempt a running tasklet, but it never
tries to schedule guests from that secure frame; it simply resumes the tasklet
until bootstrap regains control.

Per-guest RNG state inside the server uses `INVALID_DEVID` so it pulls
directly from the platform entropy source (`wolftrust_rng_generate_block`)
rather than routing back out through the client. On STM32H563 entropy comes
from the SoC TRNG via wolfHAL (`lib/wolfhal/src/rng/stm32h5_rng.c`).

## Porting tasks

A real SoC port must replace `src/platform_stub.c` (or supply a port like
`port/stm32h563/platform_stm32h563.c`) with Secure firmware that:

- configures the Secure scheduler timer,
- programs Secure attribution and memory firewalls,
- restores the NS vector table and stack pointers as required by the SoC,
- applies per-guest NVIC enable/pending policy,
- captures fault syndrome information for recovery and audit,
- zeroizes guest RAM regions according to restart policy,
- if wolfHSM is enabled (`WT_ENGINE_HSM=1`), supplies a flash backend for the
  shared NVM partition and a target-specific entropy source.

## STM32H563 status

`tests/firmware/stm32h563/` is the reference port, executed under `m33mu`. The
top-level `make` builds it; `make run-stm32h563-uarts` (or
`make run-hsm-uarts`) runs the dual-UART test.

What is implemented:

- freestanding Secure image with vector table, linker script, and an MSP-only
  Secure runtime (no heap, no libc beyond the stub set in
  `src/services/wolfhsm/runner/libc_stubs.c`),
- two freestanding Non-secure guest images with fixed flash/RAM placement and
  identical client code parameterised by `WT_GUEST_ID`,
- SAU + STM32H5 GTZC/MPCBB setup for Secure vs Non-secure partitioning,
- Secure `SysTick` driven preemptive context switching between the two guests,
- NS MPU reload on each dispatch for guest-local flash, RAM, USART, and the
  NSC veneer window,
- per-guest wolfHSM client/server pair using sp_math + Thumb-2 ARMASM,
  performing ECDSA sign/verify in each guest and persisting key material in
  internal flash through the wolfHSM NVM layer,
- CMSE-import-library based NSC linkage (`--cmse-implib`): the Secure ELF
  emits `secure_cmse_implib.o`, guests link against it instead of hardcoding
  veneer addresses (wolfBoot pattern),
- a visible dual-UART firmware test where the two guest sandboxes print
  independently on `USART2` and `USART3`,
- a host-side POSIX loopback test (`tests/host/wolfhsm_loopback`) that
  exercises the wolfHSM client/server stack with the same wolfCrypt settings
  used in firmware.

What is intentionally deferred:

- stronger peer isolation using SoC-specific firewalls beyond the NS MPU,
- richer fault handling and audit logging from the secure log buffer.

That means the generic monitor core, the wolfHSM secure service, and the
STM32H563 firmware target all exercise the intended timer-preemptive Secure
monitor flow plus the cooperative HSM service path under `m33mu`. The main
remaining gap is stronger hardware-backed peer isolation inside the
Non-secure world.
