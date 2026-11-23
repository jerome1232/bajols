[![ESP32 Compile](https://github.com/jerome1232/bajols/actions/workflows/compile-esp32.yml/badge.svg)](https://github.com/jerome1232/bajols/actions/workflows/compile-esp32.yml) ![Arduino Lint](https://github.com/jerome1232/bajols/actions/workflows/check-arduino.yml/badge.svg) ![Arduino Compile](https://github.com/jerome1232/bajols/actions/workflows/compile-sketches.yml/badge.svg) [![Ground Station Build](https://github.com/jerome1232/bajols/actions/workflows/build-groundstation.yml/badge.svg)](https://github.com/jerome1232/bajols/actions/workflows/build-groundstation.yml) [![Firmware Protocol Tests](https://github.com/jerome1232/bajols/actions/workflows/test-firmware-protocol.yml/badge.svg)](https://github.com/jerome1232/bajols/actions/workflows/test-firmware-protocol.yml)

# bajols
Remotely controlled submarine with planned semi-autonomous operation.

The repository includes experimental Mega/ESP32 Arduino firmware, a portable BCP protocol
library, and a C# desktop ground station with an encoded simulated peer. The
[`TelemetryProof` sketch](firmware/Experimental/TelemetryProof/README.md) now defaults
to actuator-disabled BCP integration. Its original FlySky control/telemetry
experiment is retained behind `BAJOLS_ENABLE_LEGACY_CONTROL`. The opt-in
[Mega BCP bench build](Documentation/Wiring/MEGA_ACTUATORS.md#mega-bcp-bench-control-profile)
drives the control surfaces and unloaded main motor. The
[firmware build and provisioning guide](firmware/BUILD_AND_PROVISION.md)
covers Mega EEPROM and ESP32 NVS, including full dump/readback verification.

The legacy FlySky experiment reacts to throttle/direction and ballast switches,
streams simulated instrument values to the FS-i6X controller, and changes a fake
heading. It directly drives hardware and is not enabled in the default BCP build.

## Repository layout

```text
firmware/
  Experimental/
    TelemetryProof/    # Default BCP monitoring/telemetry; explicit legacy control option
    BcpCodecCheck/      # Isolated BCP codec compile check; no actuator setup
    BcpSessionCheck/    # Session policy compile check; no hardware initialization
    BcpSerialCheck/     # Bounded USB-serial endpoint; identity remains unverified
    BcpStorageCheck/    # Experimental identity journal with a RAM-only backend
    BcpEepromCheck/     # Mega EEPROM/coordinator compile check; writes disabled
    BcpMonitor/         # Verified startup + BCP monitoring; explicit write opt-in
    Wiring/            # Experimental wiring diagrams
  Libraries/           # Portable BajolsProtocol library and bundled archive
  tests/bcp/           # Shared vectors, state, transport and storage/startup tests
  tools/               # Offline initial identity image/readback tooling
Documentation/         # Sensor notes and receiver wiring documentation
protocol/              # BCP specification, conformance plan and shared wire vectors
groundstation/         # .NET/Avalonia ground station, simulator and tests
```

The original firmware was moved from `src/`; its direct-control path is now an
explicit legacy build option. The default vehicle sketch integrates BCP startup,
serial and telemetry with actuator outputs disabled. The C# codec, simulated peer
and desktop ground station are implemented under `groundstation/`.
The [BCP v1 discussion draft](protocol/BCP_V1_DRAFT.md) records agreed control and
safety behavior separately from proposed wire details and open decisions.
The [ground station](groundstation/README.md) separates protocol, communication,
domain, input, configuration and UI code. It starts disconnected and includes an
encoded BCP simulator, explicit session/control actions, validated YAML, live status
and bounded serial communication. The bundled configuration selects simulation.
Telemetry and plain-language vehicle status overlay the 3D view, including neutral
qualification progress and a colour-coded 0–4.5 m depth gauge. The control panel
provides a combined movement pad, pitch slider and three-position buoyancy selector.
Simulated object reports cover a provisional 0–6 m range; stale markers fade to
grey/white. See the ground-station guide for input qualification, object placement
and the limits of this schematic display.

## Compile

The default actuator-disabled build needs the matching Arduino board core and
local `BajolsProtocol` library. See [Build and provision](firmware/BUILD_AND_PROVISION.md)
for pinned Mega/ESP32 toolchains, compile/upload commands and persistent identity.
The optional **Mega legacy control** build uses the bundled
[IBusBM 1.1.4](firmware/Libraries/IBusBM-1.1.4.zip) and
[Encoder 1.4.4](firmware/Libraries/Encoder-1.4.4.zip) archives. In the Arduino IDE,
add each archive through **Sketch → Include Library → Add .ZIP Library**.
For Arduino CLI, extract them into a local library directory and pass that
directory when compiling from the repository root.

```sh
mkdir -p /tmp/bajols-legacy-libraries
unzip -o firmware/Libraries/IBusBM-1.1.4.zip -d /tmp/bajols-legacy-libraries
unzip -o firmware/Libraries/Encoder-1.4.4.zip -d /tmp/bajols-legacy-libraries
arduino-cli compile --fqbn arduino:avr:mega \
  --libraries firmware/Libraries --libraries /tmp/bajols-legacy-libraries \
  --build-property 'compiler.cpp.extra_flags=-DBAJOLS_ENABLE_LEGACY_CONTROL' \
  firmware/Experimental/TelemetryProof
```

Open [TelemetryProof.ino](firmware/Experimental/TelemetryProof/TelemetryProof.ino)
in the Arduino IDE and select Arduino Mega 2560, or use the documented ESP32 Dev
Module configuration. Mega compile CI and Arduino Lint both
target `firmware/Experimental/TelemetryProof`; compile CI also builds the isolated
`BcpCodecCheck`, `BcpSessionCheck`, `BcpSerialCheck`, `BcpStorageCheck`, `BcpEepromCheck` and `BcpMonitor` sketches with the local
`BajolsProtocol` library. The separate ESP32 workflow builds the default and
demo-writable vehicle paths plus the portable protocol/monitoring fixtures.

The [portable firmware protocol library](firmware/Libraries/BajolsProtocol/README.md)
contains the BCP codec, a session/authorization manager and a bounded serial endpoint.
It is integrated into the default vehicle sketch for actuator-disabled command
handling and telemetry. The bench build adds physical PC ownership and limited
actuator output. An experimental identity/emergency journal
models the [storage policy](protocol/FIRMWARE_STORAGE_PROPOSAL.md) in RAM.
The Mega EEPROM adapter and persistence coordinator are implemented; supplied
sketches default to EEPROM writes disabled and never provision identity. The
[offline tooling](firmware/tools/README.md) prepares reviewable initial images;
`BcpMonitor` can use provisioned identity for actuator-free monitoring only after
explicit write-enabled build configuration. Run protocol, stateful, transport
and power-loss tests with `python3 firmware/tests/bcp/run_native_tests.py`.

## Wiring

The [RF buoy and tether guide](Documentation/Wiring/RF_BUOY_TETHER.md) shows the
planned RFM69HCW shore/buoy bridges, full-duplex CAT6 tether recommendation, module
candidates and transport work still required.

To wire reciever see this diagram
![](/Documentation/Wiring/FS-IA6B_reciever_wireing.png)
