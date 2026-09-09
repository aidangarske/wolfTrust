# Porting

wolfTrust separates reusable policy and services from architecture and
MCU-specific execution. The existing supported build tuple is
`armv8m-stm32h563`. A new port should begin by identifying which
existing layer it can reuse and must not claim security properties until they
are tested on the target.

## Port layers

| Layer | Location | Responsibility |
| --- | --- | --- |
| Common core | `src/` excluding `src/arch/` | Domains, manifest validation, monitor policy, IPC state, lifecycle, guest verification, recovery, and services |
| Public and internal contracts | `include/psa/` and `include/wolftrust/` | PSA APIs, SPM types, port callbacks, manifests, and service interfaces |
| Architecture | `src/arch/<arch>/` and `include/wolftrust/arch/<arch>/` | Context switching, exception entry, Secure transport, gateway implementation, privilege changes, and MPU mechanics |
| MCU and board | `port/<target>/` | Security attribution, clocks, timers, flash, entropy, IRQ routing, device registers, memory map, guest table, and manifest |
| Build | `mk/secure-<arch>-<target>.mk` | Toolchain, source selection, generated manifest, linker layout, and image checks |
| Guest integration | `tests/firmware/` or an application repository | Non-secure linker layout, PSA client shim, CMSE import library, and OS wiring |

## Architecture contract

The common runtime treats `wt_guest_context_t` as an opaque,
architecture-owned type. An architecture port supplies:

- the concrete guest context and context save/restore path;
- exception entry and return between Secure handlers, Secure threads, and
  Non-secure threads;
- the target Secure-call transport used by scheduled partitions;
- the Non-secure gateway and pointer-security checks;
- guest and Secure Partition MPU programming;
- privilege-state transitions and handler-mode queries; and
- interrupt target, mask, pending, enable, and end-of-interrupt mechanics.

The full function contract is declared in
`include/wolftrust/platform.h`. The Armv8-M implementation is under
`src/arch/armv8m/`, with STM32H563 platform callbacks in
`port/stm32h563/platform_stm32h563.c`.

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

- Secure, NSC, guest, update, and persistent flash regions;
- Secure Partition, SPM, and guest RAM;
- target clocks, timer, UART, RNG, and security peripherals;
- flash erase and write geometry;
- guest vector-table read aliases, if required; and
- WRP or equivalent immutable guest-image protection.

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
6. Link Non-secure guests against the target's CMSE import library and matching
   generated service IDs.
7. Add image assembly that patches guest ID, version, size, and digest records
   before signing wolfTrust.
8. Add safe provisioning tooling for TrustZone attribution, guest-flash
   protection, debug policy, and product lifecycle.

## Validation checklist

- Run `make test` for architecture-neutral behavior.
- Run `WT_SPLIT_STRICT=1 tools/check-core-port-split.sh` and resolve
  hard core-to-architecture leaks.
- Cross-build the Secure image with warnings enabled.
- Inspect `nm` output and confirm only the five FF-M veneers are
  Non-secure-callable.
- Test invalid manifests, memory overlap, pointer ranges, stale handles,
  cross-owner access, and unsupported capabilities.
- Run authenticated boot, guest tamper, rollback, restart, Secure Partition
  fault, storage recovery, and update tests in an architecture-accurate
  emulator when one exists.
- Verify attribution, interrupts, entropy, flash failure, WRP-equivalent
  coverage, reset, and recovery on physical hardware.
- Keep emulator and hardware evidence distinct.

See [[Architecture]], [[Building]], and [[Testing]] for the current reference
implementation.
