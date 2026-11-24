# Vehicle BCP integration

## 1. USB serial actuator bench build

Use the Mega 2560 BCP bench profile on a dry bench with the propeller removed.
It requires provisioned writable EEPROM and physical ownership, stop and reset
switches. BCP runs on the Mega’s USB serial port at 115200 baud, 8N1.
[Compile, upload and test](../../BUILD_AND_PROVISION.md#mega-compile-and-upload-with-actuator-writes) · [Actuator and switch wiring](../../../Documentation/Wiring/MEGA_ACTUATORS.md#mega-bcp-bench-control-profile)

## 2. RF bench build

Build the two ESP32 radio bridges and check the RF transport with an actuator-disabled
peer. For a tethered Mega, add `BAJOLS_BCP_USE_SERIAL1` to the writable vehicle build
and connect through the shore ESP32’s USB serial port.
[Radio bridge builds](../BcpRadioBridge/README.md#build) · [Mega tether setup](../../BUILD_AND_PROVISION.md#mega-use-the-rf-buoy-and-rs-422-tether)

## Build modes

| Build definition | Behavior |
|---|---|
| None | BCP binary serial at 115200/8N1; Persistent writes disabled; no actuator support |
| `BAJOLS_BCP_DEMO_TELEMETRY` | Synthetic depth, 12 V supply, changing heading and zero pitch; every sample is explicitly marked simulated |
| `BAJOLS_BCP_DEMO_DEPTH_MM` | Fixed simulated depth in millimetres. Requires the demo flag and defaults to `2000` (2 m) |
| `BAJOLS_BCP_ENABLE_EEPROM_WRITES` | Permits verified EEPROM/NVS boot/latch journal writes; requires explicit `BAJOLS_BCP_EEPROM_BASE` and `BAJOLS_BCP_WRITE_TIMEOUT_MS` |
| `BAJOLS_BCP_ENABLE_BENCH_CONTROL` | Mega-only dry bench control for propulsion and control surfaces. Requires writable identity storage and physical switches. |
| `BAJOLS_BCP_USE_SERIAL1` | Mega-only BCP over Serial1 (D18 TX, D19 RX) for the RS-422 tether. See the [tether build guide](../../BUILD_AND_PROVISION.md#mega-use-the-rf-buoy-and-rs-422-tether). |
| `BAJOLS_ENABLE_LEGACY_CONTROL` | Mega-only legacy FlySky-to-actuator experiment; excludes the BCP adapter entirely |
| `BAJOLS_LEGACY_SHIELD_ENGINE_PWM` | With the legacy flag, routes main motor PWM to PCA9685 channel 2 and pump enable to channel 3 |

### Monitoring builds

The default monitoring build supports the Mega 2560 and classic ESP32 Dev Module
with 4 MB flash.

On ESP32, the supplied partition table isolates BCP NVS at `0x310000`; the
`BAJOLS_BCP_EEPROM_BASE` setting is logical **0**, not this flash address.

The default read-only build cannot advance the durable boot counter and therefore
cannot establish a BCP session, even on provisioned EEPROM. A live serial session
requires the separately reviewed writable mapping/timeout, deliberate write-enabled
build and an already provisioned identity image. Blank/corrupt storage fails closed;
this sketch never provisions identity automatically. See
[offline provisioning](../../tools/README.md) and
[storage policy](../../../protocol/FIRMWARE_STORAGE_PROPOSAL.md).

From the repository root, compile the default path with the local library:

```sh
arduino-cli compile --fqbn arduino:avr:mega --libraries firmware/Libraries firmware/Experimental/TelemetryProof
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries firmware/Libraries firmware/Experimental/TelemetryProof
```

### Legacy FlySky build

The Mega-only FlySky experiment uses the bundled IBusBM 1.1.4 and Encoder 1.4.4
libraries. From the repository root:

```sh
mkdir -p /tmp/bajols-legacy-libraries
unzip -o firmware/Libraries/IBusBM-1.1.4.zip -d /tmp/bajols-legacy-libraries
unzip -o firmware/Libraries/Encoder-1.4.4.zip -d /tmp/bajols-legacy-libraries
arduino-cli compile --fqbn arduino:avr:mega \
  --libraries firmware/Libraries --libraries /tmp/bajols-legacy-libraries \
  --build-property 'compiler.cpp.extra_flags=-DBAJOLS_ENABLE_LEGACY_CONTROL' \
  firmware/Experimental/TelemetryProof
```

The legacy path drives hardware directly and bypasses BCP arbitration.
[FlySky receiver wiring](../../../Documentation/Wiring/FS-IA6B_reciever_wireing.png)
For legacy serial logging, add one of `-DDEBUG_INFO`, `-DDEBUG_WARN`,
`-DDEBUG_ERROR`, or `-DDEBUG_TRACE` to the build flags.
See the [Mega actuator wiring and shield PWM build](../../../Documentation/Wiring/MEGA_ACTUATORS.md)
for the channel and pin assignments.

## Telemetry and command boundary

Depth, supply voltage, heading and pitch reporting use BCP's fixed units. The
adapter publishes at most one newly acquired snapshot every 200 ms. Without the
demo definition, these sources report SensorFault, unknown sample age and no valid
reading: no raw ADC or fake pressure value is passed off as calibrated depth or
voltage. The capability mask describes supported report types, not current health.
RPM and object reporting are not advertised until their drivers are integrated.
For a bench test at 4.5 m, add `-DBAJOLS_BCP_DEMO_TELEMETRY
-DBAJOLS_BCP_DEMO_DEPTH_MM=4500` to the compile flags. This value goes to
telemetry only. The depth safety interlock still requires calibrated sensor data.

Trusted instrument adapters can call `MonitoringRuntime::SetTelemetry(snapshot,
acquired_at)`; each record's sample age is relative to that timestamp. Publication
adds elapsed onboard time rather than refreshing an old reading. Unknown ages stay
unknown; known ages saturate, then become unknown until a new acquisition. Counts,
IDs, duplicates, quality bits and advertised support are validated before replacing
the retained snapshot. The serial endpoint coalesces unsent telemetry frames.

A verified monitor-only session supports handshake, capabilities, status, heartbeat, command
results, Disarm and Emergency stop. Movement modes, axes, ballast systems and
movement ownership remain unavailable. Arm is rejected; the ground station must
not treat a successful connection or telemetry decode as movement authorization.
Emergency stop still follows the existing durable-latch rules. No vehicle-local
reset pin/procedure, sensor calibration or ballast completion feedback is invented.

The [Mega BCP bench build](../../../Documentation/Wiring/MEGA_ACTUATORS.md#mega-bcp-bench-control-profile)
uses the shared BCP session manager with physical owner, stop and reset inputs.
The in-water profile still requires calibrated depth and ballast feedback.
