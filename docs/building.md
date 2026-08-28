# Building

The default build is the STM32H563 Armv8-M Secure image. `ARCH=armv8m` and
`TARGET=stm32h563` are already the defaults; no board flag is needed for a
first build.

## Prerequisites

For a local build, install:

- GNU Make and Python 3
- an `arm-none-eabi` GCC/binutils toolchain with Arm newlib headers
- a host C compiler for `make test`
- Git for submodules and the pinned upstream test sources

Initialize the checkout once:

```sh
git submodule update --init --recursive
```

CI and target scenarios use
`ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15`. It carries the Arm toolchain and
the M33MU build environment. With submodules present in the checkout, the
plain secure build can run in it like this:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15 make all
```

## First build

```sh
make all
```

The useful outputs are:

- `build/wolftrust.elf`
- `build/wolftrust.bin`
- `build/secure_cmse_implib.o`, linked by Non-secure guests
- `build/manifest/`, generated from the H563 manifest

`all` builds wolfTrust itself. The authenticated wolfBoot-to-guest image is
assembled by the target scenario scripts.

## Targets worth knowing

| Target | What it does |
| --- | --- |
| `make all` | Builds the H563 Secure ELF, binary, CMSE import library, and runs the veneer whitelist |
| `make test` | Runs all host unit and integration suites |
| `make test-target` | Runs `positive`, `restart`, `crossdomain`, and `confboot` under M33MU; skips if M33MU is not detected |
| `make test-conformance` | Fetches the pinned Arm suite; runs full FF-M conformance on M33MU or an explicitly marked host subset without it |
| `make test-hardware` | Builds, flashes, and checks the default H563 silicon scenarios; skips if the board tools are absent |

`make test-conformance` needs network access the first time it fetches the
pinned PSA Architecture Test checkout. Target scenarios also fetch pinned
wolfBoot, M33MU, Zephyr, and FreeRTOS sources as needed.

## Useful knobs

Most first builds need none. These are the ones the top-level build and test
scripts actually consume.

| Variable | Default | Use |
| --- | --- | --- |
| `TOOLPREFIX` | `arm-none-eabi-` | Select another Arm toolchain prefix |
| `BUILD_DIR` | `build` | Put top-level Secure build output elsewhere |
| `CC` | `cc` in host tests | Select the host compiler for `make test` |
| `M33MU` | `m33mu` on `PATH` | Point target tests at a prebuilt emulator |
| `WT_TARGET_SCENARIOS` | `0` | Set to `1` to force target execution and let the runner build M33MU |

For a target run inside the CI image:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15 \
  env WT_TARGET_SCENARIOS=1 make test-target
```

## Hardware knobs

The H563 harness expects an ST-Link, a serial VCP, and
STM32CubeProgrammer. pyOCD is used to reset the board and clear persistent
test state. Its common overrides are:

| Variable | Use |
| --- | --- |
| `WT_H5_DOCKER_IMAGE` | Build in the same container used by CI |
| `STM32_CLI` | Path to `STM32_Programmer_CLI` |
| `H5_SERIAL` | Serial device, default `/dev/ttyACM0` |
| `ARM_NM` | Path to `arm-none-eabi-nm` for fault checks |
| `WT_H5_SCENARIOS` | Space-separated silicon scenarios to run |

The default silicon set is `positive restart crossdomain confboot`:

```sh
WT_H5_DOCKER_IMAGE=ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15 \
  make test-hardware
```

To run the supported PSA service conformance cases on the board as well:

```sh
WT_H5_DOCKER_IMAGE=ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15 \
WT_H5_SCENARIOS="devstorage devcrypto devattest" \
  make test-hardware
```

Read the final `PASS`, `FAIL`, or `SKIP` line. A skipped target is not test
evidence.
