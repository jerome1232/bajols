# Vehicle BCP integration — actuator-disabled stage

`TelemetryProof` now defaults to the composed BCP monitoring/telemetry path in
`bcpVehicle.cpp`. It continuously services bounded USB serial, verified startup,
persistence and instrument reporting. It does not initialize motors, the PWM/servo
board, ballast outputs or the FlySky receiver. There is no blocking LED startup.
No code in this stage applies movement intent to hardware. The default path now
builds for Mega 2560 and classic ESP32 Dev Module (4 MB flash). Follow the complete
[build, dump, provisioning and readback guide](../../BUILD_AND_PROVISION.md) for
both platforms before attempting a live serial connection.

## Build modes

| Build definition | Behavior |
|---|---|
| None | BCP binary serial at 115200/8N1; Persistent writes disabled; no actuator support |
| `BAJOLS_BCP_DEMO_TELEMETRY` | Synthetic 2 m depth, 12 V supply, changing heading and zero pitch; every sample is explicitly marked simulated |
| `BAJOLS_BCP_ENABLE_EEPROM_WRITES` | Permits verified EEPROM/NVS boot/latch journal writes; requires explicit `BAJOLS_BCP_EEPROM_BASE` and `BAJOLS_BCP_WRITE_TIMEOUT_MS` |
| `BAJOLS_ENABLE_LEGACY_CONTROL` | Mega-only legacy FlySky-to-actuator experiment; excludes the BCP adapter entirely |

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

Build definitions may be supplied with Arduino CLI's
`--build-property 'compiler.cpp.extra_flags=-DNAME ...'`. Build configuration is not
permission to upload firmware or change EEPROM. No upload/provisioning command is
included here. The existing Compile Sketches workflow builds the default BCP path.
The legacy option requires the receiver/actuator libraries listed in the root README.
It bypasses BCP arbitration and is not the operational vehicle-control implementation.

## Telemetry and command boundary

Depth, supply voltage, heading and pitch reporting use BCP's fixed units. The
adapter publishes at most one newly acquired snapshot every 200 ms. Without the
demo definition, these sources report SensorFault, unknown sample age and no valid
reading: no raw ADC or fake pressure value is passed off as calibrated depth or
voltage. The capability mask describes supported report types, not current health.
RPM and object reporting are not advertised until their drivers are integrated.

Trusted instrument adapters can call `MonitoringRuntime::SetTelemetry(snapshot,
acquired_at)`; each record's sample age is relative to that timestamp. Publication
adds elapsed onboard time rather than refreshing an old reading. Unknown ages stay
unknown; known ages saturate, then become unknown until a new acquisition. Counts,
IDs, duplicates, quality bits and advertised support are validated before replacing
the retained snapshot. The serial endpoint coalesces unsent telemetry frames.

A verified session supports handshake, capabilities, status, heartbeat, command
results, Disarm and Emergency stop. Movement modes, axes, ballast systems and
movement ownership remain unavailable. Arm is rejected; the ground station must
not treat a successful connection or telemetry decode as movement authorization.
Emergency stop still follows the existing durable-latch rules. No vehicle-local
reset pin/procedure, sensor calibration or ballast completion feedback is invented.

The next integration stage is a reviewed vehicle profile, fresh FlySky adapter,
physical owner/reset inputs and actuator adapters behind the common safety manager.
Ballast hardware and powered validation remain separate prerequisites.
