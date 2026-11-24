#!/usr/bin/env python3
"""Exercise offline provisioning refusal, sparse output and full readback checks."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

TOOL_PATH = Path(__file__).resolve().parents[2] / "tools/provision_identity.py"
SPEC = importlib.util.spec_from_file_location("provision_identity", TOOL_PATH)
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)
IDENTITY = "0102030405060708090A0B0C0D0E0F10"


class ProvisioningTests(unittest.TestCase):
    """Temporary local files only; there are no hardware/programmer calls."""

    def test_sparse_image_and_readback(self):
        """Only the selected erased window is represented and all HEX checksums agree."""
        original = bytes([0x55]) * 32 + bytes([255]) * 192 + bytes([0x77]) * (4096 - 224)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "bundle"
            manifest = TOOL.prepare(original, 32, IDENTITY, output)
            reconstructed = bytearray(original)
            addresses = set()
            for line in (output / "identity.eep.hex").read_text().splitlines():
                record = bytes.fromhex(line[1:])
                self.assertEqual(sum(record) & 255, 0)
                if record[3] == 1:
                    continue
                self.assertEqual(record[3], 0)
                address = int.from_bytes(record[1:3], "big")
                for offset, value in enumerate(record[4:-1]):
                    self.assertNotIn(address + offset, addresses)
                    addresses.add(address + offset)
                    reconstructed[address + offset] = value
            self.assertEqual(addresses, set(range(32, 224)))
            TOOL.verify_readback(bytes(reconstructed), original, manifest)
            for key, value in (("initial_boot_counter", 1), ("initial_emergency_state", "cleared"),
                               ("image_sha256", "0" * 64), ("base", 4096)):
                changed = dict(manifest)
                changed[key] = value
                with self.assertRaises(ValueError):
                    TOOL.verify_readback(bytes(reconstructed), original, changed)
            self.assertEqual(manifest["initial_emergency_state"], "latched")
            self.assertEqual(manifest["initial_boot_counter"], 0)
            self.assertEqual(json.loads((output / "manifest.json").read_text()), manifest)
            for cell in (0, 32, 100, 223, 4095):
                corrupted = bytearray(reconstructed)
                corrupted[cell] ^= 1
                with self.assertRaises(ValueError):
                    TOOL.verify_readback(corrupted, original, manifest)
            with self.assertRaises(FileExistsError):
                TOOL.prepare(original, 32, IDENTITY, output)

    def test_initial_only(self):
        """No force/repair mode accepts an existing record, partial write or short dump."""
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "bundle"
            for original, base in ((bytes(4096), 0), (bytes([255]) * 4095, 0),
                                   (bytes([255]) * 4096, 3905), (bytes([255]) * 4096, -1)):
                with self.assertRaises(ValueError):
                    TOOL.prepare(original, base, IDENTITY, output)
                self.assertFalse(output.exists())
            occupied = bytearray([255] * 4096)
            occupied[191] = 0
            with self.assertRaises(ValueError):
                TOOL.prepare(occupied, 0, IDENTITY, output)
            self.assertFalse(output.exists())

    def test_reserved_identity(self):
        """Wire-order IDs must be explicit, non-erased and distinct from the simulator."""
        for identity in ("00" * 16, "FF" * 16, TOOL.SIMULATOR_ID, "x" * 32, "01" * 15):
            with self.assertRaises(ValueError):
                TOOL.vehicle_id(identity)
        self.assertEqual(TOOL.vehicle_id(IDENTITY), bytes(range(1, 17)))


if __name__ == "__main__":
    unittest.main()
