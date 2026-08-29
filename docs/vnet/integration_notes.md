# VNET integration notes (Wave 0)

> **Historical (superseded).** These notes captured the original design, in
> which guests reached the switch through raw `WolfTrust_VNet_*` CMSE veneers.
> That surface was removed: wolfIP virtual networking now ships as
> `SERVICE_VNET`, a Secure Partition reached only through the FF-M SPM
> (`psa_connect`/`psa_call`), with no dedicated veneer. Caller identity is the
> SPM-stamped client id, and NS-pointer validation happens in the FF-M gateway,
> not in a per-service veneer. See [architecture.md](../architecture.md) and
> the `WT-FFM-0056..0058` rows in `docs/requirements/framework.md` for the
> shipping contract; the sections below are kept for design history.

Implementation notes covering where the virtual-Ethernet subsystem hooked into
the wolfTrust monitor in the original veneer-based design.

## Caller identification

The wolfHSM veneers in `port/stm32h563/platform_stm32h563.c` read
`g_active_guest` (a file-static `volatile uint32_t`) to know which guest
is currently calling. The scheduler in `src/monitor.c` keeps a parallel
`g_scheduler.current_guest` it updates on dispatch.

VNET veneers MUST identify the caller the same way and MUST NOT trust a
guest-supplied VM ID. To avoid every new veneer reaching into the
platform global, VNET introduces `wt_monitor_active_guest_id(void)` as a
thin accessor (Wave 2).

## NS pointer validation

Two existing helpers in `src/arch/armv8m/cmse.c` cover the validation
contract:

  - `wt_cmse_check_ns_rw(ptr, size)` / `wt_cmse_check_ns_ro(ptr, size)` -
    CMSE address-range check. Tells you "this address range is reachable
    as NS world from S code, with the requested permission." Does NOT
    tell you which guest owns it.

  - `wt_cmse_check_in_guest_ns_ram(guest_id, ptr, size)` - walks the
    declared `memory_windows` for that guest's `wt_guest_config_t` and
    verifies the pointer lies entirely inside one of them. This is the
    isolation check: it stops guest A from passing guest B's RAM as an
    "output" buffer.

Every VNET NSC entry must call both, in that order. The wolfHSM
transport (`src/arch/armv8m/cmse_transport.c`) is the working template:
it re-validates on every call (not just at init) as TOCTOU defence, and
copies via a single `memcpy` so the secure side never re-reads from NS
memory after the copy.

## Error convention

The project uses signed-int wolfHSM codes from
`lib/wolfHSM/wolfhsm/wh_error.h`: `WH_ERROR_OK=0`,
`WH_ERROR_BADARGS=-2000`, `WH_ERROR_NOTREADY=-2001`,
`WH_ERROR_ABORTED=-2002`. Veneers return `int`. VNET reuses these where
the semantics match and adds vnet-specific codes in
`include/wolftrust/vnet/vnet_errors.h` (Wave 1) for cases the wolfHSM
set does not cover (spoofed source MAC, duplicate MAC assignment, RX
queue full, frame-pool full, slot/gen mismatch on release, etc.).

## Concurrency model

The secure monitor is cooperative + timer-driven: it runs to completion
inside a veneer or a SysTick handler, with no preemption between
veneers. There is a `wt_mutex_t` (`src/sync/mutex.c`) but its header
explicitly forbids use from bootstrap or interrupt context - it is for
coroutine code only.

VNET's data plane runs inside veneer context (caller guest is by
definition not running while its veneer executes) and is touched by the
scheduler on dispatch (to refresh the vIRQ reflection). To bracket the
short windows where the scheduler and a veneer could otherwise observe
inconsistent refcount/queue state, VNET will use a single
`wt_vnet_critical_enter/exit()` wrapper that disables interrupts via
`PRIMASK`. This is appropriate because every protected section is short
(handful of pointer/counter edits) and the monitor is the only consumer.

## Virtual IRQ - new infrastructure

There is **no existing virtual-interrupt mechanism in the monitor.**
Hardware IRQs pass through to the NS NVIC via the per-guest
`wt_irq_mask_t` programmed in `wt_platform_apply_irq_mask()`. Nothing
synthesises an interrupt visible to a guest.

VNET introduces the first synthesised, per-guest IRQ. Design:

  - One reserved NS IRQ line, configurable via `WT_VNET_RX_IRQ` (default
    a high-number line outside any STM32H5 peripheral, e.g. line 130).
  - Per-guest pending bit owned by the monitor.
  - Level semantics: pending while the guest's RX queue is non-empty.
  - Reflection point: on every guest dispatch, the monitor writes the
    pending bit into the NS NVIC's `ISPR` (if set) or `ICPR` (if clear).
  - `vnet_irq_ack` only clears the pending bit when the RX queue is
    empty - re-asserting otherwise on next dispatch.

Wave 4 implements this. Until then the data plane keeps the bit
correct; the reflection is a stub.

## Subsystem layout

Files added per wave (paths fixed up-front so subsequent commits stay
coherent):

  - `include/wolftrust/vnet/*.h` - public types, ABI, helper APIs
  - `src/vnet/*.c` - pure dataplane, no hardware coupling
  - `port/stm32h563/platform_stm32h563.c` - VNET veneers appended,
    pattern-matched to the existing `WolfTrust_HSM_*` block
  - `tests/host/vnet/` - host-side unit tests (gcc, raw asserts,
    same shape as `tests/host/wolfhsm_loopback`)
  - `docs/vnet/` - this note plus per-wave addenda

## Build switch

A top-level `CONFIG_VNET` switch in `mk/secure-armv8m-stm32h563.mk`
gates whether `src/vnet/*.c` and the VNET veneers are linked into the
secure image. It defaults to `n` until Wave 2 lands a working core.
Host tests are unconditional - they don't link against the firmware.

## L3 stack: wolfIP

`lib/wolfIP` is vendored as a submodule and is the reference L3 stack
for guest-side use. Its public driver interface (`struct wolfIP_ll_dev`
in `wolfip.h`) is the integration point: a guest links wolfIP, builds
a `wolfIP_ll_dev` whose `.poll` calls `vnet_rx_poll`/`vnet_rx_read`/
`vnet_rx_release` and whose `.send` calls `vnet_tx`, then registers it
via the normal wolfIP setup. No vnet ↔ wolfIP coupling exists in the
secure monitor - wolfIP is strictly NS-side.

wolfIP's `struct wolfIP_eth_frame` lives in `src/wolfip.c` under
`#ifdef ETHERNET` and is not exported. The vnet switch keeps its own
minimal Ethernet header view (dst[6] + src[6] + ethertype) for
source-MAC validation and FDB learning - there is no need to reach
into wolfIP internals.

## Acceptance bar

End-to-end: two baremetal NS guests, each linking wolfIP against a
vnet driver shim, can exchange ICMP echo (`ping`). That is the Wave 5
deliverable and the firmware-level pass criterion. Earlier waves are
gated on host-side unit tests and the secure monitor compiling clean
under `CONFIG_VNET=y`.

A new firmware test target - separate from the existing wolfHSM
dual-UART test - will host this demo (tentative path
`tests/firmware/stm32h563-vnet/`).

## Out of scope for the early waves

Physical Ethernet bridging and routed mode are explicitly deferred. The
core types include placeholder hooks (`vnet_port_ops`) so that when a
physical port shows up later it can be a peer of the per-guest virtual
ports, but Waves 1–5 do not implement any physical-PHY code. The
secure monitor will keep DMA ownership of any future Ethernet
peripheral; guests will only ever speak the copy ABI.
