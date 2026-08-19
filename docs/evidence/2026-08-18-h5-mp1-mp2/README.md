# MP1 + MP2 evidence — STM32H563 hardware + M33MU, 2026-08-18

Raw outputs backing the validation-log entries "MP1 GREEN" and "MP2" and the
task-list MP1/MP2 completion marks. Hardware runs are a NUCLEO-H563ZI
(ST-Link SWD + `/dev/ttyACM0` VCP); emulator runs are the pinned M33MU. All on
the commit series ending `cac65eb`.

| File | What it is |
| --- | --- |
| `hardware-suite.log` | `make test-hardware` (run_h5_suite.sh) top-level: positive + restart + crossdomain all PASS on the board |
| `hardware-positive-run.log` | positive scenario flash run: the 8-check PSA/FF-M lifecycle checklist |
| `hardware-restart-run.log` | restart scenario: monitor restarted the guest exactly 3 times, 1 quarantine (monitor event counters over the debug port), guest1 alive |
| `hardware-crossdomain-run.log` | cross-domain scenario: unprivileged crypto SP denied at 0x30028000 (WT_RAM_S_BASE), no HardFault escalation, guest1 alive |
| `uart-crossdomain-final.log` | raw UART capture of the final crossdomain run (guests interleave on USART3 — console veneer is tracked work) |
| `m33mu-regression.log` | emulator `make test-target`: positive/restart/crossdomain/confboot all PASS on the same source |
| `m33mu-conformance.log` | emulator `make test-conformance`: manifest ingest reproducible + Arm FF-M suite 85 passed / 4 skipped / 0 failed |
