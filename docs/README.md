# wolfTrust documentation

wolfTrust is an on-chip secure runtime for Armv8-M TrustZone-M systems. It
replaces the TF-M runtime in a wolfBoot-based chain, mediates Non-secure PSA
requests through an FF-M SPM, and keeps security-sensitive services and
wolfHSM state in the Secure world. The reference setup boots Zephyr and
FreeRTOS guests on STM32H563.

## Start here

- [Architecture](architecture.md) — boot chain, Secure/Non-secure split,
  SPM, services, and the guest crypto path
- [Security model](security-model.md) — what hardware, build checks, and tests
  enforce, plus the current limits
- [Building](building.md) — toolchain, container, useful targets, and the few
  knobs needed for a first run
- [Testing](testing.md) — host, M33MU, and STM32H563 evidence without mixing
  emulator results with silicon results

## Porting and traceability

- [Port contract](port-contract.md) — the architecture and SoC interfaces a
  port supplies
- [Adding a port](adding-a-port.md) — the short bring-up checklist
- [VNET integration notes](vnet/integration_notes.md) — the optional virtual
  networking data path
- [Requirements](requirements/README.md) — the traceability ledger: approved
  sources, requirement IDs, compatibility decisions, and validation records

The requirements ledger is the normative record. These pages are the quick
tour of the implementation that exists in the tree.
