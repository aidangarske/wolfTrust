# Phase validation ledger

This ledger records validation against the exact wolfTrust phase commit. A
M33MU result is emulator evidence for the Cortex-M33 execution model; it is
not a physical-board result. Hardware qualification remains open until a
target is available.

## Phase 3 foundation evidence

Commit: `2bc5f70e69faedfeadca5d45f317ff6a880df6ef`

- Aggregate host validation: `make test BUILD_ROOT=/tmp/wolftrust-phase3-capability-tests`, passed.
- Focused production policy validation: `make -C tests/host/spm BUILD_DIR=/tmp/wolftrust-phase3-spm-capabilities run`, passed.
- Complete local M33MU lifecycle: passed with wolfBoot authentication,
  wolfTrust startup, wolfCOSE Initial Attestation signing and verification,
  Zephyr PSA operations, and FreeRTOS wolfHSM-backed wolfPKCS11 operations.
- Expected terminal marker: `[EXPECT BKPT] Success`.
- Fault markers: none.

Validated dependency and emulator revisions:

- wolfCOSE: `588232e6f2213133b48976f5cf3153b21fc7199c`
- wolfHSM: `a0323156606282448f00473a3fcb7aaa69361921`
- wolfPSA: `dd557dc526ee79f38e5f5c8d8839fb477d045941`
- wolfPKCS11: `6fbb6fbc5251b59280eb5f74bcc9241bd46073e9`
- wolfSSL: `22e505bcfad8ce21067ee4232128728543767a95`
- wolfBoot: `d85fa9dbdf6c36f47b7e96eba5c9df750ad3c963`
- M33MU: `c84792f7f9e9ce24cf94ffc492c36231de1854c2`

Validated image SHA-256 digests:

- Signed wolfTrust: `51d3bb49f9686df8bd01b2543bcac4c66c1e9416aecafda22cd78f36f414cd20`
- Zephyr guest: `60a7c51c08d5503885eb26170eef483fdace52e8c85254d18b65d5e423485daa`
- FreeRTOS guest: `f05d17ecef1df53585ba3c7a491cb7d0070cfbdaf6ee9889f9bca140de1e98aa`

This is emulator evidence for the manifest bootstrap, authenticated boot,
Non-secure guest monitor, and direct secure-service veneers. It is not FF-M
IPC, Secure Partition, or Level 3 isolation acceptance. No physical STM32H563
result is claimed for this commit.

## Current no-hardware gate

Commit: `36d28a5264c145e8aa71dab447db8d397152499c`

- Host validation: [workflow run 31059194072](https://github.com/aidangarske/wolfTrust/actions/runs/31059194072) — passed.
- M33MU lifecycle matrix: [workflow run 31059193766](https://github.com/aidangarske/wolfTrust/actions/runs/31059193766) — passed.
  - wolfBoot authenticated the secure wolfTrust image.
  - Zephyr exercised PSA Initial Attestation, rejected the short buffer,
    received a token, and verified the wolfCOSE COSE_Sign1 signature, challenge,
    identity, lifecycle, and measurement claims.
  - FreeRTOS exercised wolfHSM-backed wolfPKCS11 initialization, slot and
    session setup, and SHA-256 digest.
  - Both lifecycle jobs reached `[EXPECT BKPT] Success` and exited M33MU with
    status 0; no fault marker was emitted.

Physical STM32H563 H5 validation is pending. The board was not available for
the corrected-image run, so no H5 hardware pass or failure is claimed here.

## Item 5 Level 3 isolation evidence (Phase D positive, Phase E negative)

Commits: `69dc8eb` (Phase D — isolated crypto SP compute),
`f15fe11` (Phase E — gated cross-domain probe).

- Host validation: `make test` fully green on both commits, including
  `unit/crypto_service` (copied-IOVEC restructure: isolated-compute KAT +
  over-cap rejection), `unit/ffm_domain`, and `unit/sp_layout`.
- M33MU positive (run #1, Phase D): the crypto Secure Partition runs its
  SHA-256 on `WT_SP_CRYPTO_STACK_BASE` with the secure MPU narrowed to
  `[secure code RX] + [crypto SP stack RW]`. `SERVICE_CRYPTO` returns the
  correct digest, `attestation verify=0 lifecycle=0x1000 measurement=ok`,
  `[EXPECT BKPT] Success`, exit 0, no fault marker.
- M33MU negative (run #2, Phase E, `run_m33mu_negative.sh`, built with
  `WT_FFM_NEGATIVE_PROBE=1`): a probe inside the narrowed SP domain reads
  SPM-private RAM and faults —
  `[MEMFAULT] pc=0x0c060f34 addr=0x30028000 sp=0x3009dff0` (SP stack). The
  initiating context faults with no data exposed. This is the WT-FFM-0011
  failure clause and `framework.md` acceptance-gate negative #1.

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed. Follow-up (tracked): graceful fault recovery so the negative
probe run continues rather than halting, and CI wiring of the negative job.

## Item 6 manifest-authoritative bindings (restart policy, initial lifecycle)

Commits: `22e5461` (restart policy bind), `95c61a1` (initial lifecycle bind +
NS guests declared READY).

- Host validation: `make test` green; `tests/host/spm/test_production_manifest`
  now asserts both fields flow from the generated manifest — a distinct
  `restart_limit` (7) and a `STOPPED` guest domain bind through to the runtime
  config, and the real manifest's `READY` restores a runnable guest.
- M33MU positive: full wolfBoot -> wolfTrust -> guest lifecycle green,
  `[EXPECT BKPT] Success`, exit 0, no fault markers — NS guests still boot
  runnable now that their run state is sourced from the manifest.

Emulator evidence for the Cortex-M33 execution model; no physical STM32H563
result is claimed.

## Phase gate rule

Every implementation phase must repeat host tests and the complete M33MU
matrix on its exact phase commit. When target hardware is available, the same
phase must additionally record a physical-board smoke result covering the
phase behavior.
