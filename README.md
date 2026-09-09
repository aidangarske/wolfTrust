# wolfTrust

wolfTrust is a Secure Partition Manager (SPM) for Armv8-M TrustZone systems.
In the STM32H563 reference chain, wolfBoot authenticates wolfTrust. TrustZone
isolates wolfTrust and its Secure services from Non-secure guests, while STM32
GTZC attribution isolates each guest's RAM. wolfTrust also provides
interprocess communication (IPC) through the Arm Platform Security Architecture
(PSA) Firmware Framework for M (FF-M), lifecycle management, fault recovery,
and PSA services.

The Zephyr and FreeRTOS reference guests use wolfPSA's PSA Crypto API. Requests
cross a single Cortex-M Security Extensions (CMSE) gateway comprising five
veneers, then reach wolfCrypt and per-guest wolfHSM key namespaces. Additional
Secure services provide storage, COSE Initial Attestation, firmware update, and
optional wolfIP virtual networking.

## Quick start

Prerequisites are GNU Make, Python 3, Git, a native C compiler, and an
`arm-none-eabi-` toolchain. Some submodule URLs use GitHub SSH.

```sh
git clone --recurse-submodules https://github.com/wolfSSL/wolfTrust.git
cd wolfTrust
git submodule update --init --recursive
make
make test
```

The Secure build produces `build/wolftrust.elf`,
`build/wolftrust.bin`, and `build/secure_cmse_implib.o`.
Target runners assemble the authenticated wolfBoot-to-wolfTrust image and
patch signature-covered guest measurements before signing.

Build both PSA reference guests with:

```sh
make -C tests/firmware/zephyr-stm32h5 clone
make -C tests/firmware/zephyr-stm32h5 \
    build-guest0-psa build-freertos-guest1
```

Common validation entry points:

```sh
make test
make test-target
make test-conformance
WT_H5_DOCKER_IMAGE=ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15 make test-hardware
```

Target commands skip explicitly when M33MU is unavailable. Hardware commands
skip when detection fails; on hosts without `lsusb`, a missing ST-Link can
instead surface as a flash failure. Hardware commands can flash and reset the
connected board. Use the disposable build container for the published hardware
workflow; the current direct-host build path changes the user's global Git
`safe.directory` configuration.

## Documentation

The [GitHub wiki](https://github.com/wolfSSL/wolfTrust/wiki) is the primary
documentation home:

- [Getting Started](https://github.com/wolfSSL/wolfTrust/wiki/Getting-Started)
- [Architecture](https://github.com/wolfSSL/wolfTrust/wiki/Architecture)
- [Security Model](https://github.com/wolfSSL/wolfTrust/wiki/Security-Model)
- [API Reference](https://github.com/wolfSSL/wolfTrust/wiki/API-Reference)
- [Services](https://github.com/wolfSSL/wolfTrust/wiki/Services)
- [TF-M Compatibility](https://github.com/wolfSSL/wolfTrust/wiki/TF-M-Compatibility)
- [Building](https://github.com/wolfSSL/wolfTrust/wiki/Building)
- [Testing](https://github.com/wolfSSL/wolfTrust/wiki/Testing)
- [Porting](https://github.com/wolfSSL/wolfTrust/wiki/Porting)
- [STM32H5 Guide](https://github.com/wolfSSL/wolfTrust/wiki/STM32H5-Guide)

The source tree is authoritative:

| Path | Authority |
| --- | --- |
| `include/` | Public APIs and integration contracts |
| `src/` | Runtime and Secure service behavior |
| `port/` | Target policy, memory layout, flash, entropy, and hardware enforcement |
| `mk/` | Build configuration and linked-image checks |

The Markdown sources published to the wiki are in `docs/`.

## License

wolfTrust is licensed under the
[GNU General Public License, version 3 or later](LICENSE).
Submodules under `lib/` retain their own licenses.
