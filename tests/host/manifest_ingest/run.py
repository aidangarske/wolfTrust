#!/usr/bin/env python3
"""Assert the Arm PSA-FF manifest ingester emits the expected psa_manifest
identity headers from the real upstream server/driver/client manifests."""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
INGESTER = REPO / "tools" / "manifest" / "ingest_psa_arch.py"

EXPECT_PID = {
    "SERVER_PARTITION_ID",
    "DRIVER_PARTITION_ID",
    "CLIENT_PARTITION_ID",
}

EXPECT_SID = {
    "SERVER_TEST_DISPATCHER_SID": 0x0000FB01,
    "SERVER_SECURE_CONNECT_ONLY_SID": 0x0000FB02,
    "SERVER_STRICT_VERSION_SID": 0x0000FB03,
    "SERVER_UNSPECIFIED_VERSION_SID": 0x0000FB04,
    "SERVER_CONNECTION_DROP_SID": 0x0000FB07,
    "DRIVER_UART_SID": 0x0000FC01,
    "DRIVER_NVMEM_SID": 0x0000FC03,
    "CLIENT_TEST_DISPATCHER_SID": 0x0000FA01,
}

EXPECT_VERSION = {
    "SERVER_STRICT_VERSION_VERSION": 2,
    "SERVER_UNSPECIFIED_VERSION_VERSION": 1,
    "DRIVER_UART_VERSION": 1,
}

EXPECT_DRIVER_SIGNAL = {
    "DRIVER_UART_SIGNAL": 0x10,
    "DRIVER_NVMEM_SIGNAL": 0x40,
    "DRIVER_UART_INTR_SIG_SIGNAL": 0x100,
}

DEFINE = re.compile(r"#define\s+(\S+)\s+(\S+?)U?$", re.MULTILINE)


def defines(path):
    values = {}
    for name, value in DEFINE.findall(path.read_text(encoding="utf-8")):
        try:
            values[name] = int(value, 0)
        except ValueError:
            values[name] = value
    return values


def main():
    if len(sys.argv) < 2:
        print("usage: run.py <psa-arch-tests-dir>", file=sys.stderr)
        return 2
    manifests = Path(sys.argv[1]) / "api-tests" / "platform" / "manifests"
    inputs = [
        manifests / "server_partition_psa.json",
        manifests / "driver_partition_psa.json",
        manifests / "client_partition_psa.json",
    ]
    for path in inputs:
        if not path.is_file():
            print("missing upstream manifest: {}".format(path), file=sys.stderr)
            return 2

    failures = 0
    with tempfile.TemporaryDirectory() as out:
        result = subprocess.run(
            [sys.executable, str(INGESTER), *[str(p) for p in inputs],
             "--output", out],
            capture_output=True, text=True)
        if result.returncode != 0:
            print("ingester failed: {}".format(result.stderr), file=sys.stderr)
            return 1

        psa = Path(out) / "psa_manifest"
        pid = defines(psa / "pid.h")
        sid = defines(psa / "sid.h")
        driver = defines(psa / "driver_partition.h")

        for name in EXPECT_PID:
            if name not in pid:
                print("pid.h missing {}".format(name), file=sys.stderr)
                failures += 1
        for name, value in EXPECT_SID.items():
            if sid.get(name) != value:
                print("sid.h {}: expected {:#x}, got {}".format(
                    name, value, sid.get(name)), file=sys.stderr)
                failures += 1
        for name, value in EXPECT_VERSION.items():
            if sid.get(name) != value:
                print("sid.h {}: expected {}, got {}".format(
                    name, value, sid.get(name)), file=sys.stderr)
                failures += 1
        for name, value in EXPECT_DRIVER_SIGNAL.items():
            if driver.get(name) != value:
                print("driver_partition.h {}: expected {:#x}, got {}".format(
                    name, value, driver.get(name)), file=sys.stderr)
                failures += 1

    if failures != 0:
        print("manifest ingest checks failed: {}".format(failures),
              file=sys.stderr)
        return 1
    print("PASS: manifest ingest (Arm PSA-FF -> psa_manifest headers)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
