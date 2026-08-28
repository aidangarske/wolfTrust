# Testing

wolfTrust uses three evidence tiers. They answer different questions, so a
pass in one tier should not be renamed as a pass in another.

## Evidence tiers

| Tier | Entry point | What it can show | What it cannot show |
| --- | --- | --- | --- |
| Host | `make test` | State machines, policy, wire handling, crypto/storage behavior, and negative cases | Cortex-M exception, MPU, TrustZone, or board behavior |
| M33MU | `make test-target` and `run_m33mu_scenario.sh` | The linked Cortex-M image, wolfBoot chain, FF-M gateway, exception paths, and emulated isolation | Physical STM32H563 behavior |
| H563 silicon | `make test-hardware` | Real SAU/GTZC/MPU, flash, reset, ST-Link, UART, and service behavior on the board | Portability to another chip |

M33MU is an emulator. Its results are target-level Cortex-M evidence, not
silicon evidence. Only `run_h5_hardware.sh` drives the physical H563 board.

## Host suites

`make test` runs the suite list in
[`tests/host/Makefile`](../tests/host/Makefile). It covers the FF-M runtime,
manifest validation, SPM gate, lifecycle and restart logic, guest verification,
wolfHSM relay and key namespaces, attestation, storage, FWU, and more.

The host suites use production architecture-neutral source where possible.
They are fast regression evidence. They do not prove that an MPU region or
TrustZone transition works on Cortex-M.

## M33MU scenarios

`make test-target` runs four baseline scenarios through
[`tests/target/run_m33mu_scenario.sh`](../tests/target/run_m33mu_scenario.sh):

- `positive` — the signed boot chain and normal PSA/FF-M service path complete
- `restart` — a guest spends its restart budget and unrelated work continues
- `crossdomain` — an unprivileged storage partition is denied an SPM-RAM read
- `confboot` — the full Arm FF-M IPC suite runs against the production SPM

The same runner has focused scenario families:

| Family | Scenarios | What they exercise |
| --- | --- | --- |
| Guest parity | `bothpsa`, `bothiso` | The same PSA calls and malformed FF-M requests from Zephyr and FreeRTOS |
| Isolation and recovery | `restart`, `crossdomain`, `spfaultneg` | Guest restart, secure-MPU denial, and in-place Secure Partition recovery |
| PSA conformance | `confboot`, `devstorage`, `devcrypto`, `devattest`, `devattestqcbor` | FF-M plus Storage, Crypto, and Initial Attestation suites |
| Attestation and launch | `attestneg`, `authneg`, `rollbackneg`, `remeasureneg` | Token rejection, hash-pinned launch, downgrade refusal, and post-boot recheck |
| Vault behavior | `vaultrecover`, `vaultrecoversec` | Allowed recovery in an unlocked lifecycle and fail-closed behavior when locked |
| Firmware update | `fwustage`, `bootupdate` | Staging and arming, then a signed wolfBoot swap and measured reboot |

Run a focused case inside the CI container:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15 \
  tests/target/run_m33mu_scenario.sh devcrypto
```

## Arm conformance behavior

`make test-conformance` is intentionally adaptive:

- with M33MU, it runs `confboot`, the full FF-M IPC suite
- without M33MU, it runs only the host client/policy subset and prints a
  warning that the result is partial

The `devstorage`, `devcrypto`, and `devattest` scenarios run the Arm
`dev_apis` suites through the Non-secure reference client. The M33MU runner and
the H563 hardware runner both support them. They are not part of the default
four-scenario `make test-hardware` set; select them with
`WT_H5_SCENARIOS` when silicon evidence is required.

## Hardware suite

The default command is:

```sh
make test-hardware
```

It checks for STM32CubeProgrammer, a serial device, and an ST-Link. If they are
missing, it prints `SKIP` and exits without claiming a pass. The default set is
`positive restart crossdomain confboot`.

Builds can run in the CI container while flashing stays on the host:

```sh
WT_H5_DOCKER_IMAGE=ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15 \
WT_H5_SCENARIOS="positive restart crossdomain confboot" \
  make test-hardware
```

The hardware runner supports the service conformance and update cases listed
in [`run_h5_hardware.sh`](../tests/target/run_h5_hardware.sh). Hardware logs
must be recorded separately from emulator logs.

## CI mapping

| Workflow | Coverage |
| --- | --- |
| [`unit-tests.yml`](../.github/workflows/unit-tests.yml) | One job per host suite, host FF-M subset, and CBOR interop |
| [`compiler-matrix.yml`](../.github/workflows/compiler-matrix.yml) | Aggregate host tests across GCC and Clang versions |
| [`sanitizers.yml`](../.github/workflows/sanitizers.yml) and [`valgrind.yml`](../.github/workflows/valgrind.yml) | Host memory and undefined-behavior checks |
| [`cross-compile.yml`](../.github/workflows/cross-compile.yml) | Cortex-M33 firmware build |
| [`stm32h563-build.yml`](../.github/workflows/stm32h563-build.yml) | wolfBoot, Zephyr and FreeRTOS lifecycles, and the full M33MU scenario matrix |
| [`pr-m33mu-select.yml`](../.github/workflows/pr-m33mu-select.yml) | Label-selected or manually selected M33MU coverage for a PR |
| [`nightly.yml`](../.github/workflows/nightly.yml) | Calls the host, integration, cross-build, and full M33MU workflows |

The heavy M33MU workflow is not a normal pull-request job. It runs on its
configured integration branches, nightly, by manual dispatch, or through the
PR selector. GitHub Actions does not run the physical board suite; that remains
a separately recorded lab result.
