# Requirements register

This directory is the only requirements input provided to the clean-room
implementation lane in addition to the approved public specifications listed
in `sources.md`.

## Requirement format

Each requirement uses the following fields:

| Field | Meaning |
| --- | --- |
| ID | Stable `WT-<AREA>-<NUMBER>` identifier. |
| Behavior | Externally observable required behavior. |
| Failure | Required error or fail-closed behavior. |
| Rationale | Security or compatibility reason. |
| Source | Approved specification section or `WOLFTRUST-ORIGINAL`. |
| Tests | Tests that demonstrate the requirement. |
| Commit | Implementing commit after review. |

Requirements are grouped by framework, isolation, Crypto, storage,
attestation, firmware update, Platform services, boot, and portability.

The initial original requirements are in [system.md](system.md) and
[portability.md](portability.md). The clean implementation entry point is
[implementation-handoff.md](implementation-handoff.md). Work is divided into
tested stop points in [phases.md](phases.md).

The compatibility lane may add a requirement only when it can be stated using
public inputs, outputs, and state transitions. Internal implementation details
are not valid requirements.

## Status

The initial register must be completed before production SPM or Secure service
code is written. Empty commit fields are expected until implementation begins.
