#!/usr/bin/env python3
"""Exercise the real pinned Espressif encoder on synthetic flash backups only."""

from pathlib import Path
import sys
import tempfile

import esp_idf_nvs_partition_gen.nvs_partition_gen as encoder

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))
import provision_esp32 as provision


def main():
    """Generate and independently regenerate a complete initial partition bundle."""
    original = bytearray([0x77]) * provision.FLASH_BYTES
    start = provision.PARTITION_OFFSET
    end = start + provision.PARTITION_BYTES
    original[start:end] = bytes([255]) * provision.PARTITION_BYTES
    original = bytes(original)
    generator = Path(encoder.__file__)
    with tempfile.TemporaryDirectory(prefix="bajols-real-nvs-") as temporary:
        output = Path(temporary) / "bundle"
        manifest = provision.prepare(original, "0102030405060708090A0B0C0D0E0F10", output, generator)
        image = (output / "bajols-nvs.bin").read_bytes()
        provision.verify_readback(original[:start] + image + original[end:], original, manifest, generator)
    for sketch in ("TelemetryProof", "BcpMonitor"):
        rows = (TOOLS.parent / "Experimental" / sketch / "partitions.csv").read_text()
        assert "bajols,data,nvs,0x310000,0x6000," in rows
    print("PASS: official NVS generator and supplied partition tables agree with provisioning")


if __name__ == "__main__":
    main()
