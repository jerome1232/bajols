# ESP32 BCP radio bridge

Build this sketch for two classic ESP32 Dev Modules with SparkFun 915 MHz
RFM69HCW breakouts. The default build is the USB shore adapter. The buoy build
forwards to a 3.3 V UART connected to the full-duplex tether transceiver.

| Radio signal | ESP32 GPIO |
|---|---|
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| NSS | 27 |
| DIO0 | 26 |
| RESET | 25 |
| VCC, GND | Regulated 3.3 V, common ground |

Fit a matched 915 MHz antenna at each breakout. The buoy uses GPIO17 (TX) to
the line-driver input and GPIO16 (RX) from the line-receiver output. These pins
assume a classic ESP32-WROOM board. On the shore adapter use the DevKit's USB
data port. Both serial links use 115200 baud, 8N1, without flow control.

## Build

Install Arduino-ESP32 3.3.9 using the
[firmware toolchain instructions](../../BUILD_AND_PROVISION.md). From the repo
root, run the following commands.

```sh
arduino-cli lib install RFM69_LowPowerLab@1.6.0

arduino-cli compile -b esp32:esp32:esp32 --libraries firmware/Libraries \
  --output-dir /tmp/bajols-rf-shore firmware/Experimental/BcpRadioBridge

arduino-cli compile -b esp32:esp32:esp32 --libraries firmware/Libraries \
  --build-property 'build.extra_flags=-DBAJOLS_RF_BUOY=1' \
  --output-dir /tmp/bajols-rf-buoy firmware/Experimental/BcpRadioBridge
```

Flash the corresponding build to each ESP32. Select the shore adapter's serial
port in the ground station and use the expected ID of the actual BCP vehicle or
test peer. For a USB-to-USB bench test, wire the buoy's UART to a 3.3 V USB-UART
adapter with TX/RX crossed and common ground, then attach a BCP test peer to
that port. A tethered vehicle needs a BCP serial endpoint on the UART used by
its line transceiver. The bridge firmware runs on the shore and buoy boards.

## RF transport

Use a matched pair with the built-in 915 MHz FSK profile, 250 kbit/s nominal
rate, 0 dBm initial transmit power, network ID 73, shore node 1, buoy node 2.
The shore sends a poll even when idle and allows 10 ms for a reply after
transmission. The buoy starts its reply 2–4 ms after servicing the poll.

The application header has one version/flags byte (0x10, plus start bit 0
and end bit 1) and a little-endian 32-bit slot counter. The shore increments
the counter per poll from a random startup value. The buoy echoes it. A
payload carries up to 56 bytes of one BCP frame, allowing a 116-byte frame
to span three consecutive slots. Empty polls and replies carry only a header.
Lost slots break reassembly. There are no RF retransmissions. A poll reply
indicates RF activity. Command acceptance comes from the vehicle's BCP results.

Both bridges forward only complete, CRC-valid BCP frames, byte for byte.
Each direction has bounded queues and a 50 ms local frame lifetime through
admission and scheduling. Disarm and emergency-stop frames clear queued
replaceable traffic and join the remaining FIFO. Keep bench tests
actuator-disabled while measuring throughput, latency, lost fragments, radio
resets, unplugged links and vehicle timeout behavior. The RF link carries
plaintext and provides no peer authentication. The network ID and CRC are
for packet filtering and error detection. Select the operating profile for the
region and deployment before field use.

See the [buoy and tether wiring guide](../../../Documentation/Wiring/RF_BUOY_TETHER.md).
