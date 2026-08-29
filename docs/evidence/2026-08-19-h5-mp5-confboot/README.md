# MP5 evidence - unmodified Arm val conformance on STM32H563 silicon, 2026-08-19

Raw outputs backing the validation-log "MP5" entry and the task-list MP5
completion marks. **HARDWARE, not emulator**: NUCLEO-H563ZI (ST-Link SWD +
`/dev/ttyACM0` VCP) on the lab box.

The unmodified Arm psa-arch-tests FF-M IPC suite (pinned SHA `e17d294`, val
NSPE + test bodies compiled as-is, zero test edits) ran against wolfTrust's
production SPM on the board via the new `confboot` hardware scenario:
**TOTAL TESTS 89 / PASSED 85 / FAILED 0 / SKIPPED 4** (4 = heap tests,
zero-allocation image). The chain rebooted 92 times through real SYSRESETREQ
panic-resets, val resuming off its flash-backed boot flag each time.

| File | What it is |
| --- | --- |
| `hardware-confboot-run.log` | official `run_h5_hardware.sh flash confboot`: CubeProgrammer flash + full UART capture + the 5 `[check] PASS` assertions ending `PASS: hardware/h5/confboot` |
| `uart-first-green-run.log` | raw UART of the first green run (92 boot cycles, ACS report at the end) |

Three silicon defects found and fixed on the way (none reproducible under
M33MU, which models no SECWM, needs no physical reset line, and starts every
run with fresh flash):

1. **SECWM1 watermark short.** The board was provisioned `SECWM1 0x00-0x3F`,
   so the secure watermark ended at 0x08080000 while the boot partition runs
   to 0x0C0A0000. Flash writes past the watermark through the secure alias
   were silently dropped: any secure image over 128K (the conformance image is
   140.6K signed) was truncated on flash and wolfBoot integrity-rejected it
   (`hdr_ok=1, sha_ok=0, not_sha_ok=1`, panic in `wolfBoot_start`). Fix:
   `SECWM1_END=0x4F` (secure through 0x0809FFFF - the whole boot partition;
   guests at 0x080A0000 stay NS). `provisioning_ctrl.sh` and the hardware plan
   now carry 0x4F.
2. **CubeProgrammer `-hardRst` unreliable.** After flashing, the board was
   left parked in its pre-flash state (a stale wolfBoot panic), which
   masqueraded as a persistent boot failure. `run_h5_hardware.sh` now always
   issues an explicit `pyocd -c reset` after the flash verify.
3. **Stale flash boot-flag state across runs.** The panic tests resume off a
   flash-backed boot flag in a reserved secure sector (0x0C1FA000, K2/K3).
   Unlike the emulator (fresh flash every run), the board keeps last run's
   counters, so a *second* back-to-back confboot inherited stale state and ~2
   panic tests misresumed as `SIM ERROR` (seen once as 83 passed / 2 SIM ERROR
   - 83+4+2=89, zero real conformance failures). Fix: `run_h5_hardware.sh`
   erases that sector (`pyocd erase -s 0x0C1FA000`) before each confboot run.
   Verified deterministic: two back-to-back runs both 85/0-sim-error, then the
   full 4-scenario `make test-hardware` with confboot last.

Assertion note: both guests raw-write USART3 and every one of the 92 reboots
splices the boot banners mid-word, so the confboot gate is the ACS report
block alone (it prints once in the quiet end window and cannot exist unless
val ran the suite end to end).
