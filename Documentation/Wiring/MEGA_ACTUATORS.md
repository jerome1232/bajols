# Mega 2560 actuator wiring

The FlySky `TelemetryProof` build drives the main motor, rudder servo, dive
plane servo, and ballast pump. For a Mega fitted with the
PCA9685 servo shield, the optional shield PWM build moves the main motor
H-bridge's PWM signal from Mega D11 to PCA9685 channel 2.

## Output assignments

| Source | Destination | Signal |
| --- | --- | --- |
| PCA9685 channel 0 | Rudder servo | Position pulse |
| PCA9685 channel 1 | Dive plane servo | Position pulse |
| PCA9685 channel 2 | Main motor L298N `ENA` | Active-high duty cycle in the shield PWM build |
| Mega D22, D23 | Main motor L298N `IN1`, `IN2` | D22 high for forward, D23 high for reverse |
| L298N `OUT1`, `OUT2` | Main motor leads | Bridge A motor output |
| PCA9685 channel 3 | Ballast pump L298N `ENB` | High to fill, low to disable |
| Mega D24, D25 | Ballast pump L298N `IN3`, `IN4` | D24 high and D25 low to fill, both low to stop |
| L298N `OUT3`, `OUT4` | Ballast pump leads | Bridge B motor output |

In the shield PWM build, disconnect the D11-to-PWM wire. Remove the module's
`ENA` and `ENB` jumpers, then connect the shield's channel 2 and 3 **signal**
pins to `ENA` and `ENB` respectively. These jumpers are separate from the
module's 5 V regulator jumper. The Mega, shield and L298N logic share ground.
The Mega connects to the shield through SDA (D20) and SCL (D21) at I²C address
`0x40`. The main motor encoder remains on Mega D2 and D3. Channels 4–15 are
unassigned. Mega D27 is available.

The shield runs all channels at a shared nominal **50 Hz** for the servos.
Channel 2 provides a logic-level duty signal to the L298N's TTL-compatible
enable input. With `ENA` low, the motor coasts. Equal direction inputs with
`ENA` high apply electrical braking. Keep `ENA` low during power transitions.
Servo, motor and pump power go through their respective supplies and
drivers. [PCA9685 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf) ·
[L298 datasheet, table 5](https://www.st.com/resource/en/datasheet/l298.pdf)

| FlySky ballast switch C | Pump |
| --- | --- |
| Up | Off |
| Middle | Fill |
| Down | Off |

## Build the shield PWM variant

Extract the [bundled legacy libraries](../../firmware/Experimental/TelemetryProof/README.md#legacy-flysky-build) into
`/tmp/bajols-legacy-libraries` as shown in the README, then compile from the
repository root:

```sh
arduino-cli compile --fqbn arduino:avr:mega \
  --libraries firmware/Libraries --libraries /tmp/bajols-legacy-libraries \
  --output-dir /tmp/bajols-build/mega-flysky-shield \
  --build-property 'compiler.cpp.extra_flags=-DBAJOLS_ENABLE_LEGACY_CONTROL -DBAJOLS_LEGACY_SHIELD_ENGINE_PWM' \
  firmware/Experimental/TelemetryProof
```

The legacy build with only `BAJOLS_ENABLE_LEGACY_CONTROL` uses D11 for main
motor PWM and expects the pump driver to be enabled separately.

## Mega BCP bench control profile

This opt-in build uses BCP to check control-surface movement and motor
direction on a dry bench with the propeller removed.

### Physical switches

Wire each switch between its assigned Mega digital input and **Mega GND**.
The profile configures these inputs as `INPUT_PULLUP`, so closed contacts
read LOW and open contacts read HIGH.

| Mega pin | Contact type | Closed to ground (LOW) | Open (HIGH) |
| --- | --- | --- | --- |
| D28 | Maintained PC ownership selector | Select ground-station control | No movement owner |
| D29 | Normally closed emergency-stop loop | Stop loop healthy | Assert emergency stop, including on a broken or disconnected wire |
| D30 | Normally open momentary reset button | A fresh press requests local latch reset | Released |

Restore the D29 loop before pressing D30. Restoring the loop alone leaves the
stop latched. A D30 press requests a verified persistent clear and returns the
controller to disarmed after it succeeds. A reset button held at startup or
while the stop loop is open must be released and pressed again. The D29 input
reports the stop to the firmware. An independent hardware cutoff is required
to interrupt motor power when the controller cannot respond.

### Output limits and arming

- PCA9685 channel 0 drives yaw through the rudder and channel 1 drives pitch
  through the dive plane. Reuse the FlySky calibration of **1,500 µs center**
  and **1,372–1,627 µs** endpoints for both. On the bench, verify that positive
  yaw turns starboard and positive dive requests nose down.
- Channel 2 drives L298N `ENA`, with direction on D22/D23 as shown above.
  Cap motor PWM duty at **20%** in either direction.
- Advertise forward, yaw and dive control with no ballast capability. Leave
  the pump outside bench control. The bench build holds channel 3
  off and drives D24/D25 low. Simulated depth is optional
  telemetry marked **SIM**. Bench arming does not depend on depth or ballast
  feedback.
- Require D28 PC ownership, a verified BCP session, one continuous second of
  fresh neutral input, and an explicit Arm command. Disarm, loss of ownership,
  command timeout, or emergency stop sets motor PWM to zero and centers both
  servos. Reset and reconnect each require a new explicit Arm command before
  motion resumes.

The normal in-water profile retains its calibrated depth and ballast
requirements.

### Build and connect

Follow the [USB serial bench build, upload and actuator checks](../../firmware/BUILD_AND_PROVISION.md#mega-compile-and-upload-with-actuator-writes).
Use the EEPROM base and vehicle ID from the provisioning manifest. The ground
station connects to the Mega’s USB port at 115200 baud.

If the PCA9685 fails to initialize, loses its configuration, or rejects an
output write, the Mega sets both L298N direction inputs low and asserts the
stop latch. After fixing the shield fault, restart and use D30 to clear the
latch. Motor power cutoff and servo position during a shield fault depend on
the hardware.
