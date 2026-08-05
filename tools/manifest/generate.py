#!/usr/bin/env python3
# generate.py
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfTrust.
#
# wolfTrust is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfTrust is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

"""Generate C from the normalized wolfTrust manifest representation."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


UINT = "uint"
WORD = "word"
BOOL = "bool"
STRING = "string"

MEMORY_SCHEMA = {
    "base": WORD,
    "size": WORD,
    "attributes": UINT,
    "share_id": UINT,
}

INTERRUPT_RESOURCE_SCHEMA = {
    "interrupt": UINT,
    "attributes": UINT,
    "share_id": UINT,
}

RESTART_SCHEMA = {
    "action": UINT,
    "restart_limit": UINT,
    "restart_window_ticks": UINT,
    "initial_delay_ticks": UINT,
}

DOMAIN_SCHEMA = {
    "id": UINT,
    "domain_class": UINT,
    "rot_role": UINT,
    "security_state": UINT,
    "privilege_state": UINT,
    "initial_lifecycle": UINT,
    "entry_point": WORD,
    "stack_base": WORD,
    "stack_size": WORD,
    "memory_resources": [MEMORY_SCHEMA],
    "interrupt_resources": [INTERRUPT_RESOURCE_SCHEMA],
    "restart_policy": RESTART_SCHEMA,
    "required_capabilities": UINT,
}

SERVICE_SCHEMA = {
    "name": STRING,
    "sid": UINT,
    "version": UINT,
    "version_policy": UINT,
    "signal": UINT,
    "stateless_handle_index": UINT,
    "nonsecure_clients": BOOL,
    "connection_based": BOOL,
}

MANIFEST_INTERRUPT_SCHEMA = {
    "signal_name": STRING,
    "interrupt": UINT,
    "signal": UINT,
}

PARTITION_SCHEMA = {
    "name": STRING,
    "domain_id": UINT,
    "framework_version": UINT,
    "model": UINT,
    "priority": UINT,
    "services": [SERVICE_SCHEMA],
    "dependencies": [UINT],
    "interrupts": [MANIFEST_INTERRUPT_SCHEMA],
}

CAPABILITIES_SCHEMA = {
    "capabilities": UINT,
    "max_domains": UINT,
    "max_memory_resources_per_domain": UINT,
    "max_interrupts_per_domain": UINT,
}

LIMITS_SCHEMA = {
    "max_partitions": UINT,
    "max_services_per_partition": UINT,
    "max_dependencies_per_partition": UINT,
    "max_stateless_handles": UINT,
}

MANIFEST_SCHEMA = {
    "format_version": UINT,
    "generator_version": STRING,
    "features": UINT,
    "isolation_profile": UINT,
    "profile_capabilities": CAPABILITIES_SCHEMA,
    "domains": [DOMAIN_SCHEMA],
    "partitions": [PARTITION_SCHEMA],
    "limits": LIMITS_SCHEMA,
}

FILE_HEADER = """/* {name}
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 *
 * wolfTrust is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfTrust is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
"""


class ManifestError(ValueError):
    """Raised for malformed normalized manifest input."""


def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ManifestError("duplicate JSON key: " + key)
        result[key] = value
    return result


def validate(value, schema, path, word_max):
    if schema in (UINT, WORD):
        if isinstance(value, bool) or not isinstance(value, int):
            raise ManifestError(path + " must be an unsigned integer")
        maximum = 0xffffffff if schema == UINT else word_max
        if value < 0 or value > maximum:
            raise ManifestError(path + " is outside the supported range")
        return

    if schema == BOOL:
        if not isinstance(value, bool):
            raise ManifestError(path + " must be a boolean")
        return

    if schema == STRING:
        if not isinstance(value, str) or not value:
            raise ManifestError(path + " must be a nonempty string")
        if any(ord(char) < 0x20 or ord(char) > 0x7e for char in value):
            raise ManifestError(path + " must contain printable ASCII")
        return

    if isinstance(schema, list):
        if not isinstance(value, list):
            raise ManifestError(path + " must be an array")
        for index, item in enumerate(value):
            validate(item, schema[0], "{}[{}]".format(path, index), word_max)
        return

    if not isinstance(value, dict):
        raise ManifestError(path + " must be an object")

    unknown = set(value) - set(schema)
    missing = set(schema) - set(value)
    if unknown:
        raise ManifestError(path + " has unknown field " + sorted(unknown)[0])
    if missing:
        raise ManifestError(path + "." + sorted(missing)[0] + " is required")
    for name, child_schema in schema.items():
        validate(value[name], child_schema, path + "." + name, word_max)


def policy_error(message):
    raise ManifestError("policy: " + message)


def name_valid(value):
    return re.fullmatch(r"[A-Z_][A-Z0-9_]*", value) is not None


def ranges_overlap(first_base, first_size, second_base, second_size):
    return (first_base < second_base + second_size and
            second_base < first_base + first_size)


def validate_policy(manifest, supported_features, word_max):
    features = manifest["features"]
    if features & ~0xF or features & 1 == 0:
        policy_error("invalid required feature set")
    if supported_features & ~0xF or features & ~supported_features:
        policy_error("manifest requests unsupported features")
    limits = manifest["limits"]
    capabilities = manifest["profile_capabilities"]
    domains = manifest["domains"]
    partitions = manifest["partitions"]
    if not partitions or len(partitions) > limits["max_partitions"]:
        policy_error("partition count exceeds limits")
    if not domains or len(domains) > capabilities["max_domains"]:
        policy_error("domain count exceeds profile capability")
    domain_ids = set()
    spm_count = 0
    secure_partition_ids = set()
    memory_resources = []
    interrupt_resources = set()
    for domain in domains:
        domain_id = domain["id"]
        if domain_id in domain_ids:
            policy_error("duplicate domain id")
        domain_ids.add(domain_id)
        if domain["domain_class"] == 0:
            spm_count += 1
        if domain["domain_class"] == 1:
            secure_partition_ids.add(domain_id)
        if not domain["memory_resources"]:
            policy_error("domain has no memory resource")
        if len(domain["memory_resources"]) > capabilities["max_memory_resources_per_domain"]:
            policy_error("domain memory count exceeds profile capability")
        if len(domain["interrupt_resources"]) > capabilities["max_interrupts_per_domain"]:
            policy_error("domain interrupt count exceeds profile capability")
        for memory in domain["memory_resources"]:
            base = memory["base"]
            size = memory["size"]
            if size == 0 or base + size > word_max + 1:
                policy_error("memory resource is outside target address space")
            if memory["attributes"] & 2 and memory["attributes"] & 4:
                policy_error("memory resource is writable and executable")
            for owner, other_base, other_size, other_attributes, other_share in memory_resources:
                if ranges_overlap(base, size, other_base, other_size):
                    shared = (memory["attributes"] & 32 and
                              other_attributes & 32 and
                              memory["share_id"] != 0 and
                              memory["share_id"] == other_share)
                    if owner == domain_id or not shared:
                        policy_error("memory resources overlap incompatibly")
            memory_resources.append((domain_id, base, size,
                                     memory["attributes"], memory["share_id"]))
        for interrupt in domain["interrupt_resources"]:
            number = interrupt["interrupt"]
            for other_number, other_attributes, other_share in interrupt_resources:
                if number == other_number:
                    shared = (interrupt["attributes"] & 1 and
                              other_attributes & 1 and
                              interrupt["share_id"] != 0 and
                              interrupt["share_id"] == other_share)
                    if not shared:
                        policy_error("interrupt resource is multiply owned")
            interrupt_resources.add((number, interrupt["attributes"],
                                     interrupt["share_id"]))
    if spm_count != 1:
        policy_error("exactly one SPM is required")
    service_names = set()
    service_ids = set()
    for partition in partitions:
        if not name_valid(partition["name"]):
            policy_error("partition name is not a generated identifier")
        if partition["domain_id"] not in secure_partition_ids:
            policy_error("partition is not bound to a secure partition domain")
        if partition["framework_version"] not in (0x100, 0x101):
            policy_error("unsupported framework version")
        if partition["model"] == 0 and not features & 1:
            policy_error("IPC partition is missing IPC feature")
        if partition["model"] == 1 and not features & 2:
            policy_error("SFN partition is missing SFN feature")
        if not partition["services"] and not partition["interrupts"]:
            policy_error("partition has no service or interrupt")
        if len(partition["services"]) > limits["max_services_per_partition"]:
            policy_error("service count exceeds limits")
        for service in partition["services"]:
            if not name_valid(service["name"]):
                policy_error("service name is not a generated identifier")
            if service["name"] in service_names or service["sid"] in service_ids:
                policy_error("service identity is duplicated")
            service_names.add(service["name"])
            service_ids.add(service["sid"])
            signal = service["signal"]
            if partition["model"] == 0:
                if signal == 0 or signal & 0xf or signal & (signal - 1):
                    policy_error("IPC service signal is invalid")
            elif signal != 0:
                policy_error("SFN service signal must be zero")
        for interrupt in partition["interrupts"]:
            if not name_valid(interrupt["signal_name"]):
                policy_error("interrupt signal name is not a generated identifier")
            signal = interrupt["signal"]
            if signal == 0 or signal & 0xf or signal & (signal - 1):
                policy_error("interrupt signal is invalid")
    for partition in partitions:
        for dependency in partition["dependencies"]:
            if dependency not in service_ids:
                policy_error("partition dependency is unresolved")
    if not service_ids:
        policy_error("no services are defined")


def c_uint(value):
    return "{}U".format(value)


def c_word(value):
    return "{}ULL".format(value)


def c_bool(value):
    return c_uint(int(value))


def c_string(value):
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return '"{}"'.format(escaped)


def c_struct(fields):
    lines = ["{"]
    for name, value in fields:
        value_lines = value.splitlines()
        lines.append("    .{} = {}".format(name, value_lines[0]))
        lines.extend("    " + line for line in value_lines[1:])
        lines[-1] += ","
    lines.append("}")
    return "\n".join(lines)


def emit_array(lines, declaration, values):
    if not values:
        return "NULL"
    lines.append("static const {} = {{".format(declaration))
    for value in values:
        value_lines = value.splitlines()
        lines.extend("    " + line for line in value_lines)
        lines[-1] += ","
    lines.extend(("};", ""))
    return declaration.split("[")[0].split()[-1]


def c_scalar(value, schema):
    if schema == BOOL:
        return c_bool(value)
    if schema == STRING:
        return c_string(value)
    if schema == WORD:
        return c_word(value)
    return c_uint(value)


def scalar_struct(value, schema):
    return c_struct(tuple(
        (name, c_scalar(item, schema[name]))
        for name, item in value.items()))


def emit_domain(lines, domain, index):
    memory = emit_array(lines,
        "wt_memory_resource_t wt_generated_memory_{}[{}]".format(
            index, len(domain["memory_resources"])),
        [scalar_struct(item, MEMORY_SCHEMA)
         for item in domain["memory_resources"]])
    interrupts = emit_array(lines,
        "wt_interrupt_resource_t wt_generated_irqs_{}[{}]".format(
            index, len(domain["interrupt_resources"])),
        [scalar_struct(item, INTERRUPT_RESOURCE_SCHEMA)
         for item in domain["interrupt_resources"]])
    fields = []
    for name, value in domain.items():
        if name == "memory_resources":
            fields.extend(((name, memory),
                           ("memory_resource_count",
                            c_uint(len(domain[name])))))
        elif name == "interrupt_resources":
            fields.extend(((name, interrupts),
                           ("interrupt_resource_count",
                            c_uint(len(domain[name])))))
        elif name == "restart_policy":
            fields.append((name, scalar_struct(value, RESTART_SCHEMA)))
        else:
            fields.append((name, c_scalar(value, DOMAIN_SCHEMA[name])))
    return c_struct(fields)


def emit_partition(lines, partition, index):
    service_values = [scalar_struct(service, SERVICE_SCHEMA)
                      for service in partition["services"]]
    services = emit_array(lines,
        "wt_service_descriptor_t wt_generated_services_{}[{}]".format(
            index, len(service_values)), service_values)
    dependencies = emit_array(lines,
        "uint32_t wt_generated_dependencies_{}[{}]".format(
            index, len(partition["dependencies"])),
        [c_uint(value) for value in partition["dependencies"]])
    interrupt_values = [scalar_struct(interrupt, MANIFEST_INTERRUPT_SCHEMA)
                        for interrupt in partition["interrupts"]]
    interrupts = emit_array(lines,
        "wt_manifest_interrupt_t wt_generated_partition_irqs_{}[{}]".format(
            index, len(interrupt_values)), interrupt_values)
    fields = []
    for name, value in partition.items():
        if name == "name":
            fields.append((name, c_string(value)))
        elif name == "services":
            fields.extend(((name, services),
                           ("service_count", c_uint(len(value)))))
        elif name == "dependencies":
            fields.extend(((name, dependencies),
                           ("dependency_count", c_uint(len(value)))))
        elif name == "interrupts":
            fields.extend(((name, interrupts),
                           ("interrupt_count", c_uint(len(value)))))
        else:
            fields.append((name, c_scalar(value, PARTITION_SCHEMA[name])))
    return c_struct(fields)


def generate_source(manifest, digest):
    lines = [FILE_HEADER.format(name="wolftrust_manifest_generated.c"),
             "/* Normalized manifest SHA-256: {} */".format(digest.hex()),
             "#include \"wolftrust_manifest_generated.h\"", ""]
    digest_values = ["0x{:02x}U".format(value) for value in digest]
    emit_array(lines, "uint8_t wt_generated_digest[32]", digest_values)
    domain_values = [emit_domain(lines, domain, index)
                     for index, domain in enumerate(manifest["domains"])]
    domains = emit_array(lines,
        "wt_domain_descriptor_t wt_generated_domains[{}]".format(
            len(domain_values)), domain_values)
    partition_values = [emit_partition(lines, partition, index)
                        for index, partition in enumerate(manifest["partitions"])]
    partitions = emit_array(lines,
        "wt_partition_manifest_t wt_generated_partitions[{}]".format(
            len(partition_values)), partition_values)
    capabilities = scalar_struct(manifest["profile_capabilities"],
                                 CAPABILITIES_SCHEMA)
    limits = scalar_struct(manifest["limits"], LIMITS_SCHEMA)
    system = c_struct((
        ("format_version", c_uint(manifest["format_version"])),
        ("generator_version", c_string(manifest["generator_version"])),
        ("input_digest", "wt_generated_digest"),
        ("input_digest_size", "sizeof(wt_generated_digest)"),
        ("features", c_uint(manifest["features"])),
        ("isolation_profile", c_uint(manifest["isolation_profile"])),
        ("profile_capabilities", "&wt_generated_capabilities"),
        ("domains", domains),
        ("domain_count", c_uint(len(domain_values))),
        ("partitions", partitions),
        ("partition_count", c_uint(len(partition_values))),
        ("limits", limits),
    ))
    lines.extend((
        "static const wt_profile_capabilities_t wt_generated_capabilities =",
        capabilities + ";", "",
        "static const wt_system_manifest_t wt_generated_manifest =",
        system + ";", "",
        "const wt_system_manifest_t* wt_generated_manifest_get(void)",
        "{", "    return &wt_generated_manifest;", "}", ""))
    return "\n".join(lines)


def generate_header(manifest):
    lines = [FILE_HEADER.format(name="wolftrust_manifest_generated.h"),
             "#ifndef WOLFTRUST_MANIFEST_GENERATED_H",
             "#define WOLFTRUST_MANIFEST_GENERATED_H", "",
             "#include \"wolftrust/manifest.h\"", ""]
    symbols = set()

    def add_symbol(name):
        if name in symbols:
            policy_error("generated symbol is duplicated: " + name)
        symbols.add(name)

    for partition in manifest["partitions"]:
        prefix = "WT_GENERATED_" + partition["name"]
        add_symbol(prefix + "_DOMAIN_ID")
        add_symbol(prefix + "_FRAMEWORK_VERSION")
        add_symbol(prefix + "_MODEL")
        lines.extend((
            "#define {}_DOMAIN_ID {}U".format(prefix, partition["domain_id"]),
            "#define {}_FRAMEWORK_VERSION {}U".format(
                prefix, partition["framework_version"]),
            "#define {}_MODEL {}U".format(prefix, partition["model"]),
        ))
        for service in partition["services"]:
            service_prefix = "WT_GENERATED_" + service["name"]
            add_symbol(service_prefix + "_SID")
            add_symbol(service_prefix + "_VERSION")
            lines.extend((
                "#define {}_SID {}U".format(service_prefix, service["sid"]),
                "#define {}_VERSION {}U".format(
                    service_prefix, service["version"]),
            ))
            if partition["model"] == 0:
                add_symbol(service_prefix + "_SIGNAL")
                lines.append("#define {}_SIGNAL {}U".format(
                    service_prefix, service["signal"]))
            if not service["connection_based"]:
                add_symbol(service_prefix + "_HANDLE")
                lines.append("#define {}_HANDLE {}U".format(
                    service_prefix, service["stateless_handle_index"]))
        for interrupt in partition["interrupts"]:
            interrupt_prefix = "WT_GENERATED_" + interrupt["signal_name"]
            add_symbol(interrupt_prefix + "_SIGNAL")
            lines.append("#define {}_SIGNAL {}U".format(
                interrupt_prefix, interrupt["signal"]))
        lines.append("")
    lines.extend((
        "const wt_system_manifest_t* wt_generated_manifest_get(void);", "",
        "#endif", ""))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--supported-features", required=True,
                        type=lambda value: int(value, 0))
    parser.add_argument("--address-bits", choices=("32", "64"), default="32")
    args = parser.parse_args()

    try:
        input_bytes = args.input.read_bytes()
        manifest = json.loads(input_bytes.decode("utf-8"),
                              object_pairs_hook=reject_duplicate_keys)
        word_max = (1 << int(args.address_bits)) - 1
        validate(manifest, MANIFEST_SCHEMA, "manifest", word_max)
        validate_policy(manifest, args.supported_features, word_max)
        source = generate_source(manifest, hashlib.sha256(input_bytes).digest())
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "wolftrust_manifest_generated.c").write_text(
            source, encoding="utf-8")
        (args.output / "wolftrust_manifest_generated.h").write_text(
            generate_header(manifest), encoding="utf-8")
    except (ManifestError, UnicodeDecodeError, json.JSONDecodeError,
            OSError) as error:
        print("manifest generation failed: {}".format(error), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
