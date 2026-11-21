#!/usr/bin/env python3
"""Read a Mega 2560 EEPROM through the ArduinoISP sketch on a classic Uno."""

import argparse
from pathlib import Path
import time


EEPROM_BYTES = 4096
MEGA_SIGNATURE = bytes.fromhex("1e9801")
END_OF_PACKET = b"\x20"
IN_SYNC = b"\x14"
OK = b"\x10"
READ_BATCH = 8


def exchange(port, command, response_size=0):
    port.write(command + END_OF_PACKET)
    reply = port.read(response_size + 2)
    if len(reply) != response_size + 2 or reply[:1] != IN_SYNC or reply[-1:] != OK:
        raise RuntimeError(f"ArduinoISP command 0x{command[0]:02x} returned {reply.hex() or 'no data'}")
    return reply[1:-1]


def read_eeprom(port):
    # Opening the serial port may reset the Uno.
    time.sleep(2)
    port.reset_input_buffer()
    exchange(port, b"\x30")  # STK_GET_SYNC
    exchange(port, b"\x50")  # STK_ENTER_PROGMODE
    try:
        signature = exchange(port, b"\x75", 3)  # STK_READ_SIGN
        if signature != MEGA_SIGNATURE:
            raise RuntimeError(f"expected ATmega2560 signature 0x1e9801, got 0x{signature.hex()}")

        data = bytearray()
        for start in range(0, EEPROM_BYTES, READ_BATCH):
            # STK_UNIVERSAL forwards SPI opcode 0xA0 with an explicit byte
            # address, bypassing ArduinoISP's word-addressed page-read path.
            commands = b"".join(
                bytes((0x56, 0xA0, address >> 8, address & 0xFF, 0)) + END_OF_PACKET
                for address in range(start, start + READ_BATCH)
            )
            port.write(commands)
            reply = port.read(3 * READ_BATCH)
            if (len(reply) != 3 * READ_BATCH
                    or reply[::3] != IN_SYNC * READ_BATCH
                    or reply[2::3] != OK * READ_BATCH):
                raise RuntimeError(f"invalid EEPROM read response at address 0x{start:04x}")
            data.extend(reply[1::3])
    finally:
        exchange(port, b"\x51")  # STK_LEAVE_PROGMODE
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial device for the Uno running ArduinoISP")
    parser.add_argument("--output", required=True, type=Path, help="New path for the 4096-byte raw dump")
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        parser.exit(2, "error: install PySerial for this Python interpreter\n")

    try:
        if args.output.exists():
            raise FileExistsError(f"output already exists: {args.output}")
        with serial.Serial(args.port, 19200, timeout=3, write_timeout=3) as port:
            data = read_eeprom(port)
        with args.output.open("xb") as output:
            output.write(data)
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")
    print(f"ATmega2560 signature 0x{MEGA_SIGNATURE.hex()}. Saved {len(data)} bytes to {args.output}")


if __name__ == "__main__":
    main()
