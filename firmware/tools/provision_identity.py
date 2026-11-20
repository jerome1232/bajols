#!/usr/bin/env python3
"""Prepare initial BCP identity records offline; never read or write a device."""

import argparse
import binascii
import hashlib
import json
from pathlib import Path
import re
import secrets
import struct

DEVICE_BYTES = 4096
WINDOW_BYTES = 192
SIMULATOR_ID = "42414A4F4C532D53494D554C41544F52"


def vehicle_id(value):
    """Validate opaque wire-order identity; reject erased/reserved identities."""
    if not re.fullmatch(r"[0-9a-fA-F]{32}", value):
        raise ValueError("vehicle ID must be exactly 32 hex characters in wire-byte order")
    result = bytes.fromhex(value)
    if result in (bytes(16), bytes([255]) * 16) or value.upper() == SIMULATOR_ID:
        raise ValueError("zero, erased and simulator identities cannot provision a vehicle")
    return result


def record(kind, identity):
    """Build an initial record; boot/journal counters start at zero, stop is latched."""
    data = bytearray(32)
    data[0:4] = bytes([0xA5, 1, kind, 1 if kind == 3 else 0])
    if kind == 1:
        data[8:24] = identity
    struct.pack_into("<H", data, 30, binascii.crc_hqx(data[1:30], 0xFFFF))
    return bytes(data)


def initial_window(identity):
    """Create paired identity, zero boot counter and latched emergency records."""
    return b"".join(record(kind, identity) * 2 for kind in (1, 2, 3))


def intel_hex(data, base):
    """Emit sparse Intel HEX records covering only the explicitly selected window."""
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        row = bytes([len(chunk)]) + struct.pack(">H", base + offset) + b"\x00" + chunk
        lines.append(":" + (row + bytes([-sum(row) & 255])).hex().upper())
    return "\n".join(lines + [":00000001FF", ""])


def prepare(dump, base, identity, output):
    """Create a new review bundle only for an erased target window; never overwrite."""
    if len(dump) != DEVICE_BYTES:
        raise ValueError("input must be an exact 4096-byte raw Mega EEPROM dump")
    if not 0 <= base <= DEVICE_BYTES - WINDOW_BYTES:
        raise ValueError("base must leave a complete 192-byte window inside EEPROM")
    if dump[base:base + WINDOW_BYTES] != bytes([255]) * WINDOW_BYTES:
        raise ValueError("target window is not erased; initial provisioning cannot repair or reset it")
    identity = vehicle_id(identity)
    window = initial_window(identity)
    hex_text = intel_hex(window, base)
    manifest = {
        "purpose": "initial provisioning only; not repair, reset or hardware authorization",
        "storage_format": 1,
        "vehicle_id_wire_hex": identity.hex().upper(),
        "device_bytes": DEVICE_BYTES,
        "base": base,
        "window_bytes": WINDOW_BYTES,
        "initial_boot_counter": 0,
        "initial_emergency_state": "latched",
        "initial_emergency_counter": 0,
        "source_dump_sha256": hashlib.sha256(dump).hexdigest(),
        "window_sha256": hashlib.sha256(window).hexdigest(),
        "image_sha256": hashlib.sha256(hex_text.encode("ascii")).hexdigest(),
    }
    # Creation is exclusive: no --force option, existing files are never replaced.
    output.mkdir(parents=False, exist_ok=False)
    (output / "identity.eep.hex").write_text(hex_text, encoding="ascii")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "REVIEW.txt").write_text(
        "OFFLINE INITIAL PROVISIONING BUNDLE -- NO DEVICE WAS ACCESSED\n"
        "Review the source dump hash, unique vehicle ID and physical base.\n"
        "This is not a recovery tool; never reset an already-used boot identity.\n"
        "Program only the specified sparse window using a separately approved service action.\n"
        "Keep emergency latched; verify a fresh full readback before runtime use.\n"
        "For ground-station YAML, explicitly select serial, your device port, and:\n"
        f'expected_vehicle_id: "{identity.hex().upper()}"\n'
        "baud_rate: 115200\n"
    )
    return manifest


def verify_readback(readback, original, manifest):
    """Verify whole-device readback and unchanged bytes outside the provisioned window."""
    if len(readback) != DEVICE_BYTES or len(original) != DEVICE_BYTES:
        raise ValueError("both raw dumps must be exactly 4096 bytes")
    if hashlib.sha256(original).hexdigest() != manifest["source_dump_sha256"]:
        raise ValueError("original dump does not match the reviewed manifest")
    base = manifest["base"]
    if type(base) is not int or not 0 <= base <= DEVICE_BYTES - WINDOW_BYTES:
        raise ValueError("invalid manifest base")
    expected = initial_window(vehicle_id(manifest["vehicle_id_wire_hex"]))
    if (manifest["storage_format"] != 1 or manifest["window_bytes"] != WINDOW_BYTES
            or manifest["device_bytes"] != DEVICE_BYTES
            or manifest["initial_boot_counter"] != 0
            or manifest["initial_emergency_counter"] != 0
            or manifest["initial_emergency_state"] != "latched"):
        raise ValueError("unsupported or inconsistent initial-provision manifest")
    image = intel_hex(expected, base).encode("ascii")
    if hashlib.sha256(image).hexdigest() != manifest["image_sha256"]:
        raise ValueError("manifest image hash is inconsistent")
    if hashlib.sha256(expected).hexdigest() != manifest["window_sha256"]:
        raise ValueError("manifest record hash is inconsistent")
    if original[base:base + WINDOW_BYTES] != bytes([255]) * WINDOW_BYTES:
        raise ValueError("reviewed source window was not erased")
    wanted = original[:base] + expected + original[base + WINDOW_BYTES:]
    if readback != wanted:
        raise ValueError("readback differs: records changed, are incomplete, or bytes outside the window changed")


def main():
    """Parse an explicit prepare/verify operation with no serial or programmer dependencies."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("prepare", help="Create a new sparse image/review bundle")
    build.add_argument("--initial-provision", action="store_true", required=True)
    build.add_argument("--input", type=Path, required=True, help="Fresh raw 4096-byte EEPROM dump")
    build.add_argument("--base", type=lambda value: int(value, 0), required=True)
    choice = build.add_mutually_exclusive_group(required=True)
    choice.add_argument("--vehicle-id", help="Unique 32-character wire-order hex ID")
    choice.add_argument("--generate-id", action="store_true", help="Generate a new random 128-bit ID")
    build.add_argument("--output", type=Path, required=True, help="New directory; parent must exist")
    verify = commands.add_parser("verify", help="Verify offline readback against the reviewed bundle")
    verify.add_argument("--original", type=Path, required=True)
    verify.add_argument("--readback", type=Path, required=True)
    verify.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            identity = secrets.token_hex(16) if args.generate_id else args.vehicle_id
            manifest = prepare(args.input.read_bytes(), args.base, identity, args.output)
            print(f"Prepared offline bundle: {args.output}")
            print(f"Vehicle ID: {manifest['vehicle_id_wire_hex']}")
            print("No hardware accessed. Review before a separate provisioning action.")
        else:
            manifest = json.loads(args.manifest.read_text())
            verify_readback(args.readback.read_bytes(), args.original.read_bytes(), manifest)
            print("PASS: initial readback matches the manifest; bytes outside the window unchanged.")
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    main()
