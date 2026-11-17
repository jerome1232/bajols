![Arduino Lint](https://github.com/jerome1232/bajols/actions/workflows/check-arduino.yml/badge.svg) ![Arduino Compile](https://github.com/jerome1232/bajols/actions/workflows/compile-sketches.yml/badge.svg) [![Ground Station Build](https://github.com/jerome1232/bajols/actions/workflows/build-groundstation.yml/badge.svg)](https://github.com/jerome1232/bajols/actions/workflows/build-groundstation.yml)

# bajols
Remotly controlled semi-autonomous submarine


Currently there only exists an experimental sketch that fakes
sensor data to demonstrate streaming required telemetry to 
a FS-i6X controller as well as reading data from it.

Currently it will react to throttle (left joystick) and in response will
change rpms. It will also react to swC switch, while in the up position it will
decrease pressure, in middle postion not change pressure, and when in down postion
increase pressure. You should see changes to altitute as pressure changes.

It constantly changes heading (shown as '5' sensor on flysky remote)
Currently heading is 0..360 degrees.

## Repository layout

```text
firmware/
  Experimental/
    TelemetryProof/    # Existing Arduino sketch and supporting source files
    Wiring/            # Experimental wiring diagrams
  Libraries/           # Bundled Arduino library archive
Documentation/         # Sensor notes and receiver wiring documentation
protocol/              # BCP specification, conformance plan and shared wire vectors
groundstation/         # .NET/Avalonia ground station, simulator and tests
```

The firmware was moved from `src/` without changing its behavior. The Arduino
sketch remains experimental and does not implement BCP. The C# protocol codec, simulated peer and desktop ground station are implemented
under `groundstation/`.
The [BCP v1 discussion draft](protocol/BCP_V1_DRAFT.md) records agreed control and
safety behavior separately from proposed wire details and open decisions.
The [ground station](groundstation/README.md) separates protocol, communication,
domain, input, configuration and UI code. It starts disconnected and includes an
encoded BCP simulator, explicit session/control actions, validated YAML, live status
and bounded serial communication. The bundled configuration selects simulation.

## Compile
Libraries needed to compile this sketch - If not included in repo it's installable from arudino IDE.

|Library|Location in Repo|Web link|Version|
|-------|----------------|-------|-------|
|IBusBM|/firmware/Libraries/IBusBM-1.1.4.zip|[bmellink/IBusBM](https://github.com/bmellink/IBusBM)|1.1.4
|Servo|-|[Servo](https://github.com/arduino-libraries/Servo.git)|1.3.0
|Encoder|-|[Encoder](https://github.com/PaulStoffregen/Encoder)|1.4.4
|Adafruit_BusIO|-|[Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO)|1.17.4
|Adafruit_PWM-Servo-Driver|-|[Servo-Driver](https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library)|3.0.2

Open [TelemetryProof.ino](firmware/Experimental/TelemetryProof/TelemetryProof.ino)
in the Arduino IDE and select Arduino Mega 2560. Compile CI and Arduino Lint both
target `firmware/Experimental/TelemetryProof`.

## Wiring
To wire reciever see this diagram
![](/Documentation/Wiring/FS-IA6B_reciever_wireing.png)
