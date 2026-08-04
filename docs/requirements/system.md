# wolfTrust system requirements

These are original wolfTrust product requirements. They define the system
boundary independently of any reference implementation.

| ID | Behavior | Failure | Rationale | Source | Tests | Commit |
| --- | --- | --- | --- | --- | --- | --- |
| WT-SYS-0001 | wolfBoot authenticates wolfTrust before wolfTrust executes. | Authentication failure prevents entry into wolfTrust. | Establish a verified first Secure runtime. | WOLFTRUST-ORIGINAL | Corrupt-image boot test | |
| WT-SYS-0002 | wolfTrust authenticates every external application or guest domain before launch. | Verification, version, layout, or rollback failure prevents domain entry. | Extend the chain of trust to all executable domains. | WOLFTRUST-ORIGINAL | Guest verification and rollback tests | |
| WT-SYS-0003 | The privileged wolfTrust SPM is isolated from Secure Partitions and Non-secure domains. | Any attempted access faults the caller without exposing SPM memory. | Protect the enforcement authority. | WOLFTRUST-ORIGINAL | SPM read, write, and execute probes | |
| WT-SYS-0004 | Each Level 3 Secure Partition is isolated from every other partition. | Cross-partition memory, MMIO, stack, or interrupt access faults the caller. | Contain a compromised service. | WOLFTRUST-ORIGINAL | Level 3 negative-isolation suite | |
| WT-SYS-0005 | The SPM derives caller identity from the active execution context. | Supplied, forged, stale, or mismatched caller IDs are rejected. | Prevent owner spoofing. | WOLFTRUST-ORIGINAL | Caller and handle forgery tests | |
| WT-SYS-0006 | Security policy, key ownership, and persistent metadata execute only in isolated Secure services. | Direct raw-backend access is unavailable in the Level 3 profile. | Prevent bypass of PSA policy. | WOLFTRUST-ORIGINAL | Backend-bypass link and runtime tests | |
| WT-SYS-0007 | Secure services communicate through bounded SPM-managed messages. | Invalid vectors, overflow, exhaustion, or ownership errors fail without partial output. | Keep untrusted pointers outside service address spaces. | WOLFTRUST-ORIGINAL | IPC malformed-input suite | |
| WT-SYS-0008 | A restartable ARoT partition fault does not corrupt or terminate unrelated domains. | Restart limits or mandatory-service faults trigger fail-closed platform recovery. | Contain recoverable faults without hiding critical failure. | WOLFTRUST-ORIGINAL | Partition fault and restart tests | |
| WT-SYS-0009 | The Level 3 profile statically allocates SPM control objects, stacks, handles, and message slots. | A layout exceeding configured resources fails at build time. | Make resource use bounded and auditable. | WOLFTRUST-ORIGINAL | Maximum-layout generation tests | |
| WT-SYS-0010 | Sensitive context, IPC buffers, key material, and consumed DICE handoff data are zeroized before reuse. | Cleanup failure prevents reuse or domain restart. | Prevent cross-domain data recovery. | WOLFTRUST-ORIGINAL | Zeroization and restart tests | |
| WT-SYS-0011 | A selected security profile cannot exceed declared hardware enforcement capabilities. | Unsupported configurations fail during generation or build. | Avoid unsupported isolation claims. | WOLFTRUST-ORIGINAL | Capability mismatch tests | |
| WT-SYS-0012 | Product artifacts contain no TF-M implementation or generated build product. | Provenance or artifact audit failure blocks release. | Preserve the independent product boundary. | WOLFTRUST-ORIGINAL | Artifact inventory audit | |
