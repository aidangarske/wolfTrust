# Project Structure

| Path | Contents |
| --- | --- |
| `README.md` | Repository overview and quick start |
| `docs/` | Source pages published to the GitHub wiki |
| `include/psa/` | FF-M client/service, status, storage, update, and lifecycle headers implemented by wolfTrust |
| `include/wolftrust/` | Domain, manifest, monitor, port, scheduler, IPC, service, and VNET contracts |
| `src/` | Architecture-neutral monitor, FF-M runtime, domains, manifests, verification, rollback, and recovery |
| `src/arch/armv8m/` | Armv8-M context switching, exception handlers, CMSE gateway, SVC transport, and MPU support |
| `src/client/` | OS-neutral FF-M, storage, firmware-update, HSM, and VNET client transports |
| `src/sched/` | Static coroutine and tasklet scheduling |
| `src/services/` | HSM relay, vault, storage, attestation, firmware update, and VNET service code |
| `src/sync/` | Synchronization primitives used by Secure services |
| `src/vnet/` | Secure virtual Ethernet data plane |
| `port/stm32h563/` | STM32H563 registers, board and memory maps, manifest, flash, entropy, partition tables, and platform callbacks |
| `mk/` | Secure target build fragments |
| `tools/manifest/` | Manifest validation and C/header generation |
| `tools/measure/` | Guest-measurement record patching before image signing |
| `tests/host/` | Native unit and integration suites |
| `tests/target/` | M33MU and STM32H563 build, flash, provisioning, and scenario runners |
| `tests/firmware/` | Bare-metal, Zephyr, FreeRTOS, conformance, and VNET guest images |
| `tests/upstream/` | Fetch and integration helpers for pinned external validation suites |
| `lib/` | Git submodules for wolfSSL, wolfPSA, wolfHSM, wolfCOSE, wolfHAL, and wolfIP |
| `.github/workflows/` | Build, test, dependency, fuzz, and wiki synchronization workflows |

Generated files belong under `build/`, guest build directories,
ignored workspaces, or `logs/`. Public APIs are declared in `include/`;
architecture-neutral implementation is under `src/`; target-specific
implementation is under `port/` and the active build fragment.

See [Porting](Porting.md) for the boundary between these areas.
