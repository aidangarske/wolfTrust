# wolfTrust competitive positioning — vs TF-M and STM32 Secure Manager

Status: living doc. The six-point edge and the reframe are settled positioning.
The **immutability/lock mechanism** and the **cross-vendor landscape** carry a
verification status and are confirmed against authoritative specs before we make
them externally (see "Verification status" below).

## The concern, answered

The worry was "TF-M / Secure Manager ships pre-installed and can't be replaced."
Verified against ST's own docs, that premise does not hold: standard STM32H5
parts ship **blank** (product state "Open"), and Secure Manager is **installed by
the customer** on their own line (via SMAK / `X-CUBE-SEC-M-H5`), not preloaded at
the factory. So the real battleground is the **install decision** — exactly where
wolfTrust competes head-on. The only genuinely immutable, factory-fixed piece is
the tiny **SMiRoT boot-ROM stage** on Secure-Manager-capable silicon; everything
above it is a customer choice.

**"Replace" therefore means: at manufacturing, the customer chooses wolfTrust as
the Root of Trust and locks it down themselves — the same immutability, but
OEM-owned.**

## What STM32 Secure Manager actually is (verified 2026-08-18)

- **Not TF-M.** It is a *proprietary* secure OS (ST + ProvenRun's **ProvenCore-M**
  microkernel), "compatible with TF-M PSA APIs" but **not** built on open-source
  Trusted Firmware-M. (ST maintains an open-source TF-M port for the H5
  *separately* — a distinct DIY option, not Secure Manager.) Sharpens our
  **auditable / open** edge: Secure Manager is a closed proprietary kernel.
- **Customer-installed, not pre-shipped.** Parts ship blank; the OEM flashes
  Secure Manager via SMAK. No factory-preloaded "-SM" ordering SKU found.
- **H573-class only.** Requires the crypto-accelerator silicon (SAES/PKA/OTFDEC)
  + native STiRoT — **explicitly not supported on the STM32H56x line.** The
  Nucleo-H563ZI **cannot run Secure Manager at all**; a head-to-head baseline
  needs an **STM32H573I-DK**.
- **ST owns the platform RoT.** The immutable SMiRoT uses ST-defined ROM keys;
  the OEM owns only application-layer keys. Confirms the **sovereignty** edge —
  with wolfTrust the OEM owns the *entire* RoT.
- **Removal = RDP regression + full mass-erase** back to blank, not a surgical
  uninstall.
- **NS API:** PSA Crypto / ITS / Initial Attestation / Firmware Update
  (PSA-compatible; ST does not brand it "FF-M IPC" by name).

## Six-point edge vs TF-M / STM32 Secure Manager

1. **Portability.** Secure Manager is ST-only, mostly H573. wolfTrust runs on
   **every Armv8-M part** — all STM32 (H5/U5/L5/…), and Nordic, NXP, Renesas,
   Microchip. One RoT across your whole product line; Secure Manager chains you
   to specific ST SKUs.
2. **RoT sovereignty.** With Secure Manager, **ST holds a root of trust inside
   your product** and signs the immutable firmware. With wolfTrust, **you own the
   keys, provisioning, signing, and update policy.** For defense / critical-infra
   / regulated customers, an ST-owned RoT is a non-starter — this alone wins
   deals.
3. **Certified crypto you may already need.** wolfTrust rides
   wolfCrypt/wolfBoot/wolfHSM/wolfPKCS11/wolfCOSE — **FIPS 140-3, DO-178**, etc.
   Secure Manager is ST's stack. Customers standardized on wolfSSL/FIPS get one
   certified core everywhere.
4. **Auditable + customizable.** Secure Manager is a closed immutable ST blob.
   wolfTrust is source-available to the customer — auditable, and they can add
   their own secure partitions/services. High-assurance buyers pick the auditable
   RoT over the black box.
5. **Same PSA API = zero switching cost.** wolfTrust exposes the identical PSA
   Functional API (FF-M IPC, PSA Crypto/Attestation/ITS), so the Non-secure app
   is unchanged — a genuine drop-in.
6. **The blank-part reality.** The vast majority of H5 (and *all* non-H573 STM32,
   *all* non-ST M33) ship **blank** — nothing to rip out; wolfTrust is simply the
   secure firmware you install. The "can't replace a locked Secure Manager" case
   is a tiny corner of the market.

## Immutability & lock-in — wolfTrust as an OEM-owned immutable RoT (verified)

**Verdict: YES.** An OEM can build and lock its *own* immutable RoT on a standard
STM32H5 using the *same silicon primitives* ST uses for Secure Manager, with the
OEM owning the code and keys. Verified against ST docs + ST-staff community posts
+ wolfBoot docs.

**Mechanism (identical to ST's own):**
- **OEMiRoT / OEMuRoT** — the OEM compiles its own immutable RoT from ST's
  MCUboot-based `OEMiRoT_Boot` project, generates and owns *all* root keys, and
  seals it into user flash at **HDPL1**. Secure Manager is confirmed to be ST's
  *pre-built instance of the same machinery* — an ST engineer: "SMiRoT is
  basically the same code but located in user flash," protected by the same HDP +
  WRP. So "same guarantee, OEM-owned" is literally true.
- **HDP / HDPL** (hide protection) — a boot stage's code+keys become unreadable to
  anything at a higher level, *including later secure code*; locked until reset.
- **SECWM** + **WRP** — carve the RoT's flash secure and write-immutable.
- **BOOT_UBE + SECBOOT_LOCK** — force then freeze the boot entry at the RoT.
- **Product state** — Open → Provisioning → TZ-Closed → **Closed** (debug locked,
  *reversible*) → **Locked** (permanent).

**Two must-fixes vs earlier phrasing:**
- **No "RDP 1" on H5.** ST replaced RDP entirely with `PRODUCT_STATE`. The
  reversible lock is **`Closed`/`TZ-Closed`**; the one-way permanent lock is
  **`Locked`**.
- **⚠️ Regression footgun:** `Closed` reverses *only if a Debug-Authentication
  password/OBK was provisioned first*. Skip it and `Closed` is a permanent brick
  (documented ST case on an H563). The demo must provision the DA credential
  before every lock.

**wolfBoot's role (verified):** wolfBoot v2.7.0+ ships unified TrustZone-M support
and automates the TZ-partitioning option bytes (`TZEN/SECBOOTADD/NSBOOTADD/SECWM/
WRP`) via `tools/scripts/set-stm32-tz-option-bytes.sh`, running its own
secure-boot verify chain. It does **not** drive ST's HDP/product-state seal — that
OEMiRoT lock-down is a separate CubeProgrammer + STM32TrustedPackageCreator step
we script. The immutability comes from ST's primitives; wolfBoot + wolfTrust ride
on top.

## Cross-vendor RoT landscape (Armv8-M) — verified 2026-08-18

**Headline: of every Armv8-M part surveyed, the STM32H573 is the *only* one that
ships a pre-installed vendor secure stack.** Everything else ships blank — a boot
ROM, a crypto/key IP block, and unprogrammed fuses/OTP — leaving the OEM to
build, sign, and lock its own RoT. And each vendor's lock flow is a *separate*
body of proprietary knowledge.

| Vendor | Part | Pre-installed secure FW? | OEM lock mechanism | One-way permanent lock? | Upstream TF-M port? |
|---|---|---|---|---|---|
| NXP | LPC55S6x | No (boot ROM + PUF, blank OTP) | CMPA/CFPA + ROTKH + Debug Auth | Yes (CMPA `SEAL` / OTP) | Yes |
| NXP | i.MX RT5xx/RT6xx | No | OTP fuses + ROTKH + Debug Auth | Yes (fuses) | No |
| Nordic | nRF5340 | No | NSIB immutable BL + UICR keys + APPROTECT | `ERASEALL`-recoverable | Yes |
| Nordic | nRF54L15 | No | NSIB + KMU key slots + APPROTECT | `ERASEALL`-recoverable | No |
| Nordic | nRF91 | No | NSIB + MCUboot + APPROTECT | `ERASEALL`-recoverable | Yes |
| Renesas | RA6M4/M5 | No | DLM lifecycle + SKMT key inject | Yes (LCK_DBG/LCK_BOOT) | No (downstream fork) |
| Renesas | RA8 | No | Masked FSBL + Secure Factory Programming | Yes (post-SFP) | No |
| Microchip | SAM L11 | No (Trust&Go is a *separate* SE chip) | UROW/BOCOR fuses + DAL + BOOTKEY | Yes (CEHL, forever) | No (uses Trustonic Kinibi-M) |
| Microchip | PIC32CM LS | No | Same UROW/BOCOR/DAL/CEHL + DICE | Yes (CEHL) | No |
| ST | **STM32H573** | **Yes — STiRoT + Secure Manager** | Debug Auth (reversible) / OEM-iRoT | No (DA reversible on H5) | Yes |
| ST | STM32H563 | No (can't run Secure Manager) | RDP/HDP/SECWM/WRP + product state | No (DA reversible) | Yes |
| ST | STM32U5 / L5 | No (OEM builds TF-M) | RDP 0/0.5/1/2 + HDP + WRP | **Yes — RDP2 permanent** | Yes |

**Takeaway.** Only the H573 has a pre-installed stack to "compete with"; every
other part is blank and needs an OEM RoT — and each vendor's lock flow is
non-transferable (NXP CMPA/CFPA, Nordic APPROTECT/KMU, Renesas DLM/SFP, Microchip
CEHL, ST RDP/DA). Upstream TF-M ports exist only for NXP LPC55S69, Nordic
nRF5340/nRF91, and ST H5/U5/L5 — nRF54L15, both Renesas RA families, i.MX RT, and
every Microchip TrustZone part have **none**. **wolfTrust + wolfBoot gives one
PSA-Certified-equivalent secure-firmware architecture and one boot chain that
maps onto each vendor's native lock primitive** — instead of re-deriving a TF-M
integration and a security design per vendor, per part. An OEM standardizes on
one firmware supply chain across NXP / Nordic / Renesas / Microchip / ST rather
than being locked to whichever vendor bakes one in.

_Sourcing caveat: a few NXP/Renesas app-note PDFs were bot-blocked and are
corroborated via secondary sources — spot-check those rows before any
external-facing deck._

## One-line pitch

> "Secure Manager equivalent — same PSA-Certified services, same immutable
> lock-down — but portable across vendors, with a Root of Trust you own, audit,
> and certify on your terms."

## Verification status

- Six-point edge + reframe: **settled.**
- Secure Manager facts (proprietary ProvenCore-M not TF-M; customer-installed;
  H573-class only; ST owns platform RoT): **verified** against ST docs 2026-08-18.
- H5 lock mechanism (OEMiRoT + HDP/SECWM/WRP/BOOT_UBE + product-state, reversible
  via `Closed` with a DA credential): **verified** 2026-08-18. Bit-level
  option-byte encodings (`BOOT_LOCK` vs `SECBOOT_LOCK`, exact `HDPL`/
  `PRODUCT_STATE` value tables, any H563-vs-H573 delta) still need a direct
  **RM0481** pass before external-facing publication.
- Cross-vendor landscape: **verified** 2026-08-18 (spot-check the bot-blocked
  NXP/Renesas app-note rows before an external deck).
- Physical proof: the **P2 lock-in demo** (see the P2 plan) demonstrates it
  reversibly on the Nucleo-H563ZI.
