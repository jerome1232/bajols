![Arduino Lint](https://github.com/jerome1232/bajols/actions/workflows/check-arduino.yml/badge.svg) ![Arduino Compile](https://github.com/jerome1232/bajols/actions/workflows/compile-sketches.yml/badge.svg)

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

## Compile
Libraries needed to compile this sketch - If not included in repo it's installable from arudino IDE.

|Library|Location in Repo|Web link|Version|
|-------|----------------|-------|-------|
|IBusBM|/src/Libraries/IBusBM-1.1.4.zip|[bmellink/IBusBM](https://github.com/bmellink/IBusBM)|1.1.4
|Servo|-|[Servo](https://github.com/arduino-libraries/Servo.git)|1.3.0
|Encoder|-|[Encoder](https://github.com/PaulStoffregen/Encoder)|1.4.4
|Adafruit_BusIO|-|[Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO)|1.17.4
|Adafruit_PWM-Servo-Driver|-|[Servo-Driver](https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library)|3.0.2

Main .ino file is located at /src/Experimental/TelemetryProof/TelemetryProof.ino

## Wiring
To wire reciever see this diagram
![](/Documentation/Wiring/FS-IA6B_reciever_wireing.png)
