#!/usr/bin/env python3
"""Offline ESP32 bundle integrity tests; the fixture generator is not an NVS encoder."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("esp32_provision", TOOLS / "provision_esp32.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)
IDENTITY = "0102030405060708090A0B0C0D0E0F10"


class Esp32ProvisioningTests(unittest.TestCase):
    """Verify refusal/readback behavior without importing Espressif or accessing a device."""

    def test_bundle_and_whole_flash_readback(self):
        """Changed layout, identity, image, source or outside-partition bytes are rejected."""
        original = bytearray([0x77]) * TOOL.FLASH_BYTES
        start = TOOL.PARTITION_OFFSET
        end = start + TOOL.PARTITION_BYTES
        original[start:end] = bytes([255]) * TOOL.PARTITION_BYTES
        original = bytes(original)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            generator = directory / "fixture_generator.py"
            generator.write_text(
                "import hashlib, pathlib, sys\n"
                "assert sys.argv[1] == 'generate'\n"
                "assert sys.argv[5:] == ['--version', '2']\n"
                "csv = pathlib.Path(sys.argv[2]).read_bytes()\n"
                "header = b'FIXTURE-NOT-NVS' + hashlib.sha256(csv).digest()\n"
                "pathlib.Path(sys.argv[3]).write_bytes(header + bytes([255]) * (int(sys.argv[4], 0)-len(header)))\n"
            )
            output = directory / "bundle"
            manifest = TOOL.prepare(original, IDENTITY, output, generator)
            records = (output / "journal-records.bin").read_bytes()
            self.assertEqual(len(records), 192)
            self.assertEqual(records[8:24], bytes(range(1, 17)))
            self.assertEqual(records[131], 1)
            readback = original[:start] + (output / "bajols-nvs.bin").read_bytes() + original[end:]
            TOOL.verify_readback(readback, original, manifest, generator)
            for cell in (0, start - 1, start, end - 1, end, TOOL.FLASH_BYTES - 1):
                changed = bytearray(readback)
                changed[cell] ^= 1
                with self.assertRaises(ValueError):
                    TOOL.verify_readback(bytes(changed), original, manifest, generator)
            for key, value in (("partition_offset", 0x9000), ("logical_base", 32),
                               ("initial_emergency_state", "cleared"), ("image_sha256", "0" * 64),
                               ("records_sha256", "0" * 64), ("vehicle_id_wire_hex", "02" * 16)):
                changed = dict(manifest)
                changed[key] = value
                with self.assertRaises(ValueError):
                    TOOL.verify_readback(readback, original, changed, generator)
            with self.assertRaises(FileExistsError):
                TOOL.prepare(original, IDENTITY, output, generator)
            generator.write_text(generator.read_text() + "# changed\n")
            with self.assertRaises(ValueError):
                TOOL.verify_readback(readback, original, manifest, generator)

    def test_only_erased_new_partition_is_accepted(self):
        """No repair/force path accepts a short dump or any occupied partition byte."""
        original = bytearray([255]) * TOOL.FLASH_BYTES
        TOOL.validate_original(bytes(original))
        for cell in (TOOL.PARTITION_OFFSET, TOOL.PARTITION_OFFSET + TOOL.PARTITION_BYTES - 1):
            changed = bytearray(original)
            changed[cell] = 0
            with self.assertRaises(ValueError):
                TOOL.validate_original(bytes(changed))
        with self.assertRaises(ValueError):
            TOOL.validate_original(bytes(original[:-1]))
        with self.assertRaises(ValueError):
            TOOL.csv_records(TOOL.journal.SIMULATOR_ID)


if __name__ == "__main__":
    unittest.main()
