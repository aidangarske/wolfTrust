# STM32H563 product-state lock ladder — walkthrough & example output (MP3)

Driving wolfTrust up the STM32H5 OEM-iRoT lock ladder and back with
`tests/target/provisioning_ctrl.sh`, on a real NUCLEO-H563ZI. Proves the script
seals wolfTrust into each locked product state and reverses it with a Debug
Authentication (DA) certificate Full Regression. Raw console:
`2026-08-19-lock-ladder.log` (+ recovery `2026-08-19-recovery-cert-regression.log`).

## The ladder (RM0481)
Advance = write `PRODUCT_STATE`; the only way back down is a DA regression
(mass-erase). One-way each direction.

| State | Code | Debug | Reversible |
| --- | --- | --- | --- |
| Open | `0xED` | all open, free reflash | baseline |
| Provisioning | `0x17` | secure debug closed, NS open | DA regression |
| TZ-Closed | `0xC6` | TrustZone sealed | DA regression |
| Closed | `0x72` | debug fully closed | DA full regression |
| Locked | `0x5C` | permanent | **never — brick** |

## Safety gate (non-negotiable)
Before advancing to a deeper rung, `discover` (DA `debugauth=2`, non-destructive)
MUST show integrity `0xeaeaeaea` and `(a/14) Full Regression`. If it does not,
stop and regress. `provisioning_ctrl.sh advance` refuses Locked (`0x5C`).
Credential is the **certificate** OBK (`DA_Config.obk`) because wolfTrust runs
TZEN enabled (AN6008 pairing rule).

## Walkthrough

### Stage 1 — advance Open → Provisioning (`0x17`)
`provisioning_ctrl.sh advance 0x17`
```
ADVANCING product state 0xED -> 0x17 (regress is the only way back)
Option Bytes successfully programmed
now: 0x17
```

### Stage 2 — provision the certificate OBK
`provisioning_ctrl.sh provision-da`  (rejected in Open; valid only in Provisioning)
```
Secure Data Provisioning Start. OBK Input file : .../DA/Binary/DA_Config.obk
[====...====] 100% OBKey Provisioned successfully
```

### Stage 3 — GATE: discover
`provisioning_ctrl.sh discover`
```
discovery: PSA lifecycle...................:ST_LIFECYCLE_PROVISIONING
discovery: ST provisioning integrity status:0xeaeaeaea
discovery: permission if authorized........:(a/14) ==> Full Regression
discovery: permission if authorized........:(b/12) ==> To TZ Regression
Debug Authentication: Discovery Success
```
GATE PASS → safe to climb.

### Stage 4 — advance Provisioning → TZ-Closed (`0xC6`)
`provisioning_ctrl.sh advance 0xc6`
```
ADVANCING product state 0x17 -> 0xc6 (regress is the only way back)
Error: failed to reconnect after reset !          <-- benign (state advance closes debug)
```
GATE (discover):
```
discovery: PSA lifecycle...................:ST_LIFECYCLE_TZ_CLOSED
discovery: ST provisioning integrity status:0xeaeaeaea
discovery: permission if authorized........:(a/14) ==> Full Regression
Debug Authentication: Discovery Success
```
The "failed to reconnect" after an advance is expected — the deeper lock drops
the debug link; the write landed and the gate confirms `TZ_CLOSED` with the
credential intact.

### Stage 5 — TZ-Closed → Closed CANNOT be chained
`advance 0x72` from TZ-Closed failed at the connect:
```
Error: Unable to get core ID
Error: Cannot connect to access port 1!
discovery: PSA lifecycle...................:ST_LIFECYCLE_TZ_CLOSED   <-- still TZ-Closed
```
Once at TZ-Closed the debug link is too locked to write the next `PRODUCT_STATE`.
**Finding:** don't step through TZ-Closed; advance to the target lock state
**directly from Provisioning** (this is exactly what ST's `provisioning.sh`
does — one final-state write while the link is still open).

### Stage 6 — certificate Full Regression works from TZ-Closed
`provisioning_ctrl.sh regress` from TZ-Closed authenticated and mass-erased:
```
SDMAuthenticate: Authentication successful
Debug Authentication Success
```
then `restore` → `[check] PASS wolfTrust chain boots on silicon`, `PRODUCT_STATE=0xED`.

### Closed (`0x72`) round-trip — advanced DIRECTLY from Provisioning
Fresh Provisioning + cert OBK, then `advance 0x72` directly:
```
ADVANCING product state 0x17 -> 0x72
Error: failed to reconnect after reset !            <-- benign, deeper lock drops the link
discovery: PSA lifecycle...................:ST_LIFECYCLE_CLOSED     <-- reached full Closed
discovery: ST provisioning integrity status:0xeaeaeaea
discovery: permission if authorized........:(a/14) ==> Full Regression
```
`regress` from **Closed**:
```
SDMAuthenticate: Authentication successful
Debug Authentication Success
```
Full Regression mass-erases and the MCU self-resets. Note: the *immediate*
reconnect after a Closed mass-erase can race the reset and report "Cannot
connect to access port 1"; a moment later it reads `PRODUCT_STATE=0xED (Open)`,
`TZEN=0xC3`, integrity `0xf5f5f5f5` (the erased-DA marker — normal at Open, no
DA is provisioned). Re-running `restore` then succeeds:
`[check] PASS wolfTrust chain boots on silicon`, final `PRODUCT_STATE=0xED`,
`TZEN=0xB4`.

## Result — all three lock rungs proven on silicon
| Rung | Sealed | Certificate regression → Open | wolfTrust restored |
| --- | --- | --- | --- |
| Provisioning `0x17` | ✅ | ✅ | ✅ |
| TZ-Closed `0xC6` | ✅ (from Provisioning) | ✅ | ✅ |
| Closed `0x72` | ✅ (direct from Provisioning) | ✅ | ✅ |

Board left at the working baseline: **Open (`0xED`), TZEN enabled, wolfTrust
booting**. `Locked (0x5C)` never touched. Two operational rules learned:
advance to the lock state directly from Provisioning (TZ-Closed can't chain),
and after a Closed regression give the MCU a moment (or tap NRST) before the
reconnect.
