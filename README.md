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
Libraries needed to compile this sketch

|Library|Location in Repo|Weblink|Version|
|-------|----------------|-------|-------|
|IBusBM|/src/Libraries/IBusBM-1.1.4.zip|[bmellink/IBusBM](https://github.com/bmellink/IBusBM)|1.1.4


## Wiring
To wire reciever see this diagram
![](/Documentation/Wiring/FS-IA6B_reciever_wireing.png)
