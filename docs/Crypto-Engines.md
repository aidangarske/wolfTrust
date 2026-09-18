# Crypto Engines

wolfTrust provides two Secure crypto engines behind the same FF-M service
boundary. The native crypto engine is the default. The wolfHSM engine is an
opt-in add-on for deployments that need the wolfHSM client/server key model or
its external-HSM integration path.

The engine choice does not change the Non-secure-to-Secure trust boundary.
Both builds use the same five CMSE veneers, generated manifest, service IDs,
SPM-owned caller identity, copied IOVEC rules, Level 3 isolation domains,
storage services, attestation service, firmware-update service, and Secure
Partition recovery path.

## At a glance

| | Native crypto engine | wolfHSM engine |
| --- | --- | --- |
| Selector | `WT_ENGINE=native` (default) | `WT_ENGINE=hsm` |
| Guest PSA Crypto | wolfPSA and wolfCrypt execute in the Non-secure guest; DRBG seed requests cross into the Secure vault | wolfPSA routes supported operations through the wolfHSM client and `SERVICE_HSM` to a per-guest Secure wolfHSM server |
| Secure key service | Native request format dispatches wolfCrypt directly; explicitly vault-backed keys are NVM objects | wolfHSM request format dispatches the wolfHSM server and its keystore |
| Guest volatile PSA keys | Held in the guest's Non-secure memory | Keys for supported offloaded operations are held by the Secure wolfHSM server |
| Persistent Secure keys | Native vault objects marked `SENSITIVE` and `NONEXPORTABLE` | wolfHSM server-keystore objects with wolfHSM key policy |
| Attestation IAK | Vault-backed P-256 key used directly by wolfCrypt | Committed wolfHSM server-keystore key |
| External HSM path | Not provided by this engine | Available through the wolfHSM server model when a deployment configures a backend; the reference build uses software wolfCrypt |
| Secure-image footprint | Lower | Adds the wolfHSM protocol, server, per-guest contexts, and per-guest server stacks |

The native engine is not a mode in which every guest PSA key automatically
moves into the Secure vault. In the reference guests, ordinary wolfPSA
operations and volatile keys remain local to the Non-secure guest. The native
wire is a separate explicit interface for vault-backed key operations. Secure
services such as attestation use that vault backend directly.

## One service seam

`SERVICE_HSM` keeps its existing name and SID in both builds. The service
copies one request into Secure memory, obtains the caller identity stamped by
the SPM, and passes the opaque packet to the engine selected at build time:

```text
Non-secure guest
  |
  | psa_connect / psa_call / psa_close
  v
five WolfTrust_FFM_* CMSE veneers
  |
  v
SPM: caller identity + manifest policy + copied IOVECs
  |
  v
SERVICE_HSM protocol-opaque relay
  |
  +-- WT_ENGINE=native --> native request --> wolfCrypt + vault key backend
  |
  `-- WT_ENGINE=hsm ----> wolfHSM packet --> per-guest wolfHSM server
```

The binding is the `wt_hsm_relay_set_submit()` call in
`src/spm_partitions.c`. It selects `wt_native_submit()` or
`wt_hsm_relay_submit()`. `src/services/hsm_relay_service.c` does not interpret
either protocol. It enforces the copied request and response bounds and passes
the SPM-stamped client ID to the selected backend.

## Native crypto engine

The native engine links wolfCrypt and the shared flash-backed NVM object store,
but not the wolfHSM server, communication layer, or message layer.

The reference guest configuration behaves as follows:

- wolfPSA and wolfCrypt execute locally in each Non-secure guest;
- wolfCrypt DRBG seed material comes from the Secure vault RNG through one
  `SERVICE_HSM` call;
- ITS, Protected Storage, attestation, and firmware update continue to use
  their normal Secure services; and
- clients that need a vault-backed key can use the native request format
  explicitly.

Vault-backed P-256 and AES-256 key objects are indexed by the
`SERVICE_HSM` partition identity, the SPM-stamped client identity, and a
64-bit UID. They are stored with the wolfHSM NVM library's `SENSITIVE` and
`NONEXPORTABLE` flags. The storage face refuses key-flagged objects, checked
NVM reads reject non-exportable objects, and the native request format has no
private-key export operation. Private-key computations run in the Secure
key-vault domain and temporary plaintext key buffers are zeroized after use.

### Native request format

One request is a 24-byte `wt_crypto_wire_req_t` followed by an optional
payload. One response is a 32-bit PSA status followed by an optional payload.
Each complete request and response is bounded by the 384-byte
`WT_HSM_RELAY_MSG_MAX` copied buffer.

| Request field | Type | Meaning |
| --- | --- | --- |
| `uid` | `uint64_t` | Key UID for key operations |
| `op` | `uint32_t` | Operation number |
| `usage` | `uint32_t` | Key-usage bits, or requested length for `RANDOM` |
| `key_type` | `uint32_t` | Native P-256 or AES-256 key encoding |
| `reserved` | `uint32_t` | Reserved; clients set it to zero |
| payload | bytes | Imported key, digest, signature, plaintext, ciphertext, or hash input as required by the operation |

The defined operations are key generate, import, public export, sign, verify,
encrypt, decrypt, and destroy, plus random generation and SHA-256 hashing.
Random responses are limited to 256 bytes per request. P-256 signatures use a
fixed 64-byte `r || s` form and public keys use the 65-byte uncompressed X9.63
form. AES-256 encrypt and decrypt use AES-GCM and return or consume
`nonce || ciphertext || tag`.

The structures are copied directly between the reference Cortex-M client and
Secure image. This is a target-local ABI, not a versioned network protocol.
The header and operation values are declared in
`include/wolftrust/services/crypto_native.h`.

## wolfHSM engine

The wolfHSM engine links the wolfHSM client/server protocol and creates one
Secure server context for each configured guest. Guest wolfPSA calls use
wolfCrypt's crypto-callback path, the wolfHSM client serializes the request,
and the request crosses the same `SERVICE_HSM` FF-M door used by the native
engine.

The relay derives guest `N` from SPM client ID `-(N + 1)` and forces wolfHSM
server client ID `N + 1`. It rejects guest-facing wolfHSM NVM message groups,
so a guest cannot use the crypto door to read vault, rollback, storage-counter,
or attestation objects. The server keystore provides wolfHSM's key lifecycle,
namespace, and non-exportable-key behavior.

The reference engine runs wolfCrypt in the Secure image. Choosing
`WT_ENGINE=hsm` does not by itself select an external device; it retains the
wolfHSM server integration point for a deployment that supplies one.

## Choosing an engine

Use the native crypto engine when:

- Secure flash or SRAM is constrained;
- guest-local wolfPSA and wolfCrypt execution is acceptable;
- the application only needs Secure entropy, the explicit vault-key
  interface, and the other wolfTrust Secure services; or
- the deployment does not need the wolfHSM client/server protocol.

Use the wolfHSM engine when:

- guest PSA operations and their private key state must execute in the Secure
  wolfHSM server rather than in guest RAM;
- existing code depends on the wolfHSM client wire or server-keystore
  semantics; or
- the deployment needs wolfHSM's external-HSM integration path.

The wolfHSM engine adds a key-management and offload model on top of the same
isolation boundary. The native engine is not a weaker FF-M gateway or a
reduced-isolation build.

## Selecting the engine

Build the Secure image with a fresh output directory for each engine:

```sh
make secure-image WT_ENGINE=native BUILD_DIR=build-native
make secure-image WT_ENGINE=hsm BUILD_DIR=build-hsm
```

`WT_ENGINE=native` is the default, so an unset selector builds the native
engine. The legacy selector remains accepted:

| Legacy setting | Equivalent selector |
| --- | --- |
| `WT_ENGINE_HSM=0` | `WT_ENGINE=native` |
| `WT_ENGINE_HSM=1` | `WT_ENGINE=hsm` |

The Secure image and both guest images must use the same engine. See
[Building](Building.md) for the Zephyr and FreeRTOS guest settings.

## Measured Secure-image cost

These are Secure-image measurements from `wolf-prec5560` using
`arm-none-eabi-gcc` 13.2.1, `-Os`, and `arm-none-eabi-size`. Flash is
`text + data`; static RAM is `data + bss`. Both images contain the full
Level 3 reference configuration with ITS, Protected Storage, firmware update,
vault services, and COSE attestation. wolfBoot and Non-secure guest images are
not included.

| Engine | Flash | Static RAM |
| --- | ---: | ---: |
| Native | 86,240 bytes | 27,213 bytes |
| wolfHSM | 106,432 bytes | 59,073 bytes |
| wolfHSM overhead | 20,192 bytes | 31,860 bytes |

### Stack contribution

With the default `WT_MAX_GUESTS=2`, the wolfHSM engine adds one server tasklet
stack slot per guest:

```text
2 * (10,240-byte stack + 256-byte underflow guard) = 20,992 bytes
```

The stack payload is therefore 20,480 bytes and the guards add 512 bytes. The
linked wolfHSM image reports `g_co_stack_slots` as `0x5200` bytes, matching the
calculation. A one-guest build allocates one 10,496-byte slot. The rest of the
31,860-byte static-RAM difference is server, protocol, crypto, and per-guest
context state.

`WT_CO_STACK_SIZE` defaults to 10,240 bytes. The positive, Crypto-validation,
and FF-M conformance M33MU workloads also passed at 8 KiB with PSPLIM overflow
detection enabled, so the default retains at least 2 KiB of measured margin
per wolfHSM server tasklet. This is a fixed allocation, not a heap or a claim
that every run consumes all 10 KiB.

See [TF-M Compatibility](TF-M-Compatibility.md) for the complete local
footprint comparison and methodology.

## Build invariants

Both engine builds enforce the following after linking:

1. `mk/arch-armv8m.mk` runs `arm-none-eabi-nm` and writes the complete symbol
   list to `BUILD_DIR/nsc-syms.txt`.
2. The link check rejects any `__acle_se_*` symbol outside this exact `nm`
   set: `__acle_se_WolfTrust_FFM_FrameworkVersion`,
   `__acle_se_WolfTrust_FFM_ServiceVersion`,
   `__acle_se_WolfTrust_FFM_Connect`, `__acle_se_WolfTrust_FFM_Call`, and
   `__acle_se_WolfTrust_FFM_Close`.
3. A separate count check requires exactly five `__acle_se_*` symbols, so a
   missing veneer also fails the build.
4. The same symbol list is searched for `malloc`, `free`, `calloc`,
   `realloc`, `_sbrk`, `_malloc_r`, and `_free_r`; finding one fails the
   zero-heap Secure-image build.

The measured native and wolfHSM images contain exactly those five veneers and
none of the guarded heap symbols. The source profile also defines
`NO_WOLFSSL_MEMORY` and `WOLFSSL_NO_MALLOC`.

See [Security Model](Security-Model.md) for the common boundary and
[Testing](Testing.md) for the engine test matrix.
