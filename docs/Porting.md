# Porting

wolfTrust separates reusable policy and services from architecture, device,
and board-specific execution. The only currently supported and validated build
tuple is `armv8m-stm32h563`. Support for additional Cortex-M ports is an
intended extension point. Such ports may reuse common policy and service code
and an existing architecture adapter when their execution and protection
models match.

Cortex-A support is an architectural goal, not a current capability. It will
require a new adapter and changes to current internal execution and protection
contracts. The design goal is to preserve the public manifest, service, IPC,
and PSA API contracts. Every new port must report its actual capabilities and
must not claim security properties until they are tested on that target.

## Port layers

| Layer | Location | Responsibility |
| --- | --- | --- |
| Common core | `src/` excluding `src/arch/` | Domains, manifest validation, monitor policy, IPC state, lifecycle, guest verification, recovery, and services |
| Public and internal contracts | `include/psa/` and `include/wolftrust/` | PSA APIs, SPM types, port callbacks, manifests, and service interfaces |
| Architecture | `src/arch/<arch>/` and `include/wolftrust/arch/<arch>/` | Architecture-specific gateway checks and Secure execution or transport mechanisms; currently the Armv8-M CMSE gateway and Secure Partition coroutine/SVC path |
| MCU and board | `port/<target>/` | Device startup and guest exception paths, security attribution, guest and Secure MPU programming, context handling, clocks, timers, flash, entropy, IRQ routing, device registers, memory map, guest table, and manifest |
| Build | `mk/secure-<arch>-<target>.mk` | Toolchain, source selection, generated manifest, linker layout, and image checks |
| Guest integration | `tests/firmware/` or an application repository | Application-domain linker layout, PSA client shim, architecture-specific client boundary, and OS wiring; Armv8-M uses a CMSE import library |

## Current architecture and target contract

The common runtime treats `wt_guest_context_t` as an opaque,
architecture-owned type. The current internal callback ABI is not fully
architecture-neutral: `include/wolftrust/platform.h` also exposes an M-profile
exception frame and MPU-oriented region types.

For the supported Armv8-M and STM32H563 pair, `src/arch/armv8m/` supplies the
CMSE gateway and pointer-security checks, Secure Partition coroutine switching,
and the Secure SVC transport. `port/stm32h563/platform_stm32h563.c` implements
the `wt_platform_*` callbacks. Together the two layers supply:

- the concrete guest context and context save/restore path;
- exception entry and return between Secure handlers, Secure threads, and
  Non-secure threads;
- the target Secure-call transport used by scheduled partitions;
- the Non-secure gateway and pointer-security checks;
- guest and Secure Partition MPU programming;
- privilege-state transitions and handler-mode queries; and
- interrupt target, mask, pending, enable, and end-of-interrupt mechanics.

The full current callback contract is declared in
`include/wolftrust/platform.h`. A future architecture may replace this
internal split while preserving the public manifest, service, IPC, and PSA
APIs.

## MCU and board contract

A target directory must provide:

### Platform callbacks

Implement every `wt_platform_*` callback used by the selected build.
The callbacks cover initialization, timer programming, memory windows, guest
MPU state, Secure Partition MPU state, context handling, interrupt routing,
fault reporting, reset, barriers, active-guest identity, guest memory clearing,
and guest-flash protection checks.

Do not return unconditional success for a missing security mechanism. Report
the capability accurately and reject a manifest that requires more.

### Guest and capability tables

Implement the declarations in `include/wolftrust/partition.h`:

```c
const wt_guest_config_t* wt_partitions_config_table(size_t* count);
wt_guest_runtime_t* wt_partitions_runtime_table(size_t* count);
const wt_profile_capabilities_t* wt_partitions_profile_capabilities(void);
int wt_partitions_bind_manifest(const wt_system_manifest_t* manifest);
void wt_partition_reset_runtime(const wt_guest_config_t* config,
                                wt_guest_runtime_t* runtime);
```

Guest executable and RAM windows, vector-table access, IRQ ownership, restart
policy, launch policy, and minimum version must match the actual linker and
hardware layout.

The capability bitmap can declare security state, privilege state, RoT
isolation, domain isolation, memory protection, interrupt isolation, and
restart. The validator rejects a domain whose requirements exceed the port's
declaration.

### Persistent flash

Implement `include/wolftrust/port_nvm.h`:

```c
extern const whFlashCb g_wt_hsm_flash_cb;
void* wt_hsm_flash_context(void);
const void* wt_hsm_flash_config(void);
int wt_hsm_flash_format(void);
```

The implementation must preserve the wolfHSM flash-log semantics, distinguish
foreign or corrupt media, honor checked object flags, and erase only the
dedicated vault region when lifecycle policy allows reformat.

### Entropy

The current Secure wolfCrypt profile maps
`CUSTOM_RAND_GENERATE_BLOCK` to:

```c
int wolftrust_rng_generate_block(unsigned char* output, unsigned int sz);
```

The STM32H563 callback uses wolfHAL's H5 RNG driver. Its unprivileged entry
traps to a privileged SVC operation. A new target must provide an equivalent
approved entropy source and preserve the privilege boundary.

### Board and memory layout

Provide target constants for:

- Secure, client-gateway, application-domain, update, and persistent flash
  regions;
- Secure Partition, SPM, and guest RAM;
- target clocks, timer, UART, RNG, and security peripherals;
- flash erase and write geometry;
- guest vector-table read aliases, if required; and
- WRP or equivalent hardware-enforced guest-image write protection.

Represent the same resources in `manifest.json`. The generator rejects
bad attributes, overlap, missing stacks, unsupported sharing, invalid signals,
dependency cycles, and unsupported features.

## Bootloader contract

The reference integration expects wolfBoot to:

- authenticate wolfTrust;
- provide `wt_boot_handoff_t` with a SHA-256 measurement, lifecycle,
  and image version;
- reserve the configured wolfTrust image header;
- provide an update partition compatible with the FWU backend; and
- authenticate the staged replacement on reboot.

The handoff is consumed and cleared from Secure RAM. If a different first
loader is used, the port must provide equally authenticated lifecycle,
measurement, and version data and adjust the image layout.

## Add a target

1. Add `src/arch/<arch>/` only when the architecture cannot reuse an
   existing implementation.
2. Create `port/<target>/` with the platform, flash, entropy, board,
   memory-map, partition-table, and manifest files.
3. Add `mk/secure-<arch>-<target>.mk` and route the tuple from the root
   Makefile.
4. Supply startup/vector and linker handling appropriate to the target.
5. Generate the manifest at build time and include its digest in the signed
   Secure image.
6. Integrate application domains with the architecture's client boundary and
   matching generated service IDs. Armv8-M targets link Non-secure guests
   against the CMSE import library.
7. Add image assembly that patches guest ID, version, size, and digest records
   before signing wolfTrust.
8. Add safe provisioning tooling for the target's security attribution,
   application-image write protection, debug policy, and product lifecycle.

## Validation checklist

- Run `make test` for common policy and service behavior.
- Run `WT_SPLIT_STRICT=1 tools/check-core-port-split.sh` and resolve
  hard core-to-architecture leaks.
- Cross-build the Secure image with warnings enabled.
- On the current Armv8-M port, inspect `nm` output and confirm only the five
  FF-M veneers are Non-secure-callable.
- Test invalid manifests, memory overlap, pointer ranges, stale handles,
  cross-owner access, and unsupported capabilities.
- Run authenticated boot, guest tamper, rollback, restart, Secure Partition
  fault, storage recovery, and update tests in an architecture-accurate
  emulator when one exists.
- Verify attribution, interrupts, entropy, flash failure, WRP-equivalent
  coverage, reset, and recovery on physical hardware.
- Keep emulator and hardware evidence distinct.

See [Architecture](Architecture.md), [Building](Building.md), and [Testing](Testing.md) for the current reference
implementation.
