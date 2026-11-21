# BAJOLS ground station

A .NET 10/Avalonia desktop ground station for Ubuntu/GNOME and Windows. It includes
BCP encoding, explicit session establishment, validated YAML configuration, a serial
adapter, a simulated peer, keyboard, trackpad and gamepad input, and a live
status dashboard.
It starts disconnected and never automatically connects, clears failsafe or arms.

## Build, test and run

Install a .NET 10 SDK. From this directory:

```sh
dotnet restore Bajols.GroundStation.slnx --locked-mode
dotnet build Bajols.GroundStation.slnx --configuration Release --no-restore
dotnet test Bajols.GroundStation.slnx --configuration Release --no-build --no-restore
dotnet run --project Bajols.GroundStation
```

Run the desktop application in a graphical session. CI builds/tests on Linux and
Windows; headless desktop tests do not need a display server. The Linux serial test
uses a pseudo-terminal, not vehicle hardware; Windows needs a real loopback device
for the equivalent driver test and explicitly skips that Linux-only test.

`global.json` accepts stable .NET 10 feature bands. Pinned package lock files include
Avalonia 12.1.6, System.IO.Ports 10.0.0 and YamlDotNet 18.1.0. Update lock files
intentionally when changing dependencies.

## Operate the simulator

1. Press **Connect**. The bundled configuration selects the simulator and checks its
   vehicle ID. Status changes from unconfirmed to confirmed; telemetry is marked SIM.
2. Hold **Space** with forward/yaw/dive centered. After one second of fresh neutral
   input, press **Arm** while continuing to hold Space.
3. Hold Space and drag the circular pad. The top requests nose up, the bottom
   requests nose down, and left and right request port and starboard yaw.
   The pad combines pitch and yaw within a unit disk and springs to neutral on
   release or capture loss. R/F request nose down/up and A/D request port/starboard.
   The throttle slider to the right requests ahead at the top, neutral in the
   center, and reverse at the bottom. W/S request ahead/reverse. Keyboard effort is 50%.
   Pad dragging does not enable input or arm the vehicle. The **Buoyancy** control
   is a three-position vertical selector: Positive at the top, Neutral in the
   middle and Negative at the bottom. Neutral is selected initially and after
   input release. These are
   high-level effort/target commands, never actuator outputs.
4. Releasing Space, losing window focus or disconnecting
   clears local input. Without fresh movement the peer enters command-loss failsafe.
5. Restore held neutral input, press **Clear failsafe**, complete qualification again,
   then explicitly arm. Resumed communication alone never resumes control.

There is no on-screen input-enable button. Space remains the hold-to-enable
control, including while clicking Arm or Clear failsafe. The input snapshot expires
after 200 ms if the UI stops refreshing it, independently of the background
communication loop. Open **Controls** in the dashboard header for the keyboard
and gamepad reference. Opening it releases held input.

### Keyboard and on-screen controls

| Action | Control |
| --- | --- |
| Hold to enable input | Space |
| Pitch nose up / down | F / R, or drag the trackpad up / down |
| Yaw port / starboard | A / D, or drag the trackpad left / right |
| Throttle ahead / reverse | W / S, or move the throttle slider up / down |
| Arm, clear failsafe, disarm, emergency stop | Corresponding dashboard buttons |
| Positive, neutral or negative buoyancy | Top, center or bottom of the buoyancy slider |

Simulation controls inject movement loss, the 5 m depth condition and depth-sensor
loss. **Local emergency reset** represents an action at the simulated vehicle and
is only available in simulation. No remote BCP reset message is sent. The model
preserves emergency state across link reconnects and model reboots during the app
session. It has no durable emergency-latch storage across application restarts.

## Dashboard layout and depth indicator

Telemetry and vehicle status overlay the 3D scene; there is no separate top row
of sensor cards or standalone status panel. Supply voltage and propulsion RPM are
compact values with units, above and to either side of the submarine. The heading
readout sits below-left: a prominent 16-point compass direction (including NNW),
with smaller degrees and simulation/estimate provenance underneath. Pitch remains
below the view. Missing or stale heading displays a dash, not a compass direction.

The depth gauge is to the right of the submarine, with surface at 0 m and the
bottom of the display scale at 4.5 m. A marker shows current depth, with a small
colour-matched depth readout alongside it. Both warm from teal through amber to
red approaching 4.5 m; the marker clamps at the bottom for deeper readings while
the text continues to report actual depth. Invalid, stale or disconnected depth
hides the marker and shows a dash. This gauge does not change onboard depth policy.

The full-width control panel sits below the scene. The circular pitch/yaw pad
and vertical throttle slider share an outlined box centred in the panel. The throttle
label is beside the neutral notch, with ahead and reverse percentages near the ends.
Buoyancy sits immediately to the right, in an outlined box of matching height.
Arm, Disarm, Clear failsafe and Emergency stop share a box anchored at the bottom.
The default and minimum window layouts are checked without scrollbars.

## Visual attitude

One perspective-rendered 3D submarine combines received heading and pitch. Its
rounded hull, sail, periscope, fins and propeller rotate together, with shaded
surfaces drawn in depth order. The camera stays south of the fixed compass plane:
north points into the screen, south toward the viewer, east right and west left.
Positive measured pitch raises the nose; the input controls do not directly rotate
the display. Missing, invalid or stale heading/pitch hides the model and marks
attitude unconfirmed. Heading wraps continuously across north. Roll is not shown.
The renderer uses existing Avalonia drawing APIs and .NET geometry math; no new
3D package or hardware control behavior is introduced.

Simulation now includes deliberately simple kinematics: yaw changes heading only
while thrust is applied, with turn rate proportional to thrust magnitude. Dive
effort approaches a pitch angle, forward effort and pitch affect depth, and buoyancy
actions add illustrative ascent/descent. Hold Space, arm, then use W/S, A/D and R/F
to see the response. These rates are visual fixtures, not physical calibration.

## Status and command behavior

The left overlay uses plain-language safety states, selected control source,
buoyancy activity/estimate, arming requirements, restrictions and historical action
and fault reports. For example, command expiry is shown as **Stopped · input lost**.
Unknown or stale status stays **Status unconfirmed**; no missing report means safe.

The **Neutral input check** is a progress bar and percentage for the onboard
one-second qualification. It measures fresh centred forward/yaw/dive input, not
neutral buoyancy. It resets when qualification breaks, and is hidden while armed.
Waiting for fresh control input means the selected source is not supplying current
movement commands, even if the communications link is healthy. A completed bar is
not permission by itself: other blockers, the local input hold and an explicit
Arm action still apply. Unconfirmed status shows an unavailable check with zero
progress. Fault and action reports are historical, not evidence of current state.


- Connection, session and status freshness are distinct. Missing or stale status is
  UNKNOWN/unconfirmed, never inferred disarmed. Status expires after one second.
- Telemetry checks validity, quality flags, sample age and local elapsed time. Missing,
  invalid or old measurements display a dash. Simulated/estimated values are labeled.
- Arm requires confirmed compatible capabilities, ownership, a fresh ticket, fresh
  enabled operator input and zero onboard arming blockers.
- Disarm and emergency stop remain available to an established configured session
  even if status/tickets are stale. A lost physical connection cannot deliver them.
- Request results show onboard acceptance/rejection; sending a request is not proof
  that a physical action completed. There are no automatic command retries.
- A wrong vehicle ID, unsupported status or mismatched capabilities cannot authorize
  movement. Changed capabilities within a session terminate the connection.

## YAML configuration

The bundled `Bajols.GroundStation/station.yaml` is copied beside the executable.
Override it explicitly:

```sh
dotnet run --project Bajols.GroundStation -- --config /path/to/station.yaml
```

Schema 1 selects `transport: simulator` or `transport: serial`. Serial also requires
an explicit port (such as `/dev/ttyACM0` or `COM3`) and the actual provisioned
32-hex-character vehicle ID; the simulator ID cannot be used for serial. Baud is
fixed at the BCP v1 115200/8N1 profile. This adapter requires BCP firmware/another BCP
peer. The default `TelemetryProof` build now integrates BCP without actuator
support; it requires verified writable startup before establishing a session.
See [vehicle integration](../firmware/Experimental/TelemetryProof/README.md) and
[the Mega/ESP32 build and provisioning guide](../firmware/BUILD_AND_PROVISION.md).
Its default unavailable instrument reports are not live sensor readings; an
optional firmware demo explicitly marks synthetic readings. Missing movement
capabilities keep Arm unavailable even when the serial link is confirmed.

Unknown/duplicate YAML keys, oversized files, unsupported schemas, missing/invalid
identity, nonfinite input settings and out-of-range timeouts/buffers are rejected.
Configuration errors disable connection and appear in the dashboard. There is no
silent fallback to simulation after a malformed configuration. Restart to reload.
Desktop configuration does not override onboard safety thresholds or the depth limit.

## Project boundaries

C# style: use one statement per line and braces for every conditional body.
Document every type, record, field, property, event, enum member, method and
constructor, including private helpers and tests, with XML summaries. Give grouped
fields separate declarations and summaries. Document positional record properties
and primary constructors with parameter descriptions; document local functions
with explanatory comments. The ground-station `.editorconfig` records formatting
preferences. The build enforces braces and emits XML documentation, treating missing
public API documentation as an error.

| Project | Responsibility |
|---|---|
| Bajols.Protocol | Transport-independent messages, CRC/COBS codec and bounded decoder; no other project/package dependencies |
| Bajols.Core | Domain connection state and normalized movement intent |
| Bajols.Communication | Client session/authorization, bounded serial and simulator wire transports |
| Bajols.Input | Fresh input snapshots, hold gating, normalization and expiry |
| Bajols.Configuration | Strict human-editable YAML schema and validation |
| Bajols.Simulator | Deterministic reference peer/safety state model, independent of Avalonia and serial APIs |
| Bajols.GroundStation | Avalonia views, observable view model and explicit operator commands |

The simulator and ground station communicate through encoded BCP frames; neither
gets a codec bypass for command handling. Simulator queues hold at most 16 whole
frames in each direction. Its outbound congestion policy drops frames rather than
building a backlog. Serial reads use a caller buffer capped to 256-byte chunks;
writes allow at most one 116-byte frame. Driver operations use finite timeouts off
the UI thread; cancellation closes the serial connection. Actual driver open/close
latency remains OS/device-dependent and requires hardware testing.

## Gamepad

The ground station uses the first SDL2-mapped game controller. On Debian or
Ubuntu, install `libsdl2-2.0-0` if needed. On Windows, put the SDL2 runtime's
`SDL2.dll` beside the executable and match its architecture. Connect the
controller by USB or Bluetooth and keep the ground-station window focused.

| Control | Action |
| --- | --- |
| Left stick | Trackpad pitch and yaw. Up requests nose up, and left/right turn port/starboard. |
| Right stick up/down | Ahead/reverse throttle. |
| Left shoulder | Hold to enable input. Center both sticks for neutral qualification. |
| Right trigger | Arm once the normal arming checks pass. |
| Right shoulder | Clear failsafe when the normal checks allow it. |
| Left trigger | Disarm. |
| A (south face button) | Emergency stop. |
| D-pad up/down | Select positive/negative buoyancy while holding left shoulder. |
| D-pad left/right | Select neutral buoyancy while holding left shoulder. |

Releasing left shoulder, unplugging the controller, or losing window focus
clears gamepad movement and buoyancy. Press left shoulder again to resume
sampling. Disarm and emergency stop work with an active session without holding
left shoulder. The vehicle still determines which actions are available.

## Simulator limitations and next hardware work

The simulated vehicle models authorization, tickets, replay/idempotency, safety
latches and illustrative motion, not hydrodynamics. Depth can also be overridden by
fault injection, buoyancy estimate stays unknown, and evacuation completion is simulated. Its 4.5 m
intervention / 4 m release profile is a test fixture, not approved hardware calibration.
FlySky ownership can be selected in model tests, but no physical iBus adapter or
FlySky arming switch is implemented in this desktop simulator.

Onboard firmware still owns real actuator control and safety. Persistent boot/latch
storage, ballast hardware, sensors, profile validation and powered testing remain
separate work. Simulated behavior and driver-loopback tests do not validate them.

## Simulated objects

In the attitude panel, set range (0–6 m) and bearing, then choose **Place object**
to add a stationary world point relative to the vehicle's current pose. Movement
changes its reported relative position. **Clear objects** removes all points.
At most 32 points can be placed; the synthetic sensor reports the nearest four in
a six-metre forward hemisphere, marking truncation. The optional **Body-relative test
return** checkbox injects a return that follows the vehicle for display testing.

Object acquisitions travel through encoded BCP object reports and the client's
session/freshness checks. Expired acquisitions are marked stale; heartbeats cannot
make them current. Fresh markers flash and become more prominent closer to the
sensor. This is a schematic simulation, with no selected physical sensor,
calibrated hull clearance, automatic avoidance or rendered uncertainty volume.

The object display retains the latest source snapshot within six metres when its
acquisition expires. Historical markers stop flashing and fade to grey/white over
one second after expiry, with a stale label. Their positions are last reported
body-relative positions, not motion-compensated tracks. New source snapshots
replace them (including empty/fault snapshots); disconnect clears them. This is a
presentation change and does not renew freshness or change the BCP wire layout.

The six-metre display/simulator limit provisionally covers the anticipated 3–6 m
sensor class. It is not a validated physical detection range or selected hardware
specification. BCP ranges remain unsigned millimetres; no wire change is needed.

The dashed ray from the bow is a **one-metre reference**, not the sensor range or
field of view. Object spacing uses 3.4 schematic model units per metre, with
perspective projection and edge clamping. This enlarged visual scale is independent
of the six-metre reporting/display limit and distance-based warning prominence.
Distant markers can reach the view edge; their labels retain reported distance.
Neither the model dimensions nor marker spacing establish calibrated hull clearance.

## Planned buoy radio link

The selected initial radio is RFM69HCW at 915 MHz, with a buoy and a wired
submarine tether. See [the arrangement and recommended modules](../Documentation/Wiring/RF_BUOY_TETHER.md)
for the shore USB bridge, buoy SPI/UART bridge, differential tether interfaces and
RF packet-size constraints. These are planned transport additions, not currently
implemented ground-station radio support. Vehicle identity and command authority
remain on the submarine across the bridged link.
