# Architecture Notes

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

## Switch path

The intended production switch path is:

1. Secure scheduler timer expires.
2. Secure handler masks guest-visible IRQ delivery.
3. Current NS context is captured.
4. Non-owned pending IRQs are quarantined.
5. Next guest memory windows and NS MPU envelope are installed.
6. Next guest IRQ mask is installed.
7. Next guest context is restored.
8. Secure exception return transfers execution into NS.

The code in `src/monitor.c` models this sequence. The real port should bind
`wt_platform_prepare_guest_return()` and
`wt_platform_restore_guest_context()` to the architecture-specific exception
return path.

## Guest ABI

The minimum guest contract is:

- fixed boot entry point and initial NS stacks,
- fixed IRQ ownership,
- guest tick source supplied either by dedicated timer hardware or by
  monitor-injected periodic IRQ behavior,
- optional hypercall or yield entry if the integration needs it.

Guests may still use their own `PendSV` or RTOS context switch logic inside
their partition. The monitor does not rely on guest `PendSV` to perform guest to
guest switches.

## Porting tasks

A real SoC port must replace `src/platform_stub.c` with Secure firmware that:

- configures the Secure scheduler timer,
- programs Secure attribution and memory firewalls,
- restores the NS vector table and stack pointers as required by the SoC,
- applies per-guest NVIC enable/pending policy,
- captures fault syndrome information for recovery and audit,
- zeroizes guest RAM regions according to restart policy.

## STM32H563 bring-up status

The repository now includes an initial `firmware/stm32h563/` port intended for
execution under `m33mu`.

What is implemented there:

- freestanding Secure image with vector table and linker script,
- two freestanding Non-secure guest images with fixed flash/RAM placement,
- SAU and STM32H5 GTZC/MPCBB setup for Secure vs Non-secure partitioning,
- Secure `SysTick` driven preemptive context switching between the two guests,
- NS MPU reload on each dispatch for guest-local flash/RAM isolation in the
  emulator-backed port,
- a visible dual-UART demo where the two guest sandboxes print independently on
  `USART2` and `USART3`.

What is intentionally deferred in that first port:

- stronger peer isolation using SoC-specific firewalls beyond the NS MPU,
- a non-stub implementation of the optional NSC yield path.

That means the generic monitor core and the STM32H563 firmware target now both
exercise the intended timer-preemptive Secure monitor flow under `m33mu`, with
the main remaining gap being stronger hardware-backed peer isolation inside the
Non-secure world.
