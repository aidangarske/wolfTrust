#!/usr/bin/env python3
"""Ingest Arm PSA-FF partition manifests into wolfTrust psa_manifest headers.

Reads one or more upstream psa-arch-tests `*_psa.json` partition manifests and
emits the `psa_manifest/pid.h`, `sid.h`, and per-partition signal headers the
upstream `val`/PAL sources `#include`. Reuses the production generator's header
emitters so the format matches wolftrust_manifest_generated output. Memory
layout (domains, stacks, MMIO placement) is assigned by a later phase; this
stage covers the service identity and signal headers only.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate  # noqa: E402  (reuse validated header emitters)

MODEL_IPC = 0
SIGNAL_FIRST = 0x10
SIGNAL_MAX = 0x80000000

VERSION_POLICY = {
    "STRICT": 2,
    "RELAXED": 1,
}


class IngestError(Exception):
    pass


def parse_sid(value):
    try:
        return int(str(value), 0)
    except ValueError:
        raise IngestError("service sid is not an integer: {}".format(value))


def next_signal(bit):
    if bit >= SIGNAL_MAX:
        raise IngestError("partition exhausts the 31-bit signal space")
    return bit << 1


def normalize_partition(source, domain_id):
    if "name" not in source or "services" not in source:
        raise IngestError("manifest missing name or services")

    services = []
    signal = SIGNAL_FIRST
    for entry in source["services"]:
        if "name" not in entry or "sid" not in entry:
            raise IngestError("service missing name or sid")
        policy = entry.get("version_policy", "STRICT")
        if policy not in VERSION_POLICY:
            raise IngestError("unknown version_policy: {}".format(policy))
        services.append({
            "name": entry["name"],
            "sid": parse_sid(entry["sid"]),
            "version": int(entry.get("version", 1)),
            "version_policy": VERSION_POLICY[policy],
            "non_secure_clients": bool(entry.get("non_secure_clients", False)),
            "signal": signal,
        })
        signal = next_signal(signal)

    interrupts = []
    for entry in source.get("irqs", []):
        if "signal" not in entry:
            raise IngestError("irq missing signal name")
        interrupts.append({
            "signal_name": entry["signal"],
            "signal": signal,
        })
        signal = next_signal(signal)

    return {
        "name": source["name"],
        "domain_id": domain_id,
        "model": MODEL_IPC,
        "services": services,
        "interrupts": interrupts,
    }


def ingest(paths, pid_base):
    partitions = []
    domain_id = pid_base
    for path in paths:
        source = json.loads(Path(path).read_text(encoding="utf-8"))
        partitions.append(normalize_partition(source, domain_id))
        domain_id += 1
    return {"partitions": partitions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--pid-base", type=lambda value: int(value, 0),
                        default=1)
    args = parser.parse_args()

    try:
        manifest = ingest(args.manifests, args.pid_base)
        psa_manifest = args.output / "psa_manifest"
        psa_manifest.mkdir(parents=True, exist_ok=True)
        (psa_manifest / "pid.h").write_text(
            generate.generate_pid_header(manifest), encoding="utf-8")
        (psa_manifest / "sid.h").write_text(
            generate.generate_sid_header(manifest), encoding="utf-8")
        for partition in manifest["partitions"]:
            file_name, content = generate.generate_partition_header(partition)
            (psa_manifest / file_name).write_text(content, encoding="utf-8")
    except (IngestError, OSError, json.JSONDecodeError) as error:
        print("manifest ingestion failed: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
