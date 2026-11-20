#!/usr/bin/env python3
"""Prepare/verify an initial ESP32 NVS image offline; never access hardware."""

import argparse
import hashlib
import json
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile

import provision_identity as journal

FLASH_BYTES = 0x400000
PARTITION_OFFSET = 0x310000
PARTITION_BYTES = 0x6000


def csv_records(identity):
    """Encode the same six journal records as Mega, in the dedicated NVS namespace."""
    image = journal.initial_window(journal.vehicle_id(identity))
    rows = ["key,type,encoding,value", "journal,namespace,,"]
    for slot in range(6):
        rows.append(f"r{slot},data,hex2bin,{image[slot * 32:(slot + 1) * 32].hex()}")
    return "\n".join(rows) + "\n"


def generate(identity, generator, directory):
    """Run an explicitly selected official generator; no serial/programmer arguments exist."""
    csv_path = directory / "journal.csv"
    image_path = directory / "bajols-nvs.bin"
    csv_path.write_text(csv_records(identity), encoding="ascii")
    subprocess.run([sys.executable, str(generator.resolve()), "generate", str(csv_path.resolve()),
                    str(image_path.resolve()), hex(PARTITION_BYTES), "--version", "2"], check=True)
    image = image_path.read_bytes()
    if len(image) != PARTITION_BYTES:
        raise ValueError("NVS generator did not produce an exact 0x6000-byte partition")
    return image


def validate_original(original):
    """Require a complete 4 MB backup with an entirely erased new BCP partition."""
    if len(original) != FLASH_BYTES:
        raise ValueError("expected an exact 4 MB ESP32 flash dump for the supplied partition table")
    partition = original[PARTITION_OFFSET:PARTITION_OFFSET + PARTITION_BYTES]
    if partition != bytes([255]) * PARTITION_BYTES:
        raise ValueError("BCP partition is not erased; initial provisioning cannot repair/reset existing data")


def prepare(original, identity, output, generator):
    """Create a new review bundle only for a fresh dedicated partition; never overwrite."""
    validate_original(original)
    identity = journal.vehicle_id(identity).hex().upper()
    generator_hash = hashlib.sha256(generator.read_bytes()).hexdigest()
    output.mkdir(parents=False, exist_ok=False)
    image = generate(identity, generator, output)
    records = journal.initial_window(bytes.fromhex(identity))
    (output / "journal-records.bin").write_bytes(records)
    manifest = {
        "target": "esp32-nvs-v1",
        "purpose": "initial provisioning only; not recovery or hardware authorization",
        "storage_format": 1,
        "vehicle_id_wire_hex": identity,
        "flash_bytes": FLASH_BYTES,
        "partition_offset": PARTITION_OFFSET,
        "partition_bytes": PARTITION_BYTES,
        "partition_label": "bajols",
        "namespace": "journal",
        "logical_base": 0,
        "initial_boot_counter": 0,
        "initial_emergency_state": "latched",
        "initial_emergency_counter": 0,
        "source_dump_sha256": hashlib.sha256(original).hexdigest(),
        "generator_sha256": generator_hash,
        "records_sha256": hashlib.sha256(records).hexdigest(),
        "image_sha256": hashlib.sha256(image).hexdigest(),
        "csv_sha256": hashlib.sha256(csv_records(identity).encode("ascii")).hexdigest(),
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "REVIEW.txt").write_text(
        "OFFLINE ESP32 INITIAL PROVISIONING BUNDLE -- NO DEVICE ACCESSED\n"
        "Requires the supplied 4 MB partition table and a fresh erased BCP partition.\n"
        "Review generator/source/image hashes, physical board, flash size and partition map.\n"
        "Only bajols-nvs.bin at 0x310000 is a flash image; journal-records.bin is not.\n"
        "Keep runtime writes disabled until complete readback verification.\n"
        "Never erase or reprovision a used boot identity; emergency starts latched.\n"
        f'expected_vehicle_id: "{identity}"\n'
        "baud_rate: 115200\n"
    )
    return manifest


def verify_readback(readback, original, manifest, generator):
    """Regenerate the expected image and verify the whole flash, including unchanged outside bytes."""
    validate_original(original)
    if len(readback) != FLASH_BYTES:
        raise ValueError("readback must be an exact 4 MB flash dump")
    fixed = {"target": "esp32-nvs-v1", "storage_format": 1, "flash_bytes": FLASH_BYTES,
             "partition_offset": PARTITION_OFFSET, "partition_bytes": PARTITION_BYTES,
             "partition_label": "bajols", "namespace": "journal", "logical_base": 0,
             "initial_boot_counter": 0, "initial_emergency_state": "latched", "initial_emergency_counter": 0}
    if any(manifest[key] != value for key, value in fixed.items()):
        raise ValueError("manifest target/layout/initial state is unsupported or modified")
    identity = journal.vehicle_id(manifest["vehicle_id_wire_hex"])
    if hashlib.sha256(original).hexdigest() != manifest["source_dump_sha256"]:
        raise ValueError("original dump differs from the reviewed source")
    if hashlib.sha256(generator.read_bytes()).hexdigest() != manifest["generator_sha256"]:
        raise ValueError("use the exact NVS generator reviewed during preparation")
    if hashlib.sha256(journal.initial_window(identity)).hexdigest() != manifest["records_sha256"] or \
            hashlib.sha256(csv_records(identity.hex()).encode("ascii")).hexdigest() != manifest["csv_sha256"]:
        raise ValueError("manifest journal content is inconsistent")
    with tempfile.TemporaryDirectory(prefix="bajols-nvs-verify-") as temporary:
        image = generate(identity.hex(), generator, Path(temporary))
    if hashlib.sha256(image).hexdigest() != manifest["image_sha256"]:
        raise ValueError("regenerated NVS image differs from the manifest")
    end = PARTITION_OFFSET + PARTITION_BYTES
    if readback[PARTITION_OFFSET:end] != image:
        raise ValueError("NVS partition does not match the initial image")
    if readback[:PARTITION_OFFSET] != original[:PARTITION_OFFSET] or readback[end:] != original[end:]:
        raise ValueError("flash bytes outside the BCP partition changed")


def main():
    """Require an explicit offline prepare or verify action with an exact generator path."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("prepare")
    build.add_argument("--initial-provision", action="store_true", required=True)
    build.add_argument("--input", type=Path, required=True)
    choice = build.add_mutually_exclusive_group(required=True)
    choice.add_argument("--vehicle-id")
    choice.add_argument("--generate-id", action="store_true")
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--nvs-generator", type=Path, required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("--original", type=Path, required=True)
    verify.add_argument("--readback", type=Path, required=True)
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--nvs-generator", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            identity = secrets.token_hex(16) if args.generate_id else args.vehicle_id
            manifest = prepare(args.input.read_bytes(), identity, args.output, args.nvs_generator)
            print(f"Prepared offline bundle: {args.output}")
            print(f"Vehicle ID: {manifest['vehicle_id_wire_hex']}")
            print("No hardware accessed. Review before programming the reserved partition.")
        else:
            verify_readback(args.readback.read_bytes(), args.original.read_bytes(),
                            json.loads(args.manifest.read_text()), args.nvs_generator)
            print("PASS: initial NVS image and unchanged flash outside the partition verified.")
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    main()
