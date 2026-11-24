# bajols

Experimental firmware and a desktop ground station for a remotely controlled submarine.

## 1. Bench test actuators over USB serial

Start with a **Mega 2560** connected directly to the ground station by USB. Use a dry
bench with the propeller removed. The opt-in `TelemetryProof` bench build drives
the rudder, dive plane and unloaded motor. It limits motor PWM to 20% and holds
the ballast outputs off.

1. Install the Arduino AVR toolchain. For a new board, provision its BCP identity
   and verify the EEPROM readback. For an existing board, reuse its provisioned ID
   and EEPROM base. [Toolchain setup](firmware/BUILD_AND_PROVISION.md#toolchains) · [Mega provisioning](firmware/BUILD_AND_PROVISION.md#mega-initial-provisioning)
2. Wire the actuators and the D28 PC ownership, D29 stop and D30 reset switches.
   [Mega wiring and switch pinout](Documentation/Wiring/MEGA_ACTUATORS.md#mega-bcp-bench-control-profile)
3. Compile and upload the **Mega BCP bench build**. It enables
   `BAJOLS_BCP_ENABLE_BENCH_CONTROL` and writable identity storage, with BCP on the
   Mega’s USB serial port. [Build, upload and actuator checks](firmware/BUILD_AND_PROVISION.md#mega-compile-and-upload-with-actuator-writes)
4. Install or run the ground station, load a serial YAML with the Mega’s USB port
   and provisioned vehicle ID, then connect at **115200 baud, 8N1**.
   [Ground station](groundstation/README.md) · [Serial YAML example](firmware/BUILD_AND_PROVISION.md#connect-the-ground-station)

## 2. Bench test with RF modules

Build a shore adapter and a buoy bridge using two classic ESP32 Dev Modules and two
915 MHz RFM69HCW breakouts. Start with the transport checks using an actuator-disabled
BCP peer. [Bridge builds and bench setup](firmware/Experimental/BcpRadioBridge/README.md#build)

For a tethered Mega bench test, rebuild the vehicle firmware with
`BAJOLS_BCP_USE_SERIAL1` and connect the Mega’s Serial1 to the buoy through the
full-duplex RS-422 tether. In the ground-station YAML, change the port to the
**shore ESP32’s USB serial port** and keep the Mega’s provisioned vehicle ID.
[Mega tether build](firmware/BUILD_AND_PROVISION.md#mega-use-the-rf-buoy-and-rs-422-tether) · [RF and tether wiring](Documentation/Wiring/RF_BUOY_TETHER.md)

## Other builds and references

- [Vehicle firmware build modes](firmware/Experimental/TelemetryProof/README.md#build-modes)
  cover actuator-disabled Mega/ESP32 monitoring, synthetic telemetry and the legacy
  FlySky experiment.
- [Ground-station simulator](groundstation/README.md#operate-the-simulator) exercises
  BCP sessions and controls without vehicle hardware.
- [BCP v1 draft](protocol/BCP_V1_DRAFT.md), [firmware protocol library](firmware/Libraries/BajolsProtocol/README.md)
  and [ground-station protocol library](groundstation/Bajols.Protocol/README.md) describe
  the wire format and implementations.
- Run the native firmware protocol and storage tests with
  `python3 firmware/tests/bcp/run_native_tests.py`.

[![ESP32 Compile](https://github.com/jerome1232/bajols/actions/workflows/compile-esp32.yml/badge.svg)](https://github.com/jerome1232/bajols/actions/workflows/compile-esp32.yml) ![Arduino Lint](https://github.com/jerome1232/bajols/actions/workflows/check-arduino.yml/badge.svg) ![Arduino Compile](https://github.com/jerome1232/bajols/actions/workflows/compile-sketches.yml/badge.svg) [![Ground Station Build](https://github.com/jerome1232/bajols/actions/workflows/build-groundstation.yml/badge.svg)](https://github.com/jerome1232/bajols/actions/workflows/build-groundstation.yml) [![Firmware Protocol Tests](https://github.com/jerome1232/bajols/actions/workflows/test-firmware-protocol.yml/badge.svg)](https://github.com/jerome1232/bajols/actions/workflows/test-firmware-protocol.yml)
